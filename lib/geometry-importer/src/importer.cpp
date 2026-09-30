#include "geometry-importer/importer.h"

#include <gdal_priv.h>
#include <ogrsf_frmts.h>
#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <string>

// объявление из geometry.cpp
namespace gi { Geometry translateGeometry(const OGRGeometry* g); }

namespace gi
{

namespace
{

// RAII для OGRFeature — защищает от утечки при исключении в translateGeometry.
struct FeatureDeleter
{
   void operator()(OGRFeature* f) const noexcept
   {
      if (f) OGRFeature::DestroyFeature(f);
   }
};
using UniqueFeature = std::unique_ptr<OGRFeature, FeatureDeleter>;

Crs extractCrs(const OGRSpatialReference* srs)
{
   Crs out;
   if (!srs)
      return out;

   if (const char* n = srs->GetName())
      out.name = n;

   char* wkt_raw = nullptr;
   if (srs->exportToWkt(&wkt_raw) == OGRERR_NONE && wkt_raw)
   {
      out.wkt = wkt_raw;
      CPLFree(wkt_raw);
   }

   const char* auth_name = srs->GetAuthorityName(nullptr);
   const char* auth_code = srs->GetAuthorityCode(nullptr);
   if (auth_name && auth_code && std::string(auth_name) == "EPSG")
      out.epsg = std::atoi(auth_code);

   out.is_geographic = srs->IsGeographic() != 0;
   if (out.is_geographic)
   {
      out.units = "degree";
   }
   else
   {
      const char* unit = nullptr;
      if (srs->GetLinearUnits(&unit) && unit)
         out.units = unit;
   }

   return out;
}

Bounds extractBounds(OGRLayer* layer)
{
   Bounds b{};
   OGREnvelope e{};
   if (layer->GetExtent(&e) == OGRERR_NONE)
      b = { e.MinX, e.MinY, e.MaxX, e.MaxY };
   return b;
}

} // namespace

struct Importer::Impl
{
   GDALDataset* ds = nullptr;
   ~Impl() { if (ds) GDALClose(ds); }
};

Importer::Importer(std::filesystem::path path)
   : impl_(std::make_unique<Impl>())
{
   GDALAllRegister();
   impl_->ds = static_cast<GDALDataset*>(
       GDALOpenEx(path.c_str(), GDAL_OF_VECTOR | GDAL_OF_READONLY,
                  nullptr, nullptr, nullptr));
   if (!impl_->ds)
      throw std::runtime_error("Importer: cannot open " + path.string());
}

Importer::~Importer() = default;
Importer::Importer(Importer&&) noexcept = default;
Importer& Importer::operator=(Importer&&) noexcept = default;

std::vector<LayerInfo> Importer::layers() const
{
   std::vector<LayerInfo> out;
   const int n = impl_->ds->GetLayerCount();
   out.reserve(static_cast<std::size_t>(n));

   for (int i = 0; i < n; ++i)
   {
      OGRLayer* layer = impl_->ds->GetLayer(i);
      if (!layer)
         continue;

      LayerInfo info;
      info.name = layer->GetName();
      info.bounds = extractBounds(layer);
      info.crs = extractCrs(layer->GetSpatialRef());
      info.feature_count = static_cast<int64_t>(layer->GetFeatureCount());
      out.push_back(std::move(info));
   }

   return out;
}

std::vector<Feature> Importer::readLayer(std::string_view name) const
{
   OGRLayer* layer = impl_->ds->GetLayerByName(std::string(name).c_str());
   if (!layer)
      throw std::runtime_error("Importer: no such layer: " + std::string(name));

   const int count = layer->GetFeatureCount();
   std::vector<Feature> out;
   if (count > 0)
      out.reserve(static_cast<std::size_t>(count));

   layer->ResetReading();

   while (true)
   {
      UniqueFeature raw(layer->GetNextFeature());
      if (!raw)
         break;

      Feature f;
      f.id = static_cast<int64_t>(raw->GetFID());

      if (OGRGeometry* g = raw->GetGeometryRef())
         f.geometry = translateGeometry(g);

      const auto* defn = raw->GetDefnRef();
      const int nf = defn->GetFieldCount();
      f.attributes.reserve(static_cast<std::size_t>(nf));
      for (int i = 0; i < nf; ++i)
      {
         const char* key = defn->GetFieldDefn(i)->GetNameRef();
         const char* val = raw->GetFieldAsString(i);
         if (val && *val)
            f.attributes.emplace_back(key, val);
      }

      out.push_back(std::move(f));
   }

   return out;
}

} // namespace gi