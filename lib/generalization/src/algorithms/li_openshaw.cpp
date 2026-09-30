#include "generalization/algorithms/li_openshaw.h"

#include "generalization/detail/param_utils.h"

#include <cstddef>

namespace gen
{

gi::LineString LiOpenshaw::simplifyLine(const gi::LineString& line,
                                        const ParamSet& params) const
{
   const double window = detail::requireParam(params, "window_size");
   if (line.size() <= 2 || window <= 0.0)
      return line;

   const double w2 = window * window;

   gi::LineString out;
   out.reserve(line.size());
   out.push_back(line.front());

   for (std::size_t i = 1; i + 1 < line.size(); ++i)
   {
      const gi::Point& last = out.back();
      const double dx = line[i].x - last.x;
      const double dy = line[i].y - last.y;
      if (dx * dx + dy * dy >= w2)
         out.push_back(line[i]);
   }

   if (out.back().x != line.back().x || out.back().y != line.back().y)
      out.push_back(line.back());

   return out;
}

ParamSpecs LiOpenshaw::paramSpecs() const
{
   return {{
       "window_size",
       "Minimum spacing between consecutive retained vertices, in CRS units",
       0.001, 1.0, 0.001,
       /*logarithmic=*/true,
   }};
}

} // namespace gen