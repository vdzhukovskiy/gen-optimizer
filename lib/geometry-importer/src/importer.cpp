#include "geometry-importer/importer.h"

#include <gdal_priv.h>
#include <ogrsf_frmts.h>
#include <stdexcept>
#include <string>

// объявление из geometry.cpp
namespace gi { Geometry translateGeometry(const OGRGeometry* g); }

namespace gi
{

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

std::vector<std::string> Importer::layerNames() const
{
   std::vector<std::string> out;
   const int n = impl_->ds->GetLayerCount();
   out.reserve(static_cast<std::size_t>(n));
   for (int i = 0; i < n; ++i)
      out.emplace_back(impl_->ds->GetLayer(i)->GetName());
   return out;
}

std::vector<Feature> Importer::readLayer(std::string_view name) const
{
   OGRLayer* layer = impl_->ds->GetLayerByName(std::string(name).c_str());
   if (!layer)
      throw std::runtime_error("Importer: no such layer");

   std::vector<Feature> out;
   layer->ResetReading();
   while (OGRFeature* raw = layer->GetNextFeature())
   {
      Feature f;
      f.id = raw->GetFID();
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
      OGRFeature::DestroyFeature(raw);
   }
   return out;
}

Bounds Importer::layerBounds(std::string_view name) const
{
   OGRLayer* layer = impl_->ds->GetLayerByName(std::string(name).c_str());
   if (!layer)
      throw std::runtime_error("Importer: no such layer");
   OGREnvelope e{};
   if (layer->GetExtent(&e) != OGRERR_NONE)
      throw std::runtime_error("Importer: cannot get extent");
   return { e.MinX, e.MinY, e.MaxX, e.MaxY };
}

} // namespace gi