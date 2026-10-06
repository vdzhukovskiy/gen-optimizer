#include "core/metrics.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <type_traits>

namespace core::metrics
{

namespace
{

// ─── Подсчёт вершин ──────────────────────────────────────────────────

struct VertexCounter
{
   std::size_t operator()(const gi::Point &) const
   {
      return 1;
   }

   std::size_t operator()(const gi::LineString &ls) const
   {
      return ls.size();
   }

   std::size_t operator()(const gi::Polygon &poly) const
   {
      std::size_t n = 0;
      for (const auto &r : poly)
         n += r.size();
      return n;
   }

   std::size_t operator()(const gi::MultiPoint &mp) const
   {
      return mp.points.size();
   }

   std::size_t operator()(const gi::MultiLine &ml) const
   {
      std::size_t n = 0;
      for (const auto &l : ml.lines)
         n += l.size();
      return n;
   }

   std::size_t operator()(const gi::MultiPolygon &mp) const
   {
      std::size_t n = 0;
      for (const auto &p : mp.polys)
         for (const auto &r : p)
            n += r.size();
      return n;
   }
};

// ─── Вырожденность ───────────────────────────────────────────────────

bool finitePoint(const gi::Point &point)
{
   return std::isfinite(point.x) && std::isfinite(point.y);
}

bool samePoint(const gi::Point &a, const gi::Point &b)
{
   return a.x == b.x && a.y == b.y;
}

bool lineDegenerate(const gi::LineString &line, const gen::CancellationCheck &cancelled)
{
   if (line.size() < 2)
      return true;
   bool distinct = false;
   for (std::size_t i = 0; i < line.size(); ++i)
   {
      if ((i & 255) == 0)
         gen::checkCancelled(cancelled);
      if (!finitePoint(line[i]))
         return true;
      distinct |= !samePoint(line.front(), line[i]);
   }
   return !distinct;
}

bool ringDegenerate(const gi::LinearRing &ring, const gen::CancellationCheck &cancelled)
{
   if (ring.size() < 4 || !samePoint(ring.front(), ring.back()))
      return true;
   // Перенос к первой вершине снижает потерю точности при большом смещении CRS.
   long double twice_area = 0;
   for (std::size_t i = 0; i < ring.size(); ++i)
   {
      if ((i & 255) == 0)
         gen::checkCancelled(cancelled);
      if (!finitePoint(ring[i]))
         return true;
      if (i > 0)
      {
         const long double ax = static_cast<long double>(ring[i - 1].x) - ring.front().x;
         const long double ay = static_cast<long double>(ring[i - 1].y) - ring.front().y;
         const long double bx = static_cast<long double>(ring[i].x) - ring.front().x;
         const long double by = static_cast<long double>(ring[i].y) - ring.front().y;
         twice_area += ax * by - bx * ay;
      }
   }
   return !std::isfinite(twice_area) || twice_area == 0;
}

struct DegenerateCheck
{
   const gen::CancellationCheck &cancelled;

