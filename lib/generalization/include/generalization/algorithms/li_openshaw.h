#pragma once

#include "generalization/algorithm.h"

namespace gen
{

// Векторный вариант Li-Openshaw: минимальный размер объекта.
// Точка сохраняется, только если удалена от последней сохранённой
// не менее чем на window_size.
class LiOpenshaw : public GeneralizationAlgorithm
{
public:
   std::string_view name() const noexcept override { return "li-openshaw"; }
   ParamSpecs       paramSpecs() const override;

protected:
  gi::LineString simplifyLine(const gi::LineString &line, const ParamSet &params,
                              const CancellationCheck &cancelled) const override;
};

} // namespace gen