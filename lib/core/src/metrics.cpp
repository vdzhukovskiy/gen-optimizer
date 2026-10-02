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
   std::size_t operator()(const gi::Point&) const { return 1; }

   std::size_t operator()(const gi::LineString& ls) const { return ls.size(); }

   std::size_t operator()(const gi::Polygon& poly) const
   {
      std::size_t n = 0;
      for (const auto& r : poly) n += r.size();
      return n;
   }

   std::size_t operator()(const gi::MultiPoint& mp) const { return mp.points.size(); }

   std::size_t operator()(const gi::MultiLine& ml) const
   {
      std::size_t n = 0;
      for (const auto& l : ml.lines) n += l.size();
      return n;
   }

   std::size_t operator()(const gi::MultiPolygon& mp) const
   {
      std::size_t n = 0;
      for (const auto& p : mp.polys)
         for (const auto& r : p) n += r.size();
      return n;
   }
};

// ─── Вырожденность ───────────────────────────────────────────────────

bool ringDegenerate(const gi::LinearRing& r)
{
   // Кольцо замкнуто: front() == back(). Валидное кольцо = >= 4 точек.
   return r.size() < 4;
}

bool lineDegenerate(const gi::LineString& l)
{
   return l.size() < 2;
}

struct DegenerateCheck
{
   bool operator()(const gi::Point&) const { return false; }
   bool operator()(const gi::MultiPoint&) const { return false; }

   bool operator()(const gi::LineString& l) const { return lineDegenerate(l); }

   bool operator()(const gi::Polygon& p) const
   {
      if (p.empty()) return true;
      if (ringDegenerate(p.front())) return true;
      return false;
   }

   bool operator()(const gi::MultiLine& ml) const
   {
      if (ml.lines.empty()) return true;
      for (const auto& l : ml.lines)
         if (!lineDegenerate(l)) return false;
      return true;
   }

   bool operator()(const gi::MultiPolygon& mp) const
   {
      if (mp.polys.empty()) return true;
      for (const auto& p : mp.polys)
         if (!p.empty() && !ringDegenerate(p.front())) return false;
      return true;
   }
};

// ─── Геометрия: расстояния ───────────────────────────────────────────

double pointSegmentDistSq(const gi::Point& p, const gi::Point& a, const gi::Point& b)
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

double minDistToLineSq(const gi::Point& p, const gi::LineString& line)
{
   if (line.empty())
      return std::numeric_limits<double>::infinity();
   if (line.size() == 1)
   {
      const double dx = p.x - line[0].x;
      const double dy = p.y - line[0].y;
      return dx * dx + dy * dy;
   }
   double best = std::numeric_limits<double>::infinity();
   for (std::size_t i = 0; i + 1 < line.size(); ++i)
   {
      const double d2 = pointSegmentDistSq(p, line[i], line[i + 1]);
      if (d2 < best) best = d2;
   }
   return best;
}

// Одностороннее расстояние Хаусдорфа: max по вершинам a от min до b.
double directedHausdorff(const gi::LineString& a, const gi::LineString& b,
                        std::size_t stride)
{
   if (a.empty() || b.empty())
      return 0.0;
   if (stride == 0) stride = 1;

   double max_d2 = 0.0;
   for (std::size_t i = 0; i < a.size(); i += stride)
   {
      const double d2 = minDistToLineSq(a[i], b);
      if (d2 > max_d2) max_d2 = d2;
   }
   // Всегда включить последнюю вершину, иначе хвост может быть не проверен.
   if ((a.size() - 1) % stride != 0)
   {
      const double d2 = minDistToLineSq(a.back(), b);
      if (d2 > max_d2) max_d2 = d2;
   }
   return std::sqrt(max_d2);
}

double directedAverage(const gi::LineString& a, const gi::LineString& b,
                       std::size_t stride)
{
   if (a.empty() || b.empty())
      return 0.0;
   if (stride == 0) stride = 1;

   double sum = 0.0;
   std::size_t count = 0;
   for (std::size_t i = 0; i < a.size(); i += stride)
   {
      sum += std::sqrt(minDistToLineSq(a[i], b));
      ++count;
   }
   return count > 0 ? sum / static_cast<double>(count) : 0.0;
}

// ─── Обход варианта для сравнения фич ────────────────────────────────