   bool operator()(const gi::Point &point) const
   {
      return !finitePoint(point);
   }
   bool operator()(const gi::MultiPoint &points) const
   {
      if (points.points.empty())
         return true;
      for (const auto &point : points.points)
      {
         gen::checkCancelled(cancelled);
         if (!finitePoint(point))
            return true;
      }
      return false;
   }
   bool operator()(const gi::LineString &line) const
   {
      return lineDegenerate(line, cancelled);
   }
   bool operator()(const gi::Polygon &polygon) const
   {
      if (polygon.empty())
         return true;
      for (const auto &ring : polygon)
      {
         gen::checkCancelled(cancelled);
         if (ringDegenerate(ring, cancelled))
            return true;
      }
      return false;
   }
   bool operator()(const gi::MultiLine &lines) const
   {
      if (lines.lines.empty())
         return true;
      for (const auto &line : lines.lines)
      {
         gen::checkCancelled(cancelled);
         if (lineDegenerate(line, cancelled))
            return true;
      }
      return false;
   }
   bool operator()(const gi::MultiPolygon &polygons) const
   {
      if (polygons.polys.empty())
         return true;
      for (const auto &polygon : polygons.polys)
      {
         gen::checkCancelled(cancelled);
         if ((*this)(polygon))
            return true;
      }
      return false;
   }
};

// ─── Геометрия: расстояния ───────────────────────────────────────────

double pointSegmentDistSq(const gi::Point &p, const gi::Point &a, const gi::Point &b)
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
   double t = ((p.x - a.x) * dx + (p.y - a.y) * dy) / len2;
   t = std::clamp(t, 0.0, 1.0);
   const double qx = a.x + t * dx;
   const double qy = a.y + t * dy;
   const double px = p.x - qx;
   const double py = p.y - qy;
   return px * px + py * py;
}

double minDistToLineSq(const gi::Point &p, const gi::LineString &line,
                       const gen::CancellationCheck &cancelled)
{
   gen::checkCancelled(cancelled);
   if (line.empty())
      return std::numeric_limits<double>::infinity();
   if (line.size() == 1)
   {
      if (!finitePoint(p) || !finitePoint(line.front()))
         return std::numeric_limits<double>::infinity();
      const double dx = p.x - line[0].x;
      const double dy = p.y - line[0].y;
      return dx * dx + dy * dy;
   }
   double best = std::numeric_limits<double>::infinity();
   for (std::size_t i = 0; i + 1 < line.size(); ++i)
   {
      if ((i & 255) == 0)
         gen::checkCancelled(cancelled);
      if (!finitePoint(p) || !finitePoint(line[i]) || !finitePoint(line[i + 1]))
         return std::numeric_limits<double>::infinity();
      const double d2 = pointSegmentDistSq(p, line[i], line[i + 1]);
      if (d2 < best)
         best = d2;
   }
   return best;
}

// Одностороннее расстояние Хаусдорфа: max по вершинам a от min до b.
double directedHausdorff(const gi::LineString &a, const gi::LineString &b, std::size_t stride,
                         const gen::CancellationCheck &cancelled)
{
   gen::checkCancelled(cancelled);
   if (a.empty())
      return 0.0;
   if (b.empty())
      return std::numeric_limits<double>::infinity();
   if (stride == 0)
      stride = 1;

   double max_d2 = 0.0;
   for (std::size_t i = 0; i < a.size(); i += stride)
   {
      const double d2 = minDistToLineSq(a[i], b, cancelled);
      if (d2 > max_d2)
         max_d2 = d2;
   }
   // Всегда включить последнюю вершину, иначе хвост может быть не проверен.
   if ((a.size() - 1) % stride != 0)
   {
      const double d2 = minDistToLineSq(a.back(), b, cancelled);
      if (d2 > max_d2)
         max_d2 = d2;
   }
   return std::sqrt(max_d2);
}

double directedAverage(const gi::LineString &a, const gi::LineString &b, std::size_t stride,
                       const gen::CancellationCheck &cancelled)
{
   gen::checkCancelled(cancelled);
   if (a.empty())
      return 0.0;
   if (b.empty())
      return std::numeric_limits<double>::infinity();
   if (stride == 0)
      stride = 1;

   double sum = 0.0;
   std::size_t count = 0;
   for (std::size_t i = 0; i < a.size(); i += stride)
   {
      sum += std::sqrt(minDistToLineSq(a[i], b, cancelled));
      ++count;
   }
   if ((a.size() - 1) % stride != 0)
   {
      sum += std::sqrt(minDistToLineSq(a.back(), b, cancelled));
      ++count;
   }
   return count > 0 ? sum / static_cast<double>(count) : 0.0;
}

// ─── Обход варианта для сравнения фич ────────────────────────────────

// Фичи и компоненты соответствуют по индексу. Среднее — по сравниваемым
// линиям/кольцам/точкам с одинаковым весом компонентов (не по длине геометрии).
struct HausdorffVisitor
{
   const gi::Geometry &other;
   std::size_t stride;
   const gen::CancellationCheck &cancelled;
   double &max_out;
   double &avg_acc;
   std::size_t &avg_count;
   bool valid = true;

   void fail()
   {
      valid = false;
   }

   void compareLine(const gi::LineString &a, const gi::LineString &b)
   {
      gen::checkCancelled(cancelled);
      if (!valid)
         return;
      const double h = hausdorff(a, b, stride, cancelled);
      const double average = averageDeviation(a, b, stride, cancelled);
      if (!std::isfinite(h) || !std::isfinite(average))
      {
         fail();
         return;
      }
      max_out = std::max(max_out, h);
      avg_acc += average;
      ++avg_count;
   }

   void comparePoint(const gi::Point &a, const gi::Point &b)
   {
      gen::checkCancelled(cancelled);
      if (!valid)
         return;
      const double distance = std::hypot(a.x - b.x, a.y - b.y);
      if (!std::isfinite(distance))
      {
         fail();
         return;
      }
      max_out = std::max(max_out, distance);
      avg_acc += distance;
      ++avg_count;
   }

   void comparePolygon(const gi::Polygon &a, const gi::Polygon &b)
   {
      if (a.size() != b.size())
      {
         fail();
         return;
      }
      for (std::size_t i = 0; i < a.size(); ++i)
         compareLine(a[i], b[i]);
   }

