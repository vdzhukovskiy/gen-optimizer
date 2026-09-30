#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "geometry-importer/crs.h"
#include "geometry-importer/feature.h"

namespace gi
{

struct Bounds
{
   double minX = 0.0;
   double minY = 0.0;
   double maxX = 0.0;
   double maxY = 0.0;
};

struct LayerInfo
{
   std::string name;
   Bounds      bounds{};
   Crs         crs{};
   int64_t     feature_count = 0;   // -1, если GDAL не смог посчитать
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

   // Метаданные всех слоёв в порядке их следования в источнике.
   std::vector<LayerInfo> layers() const;

   // Читает все фичи слоя целиком в память.
   // Бросает std::runtime_error, если слоя нет или геометрия не поддерживается.
   // Для глобальных слоёв (Natural Earth) антимеридиан не обрабатывается —
   // геометрии, пересекающие ±180°, остаются как есть.
   std::vector<Feature> readLayer(std::string_view name) const;

private:
   struct Impl;
   std::unique_ptr<Impl> impl_;
};

} // namespace gi