// Для линии → (line, line). Для полигона — по каждому кольцу отдельно,
// берём максимум. Для точек/Multi* — считаем нулевое отклонение.
struct HausdorffVisitor
{
   const gi::Geometry& other;
   std::size_t         stride;
   double&             max_out;
   double&             avg_acc;
   std::size_t&        avg_count;

   void operator()(const gi::Point&) const {}
   void operator()(const gi::MultiPoint&) const {}
   void operator()(const gi::MultiLine&) const {}

   void operator()(const gi::LineString& a) const
   {
      const auto* b = std::get_if<gi::LineString>(&other);
      if (!b) return;
      const double h = hausdorff(a, *b, stride);
      if (h > max_out) max_out = h;
      avg_acc += averageDeviation(a, *b, stride);
      ++avg_count;
   }

   void operator()(const gi::Polygon& a) const
   {
      const auto* b = std::get_if<gi::Polygon>(&other);
      if (!b) return;
      const std::size_t n = std::min(a.size(), b->size());
      for (std::size_t i = 0; i < n; ++i)
      {
         const double h = hausdorff(a[i], (*b)[i], stride);
         if (h > max_out) max_out = h;
         avg_acc += averageDeviation(a[i], (*b)[i], stride);
         ++avg_count;
      }
   }

   void operator()(const gi::MultiPolygon& a) const
   {
      const auto* b = std::get_if<gi::MultiPolygon>(&other);
      if (!b) return;
      const std::size_t np = std::min(a.polys.size(), b->polys.size());
      for (std::size_t pi = 0; pi < np; ++pi)
      {
         const auto& pa = a.polys[pi];
         const auto& pb = b->polys[pi];
         const std::size_t nr = std::min(pa.size(), pb.size());
         for (std::size_t ri = 0; ri < nr; ++ri)
         {
            const double h = hausdorff(pa[ri], pb[ri], stride);
            if (h > max_out) max_out = h;
            avg_acc += averageDeviation(pa[ri], pb[ri], stride);
            ++avg_count;
         }
      }
   }
};

} // namespace

// ─── Публичные функции ───────────────────────────────────────────────

std::size_t countVertices(const gi::Geometry& g)
{
   return std::visit(VertexCounter{}, g);
}

std::size_t countVertices(const std::vector<gi::Feature>& features)
{
   std::size_t n = 0;
   for (const auto& f : features)
      n += countVertices(f.geometry);
   return n;
}

bool isDegenerate(const gi::Geometry& g)
{
   return std::visit(DegenerateCheck{}, g);
}

double hausdorff(const gi::LineString& a, const gi::LineString& b,
                 std::size_t stride)
{
   const double ab = directedHausdorff(a, b, stride);
   const double ba = directedHausdorff(b, a, stride);
   return std::max(ab, ba);
}

double averageDeviation(const gi::LineString& a, const gi::LineString& b,
                        std::size_t stride)
{
   const double ab = directedAverage(a, b, stride);
   const double ba = directedAverage(b, a, stride);
   return 0.5 * (ab + ba);
}

SimplificationMetrics evaluate(const std::vector<gi::Feature>& original,
                               const std::vector<gi::Feature>& simplified,
                               double elapsed_ms,
                               std::size_t stride)
{
   SimplificationMetrics m;
   m.orig_vertices   = countVertices(original);
   m.result_vertices = countVertices(simplified);
   m.orig_features   = original.size();
   m.elapsed_ms      = elapsed_ms;

   for (const auto& f : simplified)
      if (!isDegenerate(f.geometry))
         ++m.surviving_features;

   m.compression_ratio = (m.orig_vertices > 0)
                             ? static_cast<double>(m.result_vertices) /
                                   static_cast<double>(m.orig_vertices)
                             : 1.0;

   double      max_h     = 0.0;
   double      avg_sum   = 0.0;
   std::size_t avg_count = 0;

   const std::size_t n = std::min(original.size(), simplified.size());
   for (std::size_t i = 0; i < n; ++i)
   {
      HausdorffVisitor v{simplified[i].geometry, stride, max_h, avg_sum, avg_count};
      std::visit(v, original[i].geometry);
   }

   m.hausdorff         = max_h;
   m.average_deviation = (avg_count > 0)
                             ? avg_sum / static_cast<double>(avg_count)
                             : 0.0;
   return m;
}

} // namespace core::metrics