#include "core/metrics.h"
#include "geometry-importer/geometry.h"
#include "geometry-importer/importer.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <cmath>
#include <gdal_priv.h>
#include <iostream>
#include <memory>
#include <ogrsf_frmts.h>
#include <stdexcept>

namespace
{
void require(bool condition, const QString &message)
{
   if (!condition)
      throw std::runtime_error(message.toStdString());
}
struct DatasetCloser
{
   void operator()(GDALDataset *p) const
   {
      if (p)
         GDALClose(p);
   }
};
struct GeometryCloser
{
   void operator()(OGRGeometry *p) const
   {
      OGRGeometryFactory::destroyGeometry(p);
   }
};
struct FeatureCloser
{
   void operator()(OGRFeature *p) const
   {
      OGRFeature::DestroyFeature(p);
   }
};
struct TransformCloser
{
   void operator()(OGRCoordinateTransformation *p) const
   {
      OCTDestroyCoordinateTransformation(p);
   }
};
using Dataset = std::unique_ptr<GDALDataset, DatasetCloser>;
using Geometry = std::unique_ptr<OGRGeometry, GeometryCloser>;
using Feature = std::unique_ptr<OGRFeature, FeatureCloser>;

QString sha256(const QString &path)
{
   QFile file(path);
   require(file.open(QIODevice::ReadOnly), "Cannot read " + path);
   QCryptographicHash hash(QCryptographicHash::Sha256);
   require(hash.addData(&file), "Cannot hash " + path);
   return QString::fromLatin1(hash.result().toHex());
}
void writeJson(const QString &path, const QJsonObject &value)
{
   QSaveFile file(path);
   require(file.open(QIODevice::WriteOnly), "Cannot write " + path);
   const auto bytes = QJsonDocument(value).toJson();
   require(file.write(bytes) == bytes.size() && file.commit(), "Cannot commit " + path);
}
void collectParts(const OGRGeometry &geometry, OGRGeometryCollection &parts,
                  OGRwkbGeometryType wanted)
{
   if (geometry.IsEmpty())
      return;
   if (wkbFlatten(geometry.getGeometryType()) == wanted)
      require(parts.addGeometry(&geometry) == OGRERR_NONE, "Cannot collect clipped geometry");
   else if (const auto *collection = dynamic_cast<const OGRGeometryCollection *>(&geometry))
      for (int i = 0; i < collection->getNumGeometries(); ++i)
         collectParts(*collection->getGeometryRef(i), parts, wanted);
   // Касания границы могут дать точки вместо линий/линии вместо полигонов:
   // эти компоненты меньшей размерности не включаются в результат.
}

void prepare(const QJsonObject &scenario, const QDir &source_dir, const QDir &output_dir,
             const QString &config_hash)
{
   const QString id = scenario["id"].toString();
   const QString source_name = scenario["source_layer"].toString();
   const auto bounds = scenario["bbox_wgs84"].toArray();
   require(bounds.size() == 4, "bbox_wgs84 needs four numbers: " + id);
   for (const auto &coordinate : bounds)
      require(coordinate.isDouble() && std::isfinite(coordinate.toDouble()), "Invalid bbox: " + id);
   const double x0 = bounds[0].toDouble(), y0 = bounds[1].toDouble();
   const double x1 = bounds[2].toDouble(), y1 = bounds[3].toDouble();
   require(x0 < x1 && y0 < y1 && x0 >= -180 && x1 <= 180 && y0 >= -90 && y1 <= 90,
           "Invalid WGS84 extent: " + id);
   const int epsg = scenario["target_epsg"].toInt();
   OGRSpatialReference wgs84, target;
   require(wgs84.importFromEPSG(4326) == OGRERR_NONE && target.importFromEPSG(epsg) == OGRERR_NONE,
           "Cannot resolve CRS: " + id);
   wgs84.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
   target.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
   require(target.IsProjected() && std::abs(target.GetLinearUnits() - 1.0) < 1e-12,
           "Target must be projected in metres: " + id);
   double west = 0, south = 0, east = 0, north = 0;
   require(target.GetAreaOfUse(&west, &south, &east, &north, nullptr) && x0 >= west && x1 <= east &&
              y0 >= south && y1 <= north,
           "Scenario bbox is outside target CRS area of use: " + id);
   std::unique_ptr<OGRCoordinateTransformation, TransformCloser> transform(
      OGRCreateCoordinateTransformation(&wgs84, &target));
   require(bool(transform), "Cannot create coordinate transformation: " + id);

   const QString input_path = source_dir.filePath(source_name + ".shp");
   Dataset input(static_cast<GDALDataset *>(GDALOpenEx(input_path.toUtf8().constData(),
                                                       GDAL_OF_VECTOR | GDAL_OF_READONLY, nullptr,
                                                       nullptr, nullptr)));
   require(bool(input), "Cannot open " + input_path);
   auto *source = input->GetLayerByName(source_name.toUtf8().constData());
   require(source && source->GetSpatialRef(), "Missing source layer or CRS: " + source_name);
   auto source_crs = *source->GetSpatialRef();
   source_crs.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
   require(source_crs.IsSame(&wgs84), "Source must be WGS84: " + source_name);
   require(source->GetLayerDefn()->GetFieldIndex("source_fid") < 0, "source_fid already exists");

   const bool polygon = scenario["geometry"].toString() == "polygon";
   require(polygon || scenario["geometry"].toString() == "line",
           "geometry must be line or polygon");
   const auto part_type = polygon ? wkbPolygon : wkbLineString;
   const QString output_path = output_dir.filePath(id + ".gpkg");
   auto *driver = GetGDALDriverManager()->GetDriverByName("GPKG");
   require(driver, "GDAL GeoPackage driver unavailable");
   Dataset output(driver->Create(output_path.toUtf8().constData(), 0, 0, 0, GDT_Unknown, nullptr));
   require(bool(output), "Cannot create " + output_path);
   auto *destination = output->CreateLayer(id.toUtf8().constData(), &target,
                                           polygon ? wkbMultiPolygon : wkbMultiLineString, nullptr);
   require(destination, "Cannot create output layer");
   for (int i = 0; i < source->GetLayerDefn()->GetFieldCount(); ++i)
      require(destination->CreateField(source->GetLayerDefn()->GetFieldDefn(i)) == OGRERR_NONE,
              "Cannot copy source attribute definition");
   OGRFieldDefn source_fid("source_fid", OFTInteger64);
   require(destination->CreateField(&source_fid) == OGRERR_NONE, "Cannot create provenance field");
   require(output->StartTransaction() == OGRERR_NONE, "Cannot start output transaction");

   OGRLinearRing ring;
   ring.addPoint(x0, y0);
   ring.addPoint(x1, y0);
   ring.addPoint(x1, y1);
   ring.addPoint(x0, y1);
   ring.addPoint(x0, y0);
   OGRPolygon clip;
   clip.addRing(&ring);
   source->SetSpatialFilterRect(x0, y0, x1, y1);
   source->ResetReading();
   qint64 selected = 0, written = 0, clipped_count = 0, dropped_touches = 0;
   while (Feature original{source->GetNextFeature()})
   {
      ++selected;
      const auto *geometry = original->GetGeometryRef();
      require(geometry && geometry->IsValid(),
              "Invalid source geometry, FID " + QString::number(original->GetFID()));
      Geometry intersection(geometry->Intersection(&clip));
      require(bool(intersection), "Clipping failed, FID " + QString::number(original->GetFID()));
      std::unique_ptr<OGRGeometryCollection> parts(
         polygon ? static_cast<OGRGeometryCollection *>(new OGRMultiPolygon)
                 : static_cast<OGRGeometryCollection *>(new OGRMultiLineString));
      collectParts(*intersection, *parts, part_type);
      if (parts->IsEmpty())
      {
         ++dropped_touches;
         continue;
      }
      OGREnvelope extent;
      geometry->getEnvelope(&extent);
      if (extent.MinX < x0 || extent.MaxX > x1 || extent.MinY < y0 || extent.MaxY > y1)
         ++clipped_count;
      require(parts->transform(transform.get()) == OGRERR_NONE && parts->IsValid(),
              "Projection failed or invalid output");
      Feature feature(OGRFeature::CreateFeature(destination->GetLayerDefn()));
      require(feature->SetFrom(original.get(), TRUE) == OGRERR_NONE, "Cannot copy attributes");
      feature->SetField("source_fid", original->GetFID());
      require(feature->SetGeometry(parts.get()) == OGRERR_NONE &&
                 destination->CreateFeature(feature.get()) == OGRERR_NONE,
              "Cannot write feature");
      ++written;
   }
   require(written > 0, "Scenario has no features: " + id);
   require(output->CommitTransaction() == OGRERR_NONE, "Cannot commit output features");
   output.reset();

   // Проверяем результат тем же импортёром, которым пользуется приложение.
   gi::Importer verifier(output_path.toStdString());
   const auto layers = verifier.layers();
   require(layers.size() == 1 && layers.front().crs.epsg == epsg &&
              !layers.front().crs.is_geographic && layers.front().crs.units == "metre",
           "Output CRS verification failed");
   const auto features = verifier.readLayer(layers.front().name);
   require(static_cast<qint64>(features.size()) == written, "Output feature count mismatch");

   QJsonObject source_hashes;
   for (const auto &extension : {".shp", ".shx", ".dbf", ".prj", ".cpg"})
   {
      const QString name = source_name + extension;
      if (QFile::exists(source_dir.filePath(name)))
         source_hashes[name] = sha256(source_dir.filePath(name));
   }
   QJsonObject manifest{
      {"scenario", scenario},
      {"config_sha256", config_hash},
      {"source_sha256", source_hashes},
      {"gdal_version", GDALVersionInfo("RELEASE_NAME")},
      {"output_file", id + ".gpkg"},
      {"output_sha256", sha256(output_path)},
      {"selected_source_features", selected},
      {"output_features", written},
      {"output_vertices", static_cast<qint64>(core::metrics::countVertices(features))},
      {"output_bounds", QJsonArray{layers.front().bounds.minX, layers.front().bounds.minY,
                                   layers.front().bounds.maxX, layers.front().bounds.maxY}},
      {"clipped_source_features", clipped_count},
      {"dropped_boundary_touches", dropped_touches},
      {"preparation", "Clip to WGS84 bbox, then project; preserve attributes and source_fid"},
      {"units", "metre"},
      {"evaluation_stride", 8},
      {"suggested_parameters",
       QJsonObject{{"douglas-peucker", QJsonObject{{"epsilon", 1000}}},
                   {"visvalingam-whyatt", QJsonObject{{"area_threshold", 100000}}},
                   {"li-openshaw", QJsonObject{{"window_size", 1000}}}}}};
   writeJson(output_dir.filePath(id + ".manifest.json"), manifest);
   std::cout << id.toStdString() << ": " << written << " features, EPSG:" << epsg << ", clipped "
             << clipped_count << '\n';
}
} // namespace

