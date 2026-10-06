#include "generalization/algorithms/li_openshaw.h"

#include "generalization/detail/param_utils.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace gen
{
namespace
{

bool samePoint(const gi::Point &a, const gi::Point &b)
{
   return a.x == b.x && a.y == b.y;
}

void appendDistinct(gi::LineString &line, const gi::Point &point)
{
   if (line.empty() || !samePoint(line.back(), point))
      line.push_back(point);
}

// Ячейка непосредственно после начала сегмента. На границе учитываем направление.
long double startingCell(long double coordinate, long double direction)
{
   const long double cell = std::floor(coordinate);
   return coordinate == cell && direction < 0 ? cell - 1 : cell;
}

} // namespace

gi::LineString LiOpenshaw::simplifyLine(const gi::LineString &line, const ParamSet &params,
                                        const CancellationCheck &cancelled) const
{
   checkCancelled(cancelled);
   const double window = detail::requireParam(params, "window_size");
   if (!std::isfinite(window) || window <= 0)
      throw std::invalid_argument("Li-Openshaw window_size must be finite and positive");
   for (const auto &point : line)
   {
      checkCancelled(cancelled);
      if (!std::isfinite(point.x) || !std::isfinite(point.y))
         throw std::invalid_argument("Li-Openshaw requires finite coordinates");
   }
   // Одна точка или прямой двухточечный сегмент не требуют генерализации.
   if (line.size() <= 2)
      return line;

   // Регулярная квадратная сетка; начальная вершина находится в центре ячейки.
   // Работа в локальных координатах уменьшает погрешность при большом смещении CRS.
   const long double origin_x = line.front().x;
   const long double origin_y = line.front().y;
   const auto normalized = [window](long double coordinate, long double origin)
   {
      const long double value = (coordinate - origin) / window + 0.5L;
      if (!std::isfinite(value) || std::abs(value) > 0x1p52L)
         throw std::invalid_argument("Li-Openshaw grid is too fine for coordinate extent");
      return value;
   };

   gi::LineString out;
   out.reserve(line.size());
   out.push_back(line.front());
   bool active = false;
   bool first_visit = true;
   long double active_x = 0, active_y = 0;
   gi::Point entry{}, exit{};

   // Представитель каждого последовательного прохода — середина входа и выхода.
   // Повторный вход в уже посещённую ячейку создаёт отдельный проход:
   // глобальное объединение ячеек нарушило бы порядок линии.
   const auto flush = [&]
   {
      if (!first_visit)
         appendDistinct(out, {entry.x * 0.5 + exit.x * 0.5, entry.y * 0.5 + exit.y * 0.5});
      first_visit = false;
   };

   for (std::size_t i = 1; i < line.size(); ++i)
   {
      checkCancelled(cancelled);
      const auto &a = line[i - 1];
      const auto &b = line[i];
      if (samePoint(a, b))
         continue;
      const long double ax = normalized(a.x, origin_x), ay = normalized(a.y, origin_y);
      const long double bx = normalized(b.x, origin_x), by = normalized(b.y, origin_y);
      const long double dx = bx - ax, dy = by - ay;
      long double cell_x = startingCell(ax, dx), cell_y = startingCell(ay, dy);
      const int step_x = (dx > 0) - (dx < 0), step_y = (dy > 0) - (dy < 0);
      const auto pointAt = [&](long double t)
      {
         if (t == 0)
            return a;
         if (t == 1)
            return b;
         return gi::Point{static_cast<double>((1 - t) * a.x + t * b.x),
                          static_cast<double>((1 - t) * a.y + t * b.y)};
      };
      long double t = 0;
      while (t < 1)
      {
         checkCancelled(cancelled);
         const long double tx = step_x == 0 ? std::numeric_limits<long double>::infinity()
                                            : (cell_x + (step_x > 0 ? 1 : 0) - ax) / dx;
         const long double ty = step_y == 0 ? std::numeric_limits<long double>::infinity()
                                            : (cell_y + (step_y > 0 ? 1 : 0) - ay) / dy;
         const long double next = std::min({tx, ty, 1.0L});
         if (next <= t)
            throw std::runtime_error("Li-Openshaw grid traversal lost numeric precision");

         if (!active || cell_x != active_x || cell_y != active_y)
         {
            if (active)
               flush();
            active = true;
            active_x = cell_x;
            active_y = cell_y;
            entry = pointAt(t);
         }
         exit = pointAt(next);
         t = next;
         if (t == 1)
            break;

         // Одновременное пересечение двух границ — один переход через угол.
         const long double tolerance =
            32 * std::numeric_limits<long double>::epsilon() * std::max(1.0L, std::abs(t));
         if (tx <= next + tolerance)
            cell_x += step_x;
         if (ty <= next + tolerance)
            cell_y += step_y;
      }
   }
   // В первой и последней ячейках сохраняем концы вместо представительных точек.
   appendDistinct(out, line.back());
   return out;
}

ParamSpecs LiOpenshaw::paramSpecs() const
{
   return {{
      "window_size",
      "Square grid cell size (Li-Openshaw raster-vector), in CRS units",
      0.001,
      1.0,
      0.001,
      /*logarithmic=*/true,
      ParamDimension::Length,
   }};
}

} // namespace gen
