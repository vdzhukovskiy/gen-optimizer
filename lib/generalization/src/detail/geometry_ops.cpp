#include "generalization/detail/geometry_ops.h"

#include <cstddef>
#include <type_traits>

namespace gen::detail
{

namespace
{

gi::LinearRing simplifyRing(const gi::LinearRing &ring, const LineSimplifier &f,
                            const CancellationCheck &cancelled)
{
   // Кольцо замкнуто: front() == back(). Треугольник и меньше упрощать нечего.
   checkCancelled(cancelled);
   if (ring.size() < 4)
      return ring;

   // Разбиваем кольцо в самой удалённой от начала вершине, чтобы получить
   // два открытых отрезка и не иметь дело с вырожденным "first == last".
   const gi::Point &first = ring.front();
   std::size_t split = 0;
   double best = -1.0;
   for (std::size_t i = 1; i + 1 < ring.size(); ++i)
   {
      if ((i & 255) == 0)
         checkCancelled(cancelled);
      const double dx = ring[i].x - first.x;
      const double dy = ring[i].y - first.y;
      const double d2 = dx * dx + dy * dy;
      if (d2 > best)
      {
         best = d2;
         split = i;
      }
   }
   if (split == 0)
      return ring;

   gi::LineString a(ring.begin(), ring.begin() + static_cast<std::ptrdiff_t>(split) + 1);
   gi::LineString b(ring.begin() + static_cast<std::ptrdiff_t>(split), ring.end());

   gi::LineString a2 = f(a);
   gi::LineString b2 = f(b);

   gi::LinearRing out;
   out.reserve(a2.size() + b2.size());
   for (std::size_t i = 0; i < a2.size(); ++i)
   {
      if ((i & 255) == 0)
         checkCancelled(cancelled);
      out.push_back(a2[i]);
   }
   for (std::size_t i = 1; i < b2.size(); ++i)
   {
      if ((i & 255) == 0)
         checkCancelled(cancelled);
      out.push_back(b2[i]);
   }

   if (!out.empty() && (out.front().x != out.back().x || out.front().y != out.back().y))
      out.push_back(out.front());

   return out;
}

gi::Polygon simplifyPolygon(const gi::Polygon &poly, const LineSimplifier &f,
                            const CancellationCheck &cancelled)
{
   gi::Polygon out;
   out.reserve(poly.size());
   for (const auto &ring : poly)
      out.push_back(simplifyRing(ring, f, cancelled));
   return out;
}

} // namespace

gi::Geometry simplifyGeometry(const gi::Geometry &g, const LineSimplifier &f,
                              const CancellationCheck &cancelled)
{
   return std::visit(
      [&](const auto &v) -> gi::Geometry
      {
         checkCancelled(cancelled);
         using T = std::decay_t<decltype(v)>;
         if constexpr (std::is_same_v<T, gi::Point> || std::is_same_v<T, gi::MultiPoint>)
         {
            return v;
         }
         else if constexpr (std::is_same_v<T, gi::LineString>)
         {
            return f(v);
         }
         else if constexpr (std::is_same_v<T, gi::MultiLine>)
         {
            gi::MultiLine out;
            out.lines.reserve(v.lines.size());
            for (const auto &l : v.lines)
            {
               checkCancelled(cancelled);
               out.lines.push_back(f(l));
            }
            return out;
         }
         else if constexpr (std::is_same_v<T, gi::Polygon>)
         {
            return simplifyPolygon(v, f, cancelled);
         }
         else if constexpr (std::is_same_v<T, gi::MultiPolygon>)
         {
            gi::MultiPolygon out;
            out.polys.reserve(v.polys.size());
            for (const auto &p : v.polys)
            {
               checkCancelled(cancelled);
               out.polys.push_back(simplifyPolygon(p, f, cancelled));
            }
            return out;
         }
      },
      g);
}

} // namespace gen::detail