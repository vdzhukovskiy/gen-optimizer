#pragma once
#include <cstdint>
#include <variant>
#include <vector>

namespace gi
{

struct Point
{
   double x = 0.0;
   double y = 0.0;
};

using LineString = std::vector<Point>;
using LinearRing = std::vector<Point>;   // closed: front() == back()
using Polygon = std::vector<LinearRing>; // [0] = exterior, [1..] = holes

struct MultiPoint
{
   std::vector<Point> points;
};
struct MultiLine
{
   std::vector<LineString> lines;
};
struct MultiPolygon
{
   std::vector<Polygon> polys;
};

using Geometry = std::variant<Point, LineString, Polygon, MultiPoint, MultiLine, MultiPolygon>;

} // namespace gi