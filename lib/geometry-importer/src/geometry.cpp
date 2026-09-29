#include "geometry-importer/geometry.h"

#include <ogrsf_frmts.h>
#include <stdexcept>
#include <string>

namespace gi
{
namespace
{

Point toPoint(const OGRPoint &p)
{
   return Point{p.getX(), p.getY()};
}

LineString toLineString(const OGRLineString &ls)
{
   LineString out;
   out.reserve(static_cast<std::size_t>(ls.getNumPoints()));
   for (int i = 0; i < ls.getNumPoints(); ++i)
      out.push_back(Point{ls.getX(i), ls.getY(i)});
   return out;
}

LinearRing toRing(const OGRLinearRing &ring)
{
   LinearRing out;
   const int n = ring.getNumPoints();
   out.reserve(static_cast<std::size_t>(n));
   for (int i = 0; i < n; ++i)
      out.push_back(Point{ring.getX(i), ring.getY(i)});
   // Shapefile rings are already closed, but be defensive.
   if (!out.empty() && (out.front().x != out.back().x || out.front().y != out.back().y))
      out.push_back(out.front());
   return out;
}

Polygon toPolygon(const OGRPolygon &poly)
{
   Polygon out;
   if (const OGRLinearRing *ext = poly.getExteriorRing())
      out.push_back(toRing(*ext));
   const int n = poly.getNumInteriorRings();
   out.reserve(static_cast<std::size_t>(n) + 1);
   for (int i = 0; i < n; ++i)
      out.push_back(toRing(*poly.getInteriorRing(i)));
   return out;
}

MultiPoint toMultiPoint(const OGRMultiPoint &mp)
{
   MultiPoint out;
   out.points.reserve(static_cast<std::size_t>(mp.getNumGeometries()));
   for (int i = 0; i < mp.getNumGeometries(); ++i)
      out.points.push_back(toPoint(*mp.getGeometryRef(i)->toPoint()));
   return out;
}

MultiLine toMultiLine(const OGRMultiLineString &ml)
{
   MultiLine out;
   out.lines.reserve(static_cast<std::size_t>(ml.getNumGeometries()));
   for (int i = 0; i < ml.getNumGeometries(); ++i)
      out.lines.push_back(toLineString(*ml.getGeometryRef(i)->toLineString()));
   return out;
}

MultiPolygon toMultiPolygon(const OGRMultiPolygon &mp)
{
   MultiPolygon out;
   out.polys.reserve(static_cast<std::size_t>(mp.getNumGeometries()));
   for (int i = 0; i < mp.getNumGeometries(); ++i)
      out.polys.push_back(toPolygon(*mp.getGeometryRef(i)->toPolygon()));
   return out;
}

} // namespace

// Public entry point used by Importer.
Geometry translateGeometry(const OGRGeometry *g)
{
   if (!g)
      throw std::runtime_error("translateGeometry: null geometry");

   switch (wkbFlatten(g->getGeometryType()))
   {
   case wkbPoint:
      return toPoint(*g->toPoint());
   case wkbLineString:
      return toLineString(*g->toLineString());
   case wkbPolygon:
      return toPolygon(*g->toPolygon());
   case wkbMultiPoint:
      return toMultiPoint(*g->toMultiPoint());
   case wkbMultiLineString:
      return toMultiLine(*g->toMultiLineString());
   case wkbMultiPolygon:
      return toMultiPolygon(*g->toMultiPolygon());
   default:
   {
      std::string msg = "translateGeometry: unsupported type ";
      msg += OGRGeometryTypeToName(g->getGeometryType());
      throw std::runtime_error(msg);
   }
   }
}

} // namespace gi