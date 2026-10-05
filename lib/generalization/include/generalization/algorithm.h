#pragma once

#include "generalization/cancellation.h"
#include "generalization/params.h"
#include "geometry-importer/geometry.h"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace gen
{

class GeneralizationAlgorithm
{
public:
   virtual ~GeneralizationAlgorithm() = default;

   virtual std::string_view name() const noexcept = 0;
   virtual ParamSpecs       paramSpecs() const    = 0;

   // Публичная точка входа: диспетчеризует по типу геометрии и вызывает
   // simplifyLine для линий и для каждого кольца полигонов.
   gi::Geometry simplify(const gi::Geometry &input, const ParamSet &params,
                         const CancellationCheck &cancelled = {}) const;

 protected:
   // Реализуется конкретными алгоритмами. Гарантии:
   //   - первый и последний элементы должны быть сохранены;
   //   - порядок точек сохраняется;
   //   - возвращаемая линия содержит не менее двух точек.
   virtual gi::LineString simplifyLine(const gi::LineString &line, const ParamSet &params,
                                       const CancellationCheck &cancelled) const = 0;
};

// Фабрика: имя → экземпляр. Бросает std::invalid_argument, если имени нет.
std::unique_ptr<GeneralizationAlgorithm> makeAlgorithm(std::string_view name);

// Все зарегистрированные алгоритмы. Порядок стабилен.
std::vector<std::string> availableAlgorithms();

} // namespace gen