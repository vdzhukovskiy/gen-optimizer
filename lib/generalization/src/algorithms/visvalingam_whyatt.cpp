#include "generalization/algorithms/visvalingam_whyatt.h"

#include "generalization/detail/param_utils.h"

#include <cmath>
#include <cstddef>
#include <limits>
#include <queue>
#include <vector>

namespace gen
{

namespace
{

struct HeapEntry
{
   double      area;
   std::size_t index;
};

struct MinArea
{
   bool operator()(const HeapEntry& a, const HeapEntry& b) const noexcept
   {
      return a.area > b.area;
   }
};

double triangleArea(const gi::Point& a, const gi::Point& b, const gi::Point& c)
{
   return std::abs((b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y)) * 0.5;
}

} // namespace

gi::LineString VisvalingamWhyatt::simplifyLine(const gi::LineString &line, const ParamSet &params,
                                               const CancellationCheck &cancelled) const
{
   checkCancelled(cancelled);
   const double area_thresh = detail::requireParam(params, "area_threshold");
   const std::size_t n = line.size();
   if (n <= 2)
      return line;

   std::vector<bool>        removed(n, false);
   std::vector<double>      area(n, 0.0);
   std::vector<std::size_t> prev(n), next(n);
   for (std::size_t i = 0; i < n; ++i)
   {
      if ((i & 255) == 0)
         checkCancelled(cancelled);
      prev[i] = i - 1;
      next[i] = i + 1;
   }
   prev[0] = 0;
   next[n - 1] = n - 1;   // sentinels: endpoints never removed

   auto computeArea = [&](std::size_t i) -> double {
      if (i == 0 || i + 1 == n)
         return std::numeric_limits<double>::infinity();
      return triangleArea(line[prev[i]], line[i], line[next[i]]);
   };

   std::priority_queue<HeapEntry, std::vector<HeapEntry>, MinArea> pq;
   for (std::size_t i = 1; i + 1 < n; ++i)
   {
      if ((i & 255) == 0)
         checkCancelled(cancelled);
      area[i] = computeArea(i);
      pq.push({area[i], i});
   }

   while (!pq.empty())
   {
      checkCancelled(cancelled);
      const HeapEntry top = pq.top();
      pq.pop();

      if (removed[top.index])
         continue;
      if (top.area != area[top.index])
         continue;   // устаревшая запись
      if (top.area >= area_thresh)
         break;

      removed[top.index] = true;
      const std::size_t p = prev[top.index];
      const std::size_t q = next[top.index];
      next[p] = q;
      prev[q] = p;

      if (p > 0 && p + 1 < n)
      {
         area[p] = computeArea(p);
         pq.push({area[p], p});
      }
      if (q > 0 && q + 1 < n)
      {
         area[q] = computeArea(q);
         pq.push({area[q], q});
      }
   }

   gi::LineString out;
   out.reserve(n);
   for (std::size_t i = 0; i < n; ++i)
   {
      if ((i & 255) == 0)
         checkCancelled(cancelled);
      if (!removed[i]) out.push_back(line[i]);
   }
   return out;
}

ParamSpecs VisvalingamWhyatt::paramSpecs() const
{
   return {{
       "area_threshold",
       "Minimum effective triangle area, in CRS units squared",
       1e-6, 1e-1, 1e-6,
       /*logarithmic=*/true,
   }};
}

} // namespace gen