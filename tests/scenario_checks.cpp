#include "core/session.h"
#include "geometry-importer/importer.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <cmath>
#include <iostream>
#include <ogrsf_frmts.h>
#include <stdexcept>

namespace
{
void require(bool condition, const char *message)
{
   if (!condition)
      throw std::runtime_error(message);
}
QByteArray readFile(const QString &path)
{
   QFile file(path);
   require(file.open(QIODevice::ReadOnly), "Cannot read fixture output");
   return file.readAll();
}
} // namespace

int main(int argc, char **argv)
{
   QCoreApplication app(argc, argv);
   try
   {
      require(argc == 2, "Expected path to prepare-scenarios executable");
      QTemporaryDir directory;
      require(directory.isValid(), "Cannot create temporary scenario directory");
      GDALAllRegister();
      auto *driver = GetGDALDriverManager()->GetDriverByName("ESRI Shapefile");
      require(driver, "Shapefile driver unavailable");
      const auto source_path = directory.filePath("lines.shp");
      auto *source =
         driver->Create(source_path.toUtf8().constData(), 0, 0, 0, GDT_Unknown, nullptr);
      require(source, "Cannot create source fixture");
      OGRSpatialReference wgs84;
      require(wgs84.importFromEPSG(4326) == OGRERR_NONE, "Cannot resolve WGS84");
      wgs84.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
      auto *layer = source->CreateLayer("lines", &wgs84, wkbLineString, nullptr);
      require(layer, "Cannot create fixture layer");
      OGRFieldDefn name("name", OFTString);
      require(layer->CreateField(&name) == OGRERR_NONE, "Cannot create fixture attribute");
      for (int i = 0; i < 2; ++i)
      {
         OGRLineString line;
         if (i == 0)
         {
            line.addPoint(7, 50);
            line.addPoint(9, 50);
            line.addPoint(11, 50);
         }
         else
         {
            line.addPoint(7, 49);
            line.addPoint(8, 49);
         }
         auto *feature = OGRFeature::CreateFeature(layer->GetLayerDefn());
         feature->SetField("name", "sample");
         require(feature->SetGeometry(&line) == OGRERR_NONE &&
                    layer->CreateFeature(feature) == OGRERR_NONE,
                 "Cannot write source fixture");
         OGRFeature::DestroyFeature(feature);
      }
      GDALClose(source);
      const auto config_path = directory.filePath("scenarios.json");
      QFile config(config_path);
      require(config.open(QIODevice::WriteOnly), "Cannot create scenario config");
      const QByteArray json = R"({"version":1,"scenarios":[{"id":"sample","source_layer":"lines",
         "geometry":"line","bbox_wgs84":[8,49,10,51],"target_epsg":32632}]})";
      require(config.write(json) == json.size(), "Cannot write scenario config");
      config.close();
      const auto output = directory.filePath("generated");
      QProcess process;
      const QString program = QString::fromLocal8Bit(argv[1]);
      const QStringList arguments{config_path, directory.path(), output};
      process.start(program, arguments);
      require(process.waitForFinished(20000) && process.exitStatus() == QProcess::NormalExit &&
                 process.exitCode() == 0,
              "Scenario preparation failed");
      const auto result_path = QDir(output).filePath("sample.gpkg");
      gi::Importer importer(result_path.toStdString());
      require(importer.layers().size() == 1 && importer.layers().front().crs.epsg == 32632 &&
                 importer.layers().front().crs.units == "metre",
              "Projected CRS wrong");
      const auto features = importer.readLayer("sample");
      require(features.size() == 1, "Boundary touch was not excluded");
      core::Session session;
      session.loadPath(source_path);
      require(session.currentSpecs().front().max_value == 1.0, "Geographic range changed");
      session.loadPath(result_path);
      require(session.currentSpecs().front().min_value == 1.0 &&
                 session.currentSpecs().front().max_value == 10000.0 &&
                 session.currentParams().at("epsilon") == 100.0 && !session.hasGeneralization(),
              "Metric range or manual-start contract wrong");
      session.setAlgorithm("visvalingam-whyatt");
      require(session.currentSpecs().front().max_value == 1e8, "Metric area range wrong");
      session.loadPath(source_path);
      require(session.currentSpecs().front().min_value == 1e-6 &&
                 session.currentSpecs().front().max_value == 0.1,
              "Geographic range not restored");
      bool source_fid = false, preserved_name = false;
      for (const auto &[key, value] : features.front().attributes)
      {
         source_fid |= key == "source_fid" && value == "0";
         preserved_name |= key == "name" && value == "sample";
      }
      require(source_fid && preserved_name, "Provenance or attributes lost");
      auto line = std::get<gi::MultiLine>(features.front().geometry).lines.front();
      require(line.size() == 3 && std::abs(line[1].x - 500000) < 0.01 && line[1].y > 5e6 &&
                 line[1].y < 6e6,
              "Projection axis order or centre wrong");
      OGRSpatialReference utm;
      require(utm.importFromEPSG(32632) == OGRERR_NONE, "Cannot resolve UTM");
      utm.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
      auto *inverse = OGRCreateCoordinateTransformation(&utm, &wgs84);
      require(inverse, "Cannot create inverse projection");
      for (std::size_t i = 0; i < line.size(); ++i)
      {
         double x = line[i].x, y = line[i].y;
         require(inverse->Transform(1, &x, &y) && std::abs(x - (8.0 + i)) < 1e-7 &&
                    std::abs(y - 50.0) < 1e-7,
                 "Clip or inverse georeferencing wrong");
      }
      OCTDestroyCoordinateTransformation(inverse);
      const auto manifest =
         QJsonDocument::fromJson(readFile(QDir(output).filePath("sample.manifest.json"))).object();
      require(manifest["output_features"].toInt() == 1 &&
                 manifest["output_vertices"].toInt() == 3 &&
                 manifest["clipped_source_features"].toInt() == 1 &&
                 manifest["dropped_boundary_touches"].toInt() == 1,
              "Manifest counts wrong");
      const auto bytes = readFile(result_path);
      const auto hash = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
      require(manifest["output_sha256"].toString().toLatin1() == hash, "Output checksum wrong");
      process.start(program, arguments);
      require(process.waitForFinished(20000) && process.exitCode() != 0 &&
                 readFile(result_path) == bytes,
              "Existing output was overwritten");
      std::cout << "PASS: clipping, metre CRS, axis order, provenance, manifest, no overwrite\n";
      return 0;
   }
   catch (const std::exception &error)
   {
      std::cerr << error.what() << '\n';
      return 1;
   }
}
