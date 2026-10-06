#pragma once

#include "generalization/cancellation.h"
#include "geometry-importer/feature.h"
#include "geometry-importer/geometry.h"

#include <cstddef>
#include <functional>
#include <limits>
#include <vector>

namespace core::metrics
{

enum class EvaluationMode { Fast, Detailed };

// Настройка запуска; подробный режим всегда использует шаг 1.
struct EvaluationSettings
{
   EvaluationMode mode = EvaluationMode::Fast;
   std::size_t fast_stride = 8;
   std::size_t stride() const noexcept
   {
      return mode == EvaluationMode::Detailed ? 1 : fast_stride;
   }
   bool operator==(const EvaluationSettings &) const = default;
};

// Результат сравнения исходной и генерализованной геометрии одного набора.
// Все дистанции — в единицах CRS входных данных.
struct SimplificationMetrics
{
   // Сжатие
   std::size_t orig_vertices = 0;
   std::size_t result_vertices = 0;
   std::size_t orig_features = 0;
   std::size_t surviving_features = 0;
   std::size_t degenerate_features = 0; // результат: пустая/вырожденная геометрия
   std::size_t orig_degenerate_features = 0;
   std::size_t invalid_comparisons = 0; // исключённые из оценки пары фич
   std::size_t evaluated_features = 0; // пары фич, участвовавшие в оценке расстояний
   double compression_ratio = 1.0; // result_vertices / orig_vertices

   // Фактический шаг оценки; концы линий включаются всегда.
   std::size_t sample_stride = 8;

   // Точность
   // NaN означает отсутствие сопоставимых невырожденных фич, а не нулевую ошибку.
   double hausdorff = std::numeric_limits<double>::quiet_NaN();
   double average_deviation = std::numeric_limits<double>::quiet_NaN();

   // Производительность
   double simplification_ms = 0.0; // подготовка результата + упрощение + прогресс
   double metrics_ms = 0.0; // evaluate(), включая проверки геометрии и прогресс
   double total_ms = 0.0; // полный расчёт слоя в worker, без загрузки и GUI
};

// Число вершин в одной геометрии (для Multi* — суммируется по компонентам).
std::size_t countVertices(const gi::Geometry &g);

// Суммарное число вершин по всем фичам.
std::size_t countVertices(const std::vector<gi::Feature> &features);

// Геометрия вырождена после упрощения:
//   - LineString: менее двух различных точек
//   - Polygon: пусто, незамкнутые кольца или кольца нулевой ориентированной площади
//   - Multi*: пусто или хотя бы одна компонента вырождена
//   - MultiPoint: пусто; для всех типов неконечные координаты недопустимы
// Это базовая проверка, не полная проверка валидности/топологии.
bool isDegenerate(const gi::Geometry &g, const gen::CancellationCheck &cancelled = {});

// Расстояние Хаусдорфа между двумя ломаными, с прореживанием.
// stride = 1 → каждая вершина, stride = 8 → каждая восьмая.
// Симметричная версия: max(h(A,B), h(B,A)).
// Две пустые линии: 0; только одна пустая: +infinity.
// Одноточечная линия сравнивается как точка (evaluate отмечает её вырожденность).
double hausdorff(const gi::LineString &a, const gi::LineString &b, std::size_t stride = 8,
                 const gen::CancellationCheck &cancelled = {});

// Среднее отклонение вершин a от ближайших сегментов b (и наоборот),
// усреднённое по обеим сторонам. С прореживанием, включая оба конца.
// Пустые линии обрабатываются так же, как в hausdorff().
double averageDeviation(const gi::LineString &a, const gi::LineString &b, std::size_t stride = 8,
                        const gen::CancellationCheck &cancelled = {});

// Полная оценка. Ожидает, что original и simplified имеют одинаковое число
// фич и что фичи соответствуют друг другу по индексу (алгоритмы
// не удаляют фичи, только вершины).
// Компоненты Multi*, полигоны и кольца также соответствуют по индексу.
// Хаусдорф: максимум по компонентам; average_deviation: среднее по компонентам
// с одинаковым весом, включая точечные компоненты.
// Потеря/добавление фич или компонентов, смена типа и вырожденность любой стороны
// исключают всю пару фич из расстояний и увеличивают invalid_comparisons.
// evaluated_features показывает число учтённых пар. Без них обе дистанции NaN.
// Сжатие и счётчики вырожденности считаются по всем фичам, включая исключённые.
// Отмена бросает gen::Cancelled; progress получает число оценённых фич слоя.
// simplification_ms — передаётся снаружи (замер вокруг simplify()).
SimplificationMetrics evaluate(const std::vector<gi::Feature> &original,
                               const std::vector<gi::Feature> &simplified,
                               double simplification_ms = 0.0, std::size_t stride = 8,
                               const gen::CancellationCheck &cancelled = {},
                               const std::function<void(std::size_t)> &progress = {});

} // namespace core::metrics
