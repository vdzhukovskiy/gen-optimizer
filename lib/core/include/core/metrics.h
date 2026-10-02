#pragma once

#include <cstddef>
#include <vector>

#include "geometry-importer/feature.h"
#include "geometry-importer/geometry.h"

namespace core::metrics
{

// Результат сравнения исходной и генерализованной геометрии одного набора.
// Все дистанции — в единицах CRS входных данных.
struct SimplificationMetrics
{
   // Сжатие
   std::size_t orig_vertices      = 0;
   std::size_t result_vertices    = 0;
   std::size_t orig_features      = 0;
   std::size_t surviving_features = 0;
   double      compression_ratio  = 1.0;   // result_vertices / orig_vertices

   // Точность
   double hausdorff         = 0.0;   // максимум по всем фичам, на подвыборке
   double average_deviation = 0.0;   // среднее отклонение вершин оригинала

   // Производительность
   double elapsed_ms = 0.0;
};

// Число вершин в одной геометрии (для Multi* — суммируется по компонентам).
std::size_t countVertices(const gi::Geometry& g);

// Суммарное число вершин по всем фичам.
std::size_t countVertices(const std::vector<gi::Feature>& features);

// Геометрия вырождена после упрощения:
//   - LineString: < 2 вершин
//   - LinearRing: < 4 вершин (замкнутое кольцо требует >= 3 уникальных + 1 замыкающая)
//   - Polygon: пустой exterior или вырожденный exterior ring
//   - Multi*: пусто или все компоненты вырождены
//   - Point / MultiPoint: всегда не вырождена
bool isDegenerate(const gi::Geometry& g);

// Расстояние Хаусдорфа между двумя ломаными, с прореживанием.
// stride = 1 → каждая вершина, stride = 8 → каждая восьмая.
// Симметричная версия: max(h(A,B), h(B,A)).
double hausdorff(const gi::LineString& a, const gi::LineString& b,
                 std::size_t stride = 8);

// Среднее отклонение вершин a от ближайших сегментов b (и наоборот),
// усреднённое по обеим сторонам. Тоже с прореживанием.
double averageDeviation(const gi::LineString& a, const gi::LineString& b,
                        std::size_t stride = 8);

// Полная оценка. Ожидает, что original и simplified имеют одинаковое число
// фич и что фичи соответствуют друг другу по индексу (алгоритмы
// не удаляют фичи, только вершины).
// elapsed_ms — передаётся снаружи (замер вокруг simplify()).
SimplificationMetrics evaluate(const std::vector<gi::Feature>& original,
                               const std::vector<gi::Feature>& simplified,
                               double elapsed_ms = 0.0,
                               std::size_t stride = 8);

} // namespace core::metrics