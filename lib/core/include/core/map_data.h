#pragma once

#include <vector>

#include "geometry-importer/crs.h"
#include "geometry-importer/feature.h"
#include "geometry-importer/importer.h"

namespace core
{

struct Layer
{
   gi::LayerInfo            info;
   std::vector<gi::Feature> features;
};

struct MapData
{
   std::vector<Layer> layers;
   gi::Bounds         union_bounds{};
   gi::Crs            reference_crs{};
   bool               has_bounds = false;
};

} // namespace core