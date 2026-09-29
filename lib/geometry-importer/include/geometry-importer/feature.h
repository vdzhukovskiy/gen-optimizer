#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "geometry-importer/geometry.h"

namespace gi
{
struct Feature
{
   int64_t id = -1;
   Geometry geometry;
   std::vector<std::pair<std::string, std::string>> attributes;
};
}