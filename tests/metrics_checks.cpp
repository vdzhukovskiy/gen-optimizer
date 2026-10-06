#include "core/metrics.h"
#include "generalization/algorithm.h"
#include "geometry-importer/importer.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace
{
void require(bool condition, const char *message)
{
   if (!condition)
      throw std::runtime_error(message);
}
void close(double actual, double expected)
{
   require(std::isfinite(actual) && std::abs(actual - expected) < 1e-10,
           "Unexpected numerical metric");
}
core::metrics::SimplificationMetrics evaluate(const gi::Geometry &a, const gi::Geometry &b)
{
   return core::metrics::evaluate({gi::Feature{1, a, {}}}, {gi::Feature{1, b, {}}}, 12.5, 1);
}
void invalid(const gi::Geometry &a, const gi::Geometry &b)
{
   const auto result = evaluate(a, b);
   require(std::isnan(result.hausdorff) && std::isnan(result.average_deviation) &&
              result.evaluated_features == 0 && result.invalid_comparisons == 1,
           "Excluded geometry reported a distance without valid comparisons");
}
void checkKnownGeometries()
{
   const gi::LineString line{{0, 0}, {10, 0}};
   const gi::LineString shifted{{0, 2}, {10, 2}};
   auto m = evaluate(line, shifted);
   close(m.hausdorff, 2);
   close(m.average_deviation, 2);
   close(m.elapsed_ms, 12.5);
   require(m.orig_vertices == 2 && m.result_vertices == 2 && m.surviving_features == 1 &&
              m.degenerate_features == 0 && m.invalid_comparisons == 0,
           "Wrong counters");
   close(evaluate(line, line).hausdorff, 0);
   close(evaluate(gi::Point{0, 0}, gi::Point{3, 4}).hausdorff, 5);

   const gi::MultiLine original{{line, {{20, 0}, {30, 0}}}};
   const gi::MultiLine result{{shifted, {{20, 4}, {30, 4}}}};
   m = evaluate(original, result);
   close(m.hausdorff, 4);
   close(m.average_deviation, 3);
   close(evaluate(original, original).average_deviation, 0);
   invalid(original, gi::MultiLine{{shifted}});
   invalid(gi::MultiLine{{line}}, gi::MultiLine{{line, shifted}});
   invalid(original, gi::MultiLine{{line, {}}});

   const gi::Polygon polygon{{{0, 0}, {10, 0}, {10, 10}, {0, 10}, {0, 0}},
                             {{2, 2}, {3, 2}, {3, 3}, {2, 3}, {2, 2}}};
   close(evaluate(polygon, polygon).hausdorff, 0);
   invalid(polygon, gi::Polygon{polygon.front()}); // Потерянное отверстие.
   invalid(gi::Polygon{polygon.front()}, polygon); // Добавленное отверстие.
   invalid(gi::MultiPolygon{{polygon, polygon}}, gi::MultiPolygon{{polygon}});
   close(evaluate(gi::MultiPolygon{{polygon}}, gi::MultiPolygon{{polygon}}).hausdorff, 0);

   invalid(line, gi::LineString{});
   invalid(line, gi::LineString{{0, 0}});
   invalid(line, gi::LineString{{0, 0}, {0, 0}});
   invalid(gi::LineString{}, line);
   invalid(line, gi::Point{0, 0});
   invalid(polygon, gi::Polygon{});
   invalid(polygon, gi::Polygon{{{0, 0}, {1, 0}, {2, 0}, {0, 0}}});
   invalid(polygon, gi::Polygon{{{0, 0}, {1, 0}, {1, 1}, {0, 1}}});
   invalid(polygon, gi::Polygon{polygon.front(), {{2, 2}, {2, 2}, {2, 2}, {2, 2}}});
   invalid(gi::MultiPoint{{{0, 0}}}, gi::MultiPoint{});
   invalid(gi::Point{0, 0}, gi::Point{std::numeric_limits<double>::quiet_NaN(), 0});
   require(core::metrics::isDegenerate(gi::MultiLine{{line, {{0, 0}}}}),
           "Partially degenerate feature counted as surviving");
   m = evaluate(line, gi::LineString{});
   require(m.surviving_features == 0 && m.degenerate_features == 1, "Missing degeneracy counter");

   close(core::metrics::hausdorff({}, {}), 0);
   close(core::metrics::averageDeviation({}, {}), 0);
   require(std::isinf(core::metrics::hausdorff(line, {})) &&
              std::isinf(core::metrics::averageDeviation({}, line)),
           "Empty distance returned zero");
   close(core::metrics::hausdorff(line, {{0, 0}}), 10);
   close(core::metrics::averageDeviation(line, {{0, 0}, {5, 0}}, 8), 1.25);
   close(core::metrics::averageDeviation(line, shifted, 0), 2);

   const std::vector<gi::Feature> feature{{1, line, {}}};
   for (const auto &pair : {std::pair{feature, std::vector<gi::Feature>{}},
                            std::pair{std::vector<gi::Feature>{}, feature}})
   {
      m = core::metrics::evaluate(pair.first, pair.second);
      require(std::isnan(m.hausdorff) && m.evaluated_features == 0 && m.invalid_comparisons == 1,
              "Missing feature not excluded");
   }
   require(std::isnan(core::metrics::evaluate({}, {}).hausdorff),
           "Empty dataset reported zero error");

   // Исключённая фича не отравляет метрики остальных и не уменьшает среднее.
   m = core::metrics::evaluate({{1, line, {}}, {2, line, {}}},
                               {{1, shifted, {}}, {2, gi::LineString{}, {}}}, 0, 1);
   close(m.hausdorff, 2);
   close(m.average_deviation, 2);
   require(m.evaluated_features == 1 && m.invalid_comparisons == 1 && m.degenerate_features == 1 &&
              m.orig_degenerate_features == 0,
           "Mixed valid/degenerate counters wrong");
   // В первой компоненте большое отклонение, а в последней потеряно отверстие:
   // вся фича должна быть исключена без вклада частично посчитанных компонент.
   auto displaced = polygon;
   for (auto &ring : displaced)
      for (auto &point : ring)
         point.y += 100;
   const gi::MultiPolygon partial_a{{polygon, polygon}};
   const gi::MultiPolygon partial_b{{displaced, gi::Polygon{polygon.front()}}};
   m = core::metrics::evaluate({{1, line, {}}, {2, partial_a, {}}},
                               {{1, shifted, {}}, {2, partial_b, {}}}, 0, 1);
   close(m.hausdorff, 2);
   close(m.average_deviation, 2);
   require(m.evaluated_features == 1 && m.invalid_comparisons == 1 && m.degenerate_features == 0,
           "Partially comparable feature contaminated distance");
   m = evaluate(gi::LineString{}, gi::LineString{});
   require(m.orig_degenerate_features == 1 && m.degenerate_features == 1,
           "Original/result degeneracy not counted separately");

   // Проверка прерывания внутри составной геометрии и сохранения прогресса по фичам.
   bool cancelled = false;
   int checks = 0;
   try
   {
      core::metrics::evaluate({{1, original, {}}}, {{1, result, {}}}, 0, 1,
                              [&] { return ++checks > 10; });
   }
   catch (const gen::Cancelled &)
   {
      cancelled = true;
   }
   require(cancelled, "MultiLine metrics ignored cancellation");
   std::size_t progress = 0;
   core::metrics::evaluate(feature, feature, 0, 1, {},
                           [&](std::size_t count) { progress = count; });
   require(progress == 1, "Metrics progress lost");
}

void checkRealData(const char *path)
{
   gi::Importer importer(path);
   const auto algorithm = gen::makeAlgorithm("douglas-peucker");
   for (const auto &layer : importer.layers())
   {
      auto original = importer.readLayer(layer.name);
      auto simplified = original;
      for (auto &feature : simplified)
         feature.geometry = algorithm->simplify(feature.geometry, {{"epsilon", 0.1}});
      const auto result = core::metrics::evaluate(original, simplified, 0, 8);
      std::cout << layer.name << ": H=" << result.hausdorff
                << ", average=" << result.average_deviation
                << ", evaluated=" << result.evaluated_features
                << ", excluded=" << result.invalid_comparisons
                << ", degenerate=" << result.degenerate_features << '\n';
      require(result.evaluated_features + result.invalid_comparisons == original.size(),
              "Real data comparison counters do not cover all features");
      require((result.evaluated_features > 0) == std::isfinite(result.hausdorff),
              "Real data distance availability does not match evaluated features");
   }
}
} // namespace

int main(int argc, char **argv)
{
   try
   {
      checkKnownGeometries();
      for (int i = 1; i < argc; ++i)
         checkRealData(argv[i]);
      std::cout << "PASS: MultiLine, empty and degenerate geometry, missing components, numerical "
                   "metrics, cancellation\n";
      return 0;
   }
   catch (const std::exception &error)
   {
      std::cerr << error.what() << '\n';
      return 1;
   }
}