   void operator()(const gi::Point &a)
   {
      comparePoint(a, std::get<gi::Point>(other));
   }
   void operator()(const gi::MultiPoint &a)
   {
      const auto &b = std::get<gi::MultiPoint>(other);
      if (a.points.size() != b.points.size())
      {
         fail();
         return;
      }
      for (std::size_t i = 0; i < a.points.size(); ++i)
         comparePoint(a.points[i], b.points[i]);
   }
   void operator()(const gi::LineString &a)
   {
      compareLine(a, std::get<gi::LineString>(other));
   }
   void operator()(const gi::MultiLine &a)
   {
      const auto &b = std::get<gi::MultiLine>(other);
      if (a.lines.size() != b.lines.size())
      {
         fail();
         return;
      }
      for (std::size_t i = 0; i < a.lines.size(); ++i)
         compareLine(a.lines[i], b.lines[i]);
   }
   void operator()(const gi::Polygon &a)
   {
      comparePolygon(a, std::get<gi::Polygon>(other));
   }
   void operator()(const gi::MultiPolygon &a)
   {
      const auto &b = std::get<gi::MultiPolygon>(other);
      if (a.polys.size() != b.polys.size())
      {
         fail();
         return;
      }
      for (std::size_t i = 0; i < a.polys.size(); ++i)
         comparePolygon(a.polys[i], b.polys[i]);
   }
};

} // namespace

// ─── Публичные функции ───────────────────────────────────────────────

std::size_t countVertices(const gi::Geometry &g)
{
   return std::visit(VertexCounter{}, g);
}

std::size_t countVertices(const std::vector<gi::Feature> &features)
{
   std::size_t n = 0;
   for (const auto &f : features)
      n += countVertices(f.geometry);
   return n;
}

bool isDegenerate(const gi::Geometry &g, const gen::CancellationCheck &cancelled)
{
   gen::checkCancelled(cancelled);
   return std::visit(DegenerateCheck{cancelled}, g);
}

double hausdorff(const gi::LineString &a, const gi::LineString &b, std::size_t stride,
                 const gen::CancellationCheck &cancelled)
{
   const double ab = directedHausdorff(a, b, stride, cancelled);
   const double ba = directedHausdorff(b, a, stride, cancelled);
   return std::max(ab, ba);
}

double averageDeviation(const gi::LineString &a, const gi::LineString &b, std::size_t stride,
                        const gen::CancellationCheck &cancelled)
{
   const double ab = directedAverage(a, b, stride, cancelled);
   const double ba = directedAverage(b, a, stride, cancelled);
   return 0.5 * (ab + ba);
}

SimplificationMetrics evaluate(const std::vector<gi::Feature> &original,
                               const std::vector<gi::Feature> &simplified, double simplification_ms,
                               std::size_t stride, const gen::CancellationCheck &cancelled,
                               const std::function<void(std::size_t)> &progress)
{
   SimplificationMetrics m;
   stride = std::max<std::size_t>(1, stride);
   m.sample_stride = stride;
   for (const auto &feature : original)
   {
      gen::checkCancelled(cancelled);
      m.orig_vertices += countVertices(feature.geometry);
      if (isDegenerate(feature.geometry, cancelled))
         ++m.orig_degenerate_features;
   }
   for (const auto &feature : simplified)
   {
      gen::checkCancelled(cancelled);
      m.result_vertices += countVertices(feature.geometry);
   }
   m.orig_features = original.size();
   m.simplification_ms = simplification_ms;

   for (const auto &f : simplified)
   {
      gen::checkCancelled(cancelled);
      if (!isDegenerate(f.geometry, cancelled))
         ++m.surviving_features;
      else
         ++m.degenerate_features;
   }

   m.compression_ratio = (m.orig_vertices > 0) ? static_cast<double>(m.result_vertices) /
                                                    static_cast<double>(m.orig_vertices)
                                               : 1.0;

   double max_h = 0.0;
   double avg_sum = 0.0;
   std::size_t avg_count = 0;

   const std::size_t n = std::max(original.size(), simplified.size());
   for (std::size_t i = 0; i < n; ++i)
   {
      gen::checkCancelled(cancelled);
      if (i >= original.size() || i >= simplified.size() ||
          original[i].geometry.index() != simplified[i].geometry.index() ||
          isDegenerate(original[i].geometry, cancelled) ||
          isDegenerate(simplified[i].geometry, cancelled))
      {
         ++m.invalid_comparisons;
      }
      else
      {
         // Сначала оцениваем фичу отдельно: если позднее кольцо несопоставимо,
         // уже вычисленные расстояния этой фичи не попадут в общую оценку.
         double feature_h = 0.0, feature_average_sum = 0.0;
         std::size_t feature_components = 0;
         HausdorffVisitor visitor{
            simplified[i].geometry, stride, cancelled, feature_h, feature_average_sum,
            feature_components};
         std::visit(visitor, original[i].geometry);
         if (!visitor.valid || feature_components == 0)
            ++m.invalid_comparisons;
         else
         {
            max_h = std::max(max_h, feature_h);
            avg_sum += feature_average_sum;
            avg_count += feature_components;
            ++m.evaluated_features;
         }
      }
      if (progress)
         progress(i + 1);
   }

   if (m.evaluated_features > 0)
   {
      m.hausdorff = max_h;
      m.average_deviation = avg_sum / static_cast<double>(avg_count);
   }
   return m;
}

} // namespace core::metrics