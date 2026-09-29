#pragma once
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include "geometry-importer/feature.h"

namespace gi
{
struct Bounds
{
   double minX;
   double minY;
   double maxX;
   double maxY;
};

class Importer
{
public:
   explicit Importer(std::filesystem::path path);
   ~Importer();
   Importer(const Importer&) = delete;
   Importer& operator=(const Importer&) = delete;
   Importer(Importer&&) noexcept;
   Importer& operator=(Importer&&) noexcept;

   std::vector<std::string> layerNames() const;
   std::vector<Feature> readLayer(std::string_view name) const;
   Bounds layerBounds(std::string_view name) const;

private:
   struct Impl;
   std::unique_ptr<Impl> impl_;
};
}