int main(int argc, char **argv)
{
   QCoreApplication app(argc, argv);
   try
   {
      require(argc == 4, "Usage: prepare-scenarios CONFIG.json SOURCE_DIRECTORY OUTPUT_DIRECTORY");
      QFile file(QString::fromLocal8Bit(argv[1]));
      require(file.open(QIODevice::ReadOnly), "Cannot read scenario config");
      QJsonParseError error;
      const auto config = QJsonDocument::fromJson(file.readAll(), &error).object();
      require(error.error == QJsonParseError::NoError && config["version"].toInt() == 1,
              "Invalid scenario configuration or version");
      const auto scenarios = config["scenarios"].toArray();
      require(!scenarios.isEmpty(), "No scenarios specified");
      const QDir source(QString::fromLocal8Bit(argv[2])), output(QString::fromLocal8Bit(argv[3]));
      QSet<QString> ids;
      const QRegularExpression safe_name("^[A-Za-z0-9_-]+$");
      for (const auto &value : scenarios)
      {
         const auto scenario = value.toObject();
         const QString id = scenario["id"].toString();
         require(safe_name.match(id).hasMatch() && !ids.contains(id) &&
                    safe_name.match(scenario["source_layer"].toString()).hasMatch(),
                 "Unsafe or duplicate scenario name");
         ids.insert(id);
         require(!QFile::exists(output.filePath(id + ".gpkg")) &&
                    !QFile::exists(output.filePath(id + ".manifest.json")),
                 "Output already exists; choose a new output directory: " + id);
      }
      require(QDir().mkpath(output.absolutePath()), "Cannot create output directory");
      GDALAllRegister();
      const QString config_hash = sha256(file.fileName());
      for (const auto &scenario : scenarios)
         prepare(scenario.toObject(), source, output, config_hash);
      return 0;
   }
   catch (const std::exception &error)
   {
      std::cerr << error.what() << '\n';
      return 1;
   }
}
