#include "generalization/algorithms/douglas_peucker.h"

#include "generalization/detail/param_utils.h"

#include <cstddef>
#include <vector>

namespace gen
{

namespace
{

double perpDistanceSq(const gi::Point& p, const gi::Point& a, const gi::Point& b)
{
   const double dx = b.x - a.x;
   const double dy = b.y - a.y;
   const double len2 = dx * dx + dy * dy;
   if (len2 <= 0.0)
   {
      const double px = p.x - a.x;
      const double py = p.y - a.y;
      return px * px + py * py;
   }
   const double cross = (p.x - a.x) * dy - (p.y - a.y) * dx;
   return (cross * cross) / len2;
}

void recurse(const gi::LineString& pts, std::size_t first, std::size_t last,
             double eps2, std::vector<bool>& keep)
{
   if (last <= first + 1)
      return;

   double max_d2 = -1.0;
   std::size_t max_i = first;
   for (std::size_t i = first + 1; i < last; ++i)
   {
      const double d2 = perpDistanceSq(pts[i], pts[first], pts[last]);
      if (d2 > max_d2) { max_d2 = d2; max_i = i; }
   }
   if (max_d2 > eps2)
   {
      keep[max_i] = true;
      recurse(pts, first, max_i, eps2, keep);
      recurse(pts, max_i, last, eps2, keep);
   }
}

} // namespace

gi::LineString DouglasPeucker::simplifyLine(const gi::LineString& line,
                                            const ParamSet& params) const
{
   const double eps = detail::requireParam(params, "epsilon");
   if (line.size() <= 2 || eps <= 0.0)
      return line;

   std::vector<bool> keep(line.size(), false);
   keep.front() = true;
   keep.back()  = true;
   recurse(line, 0, line.size() - 1, eps * eps, keep);

   gi::LineString out;
   out.reserve(line.size());
   for (std::size_t i = 0; i < line.size(); ++i)
      if (keep[i]) out.push_back(line[i]);
   return out;
}

ParamSpecs DouglasPeucker::paramSpecs() const
{
   return {{
       "epsilon",
       "Maximum perpendicular deviation, in CRS units",
       0.001, 1.0, 0.001,
       /*logarithmic=*/true,
   }};
}

} // namespace gen