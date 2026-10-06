#pragma once

#include "generalization/algorithm.h"

namespace gen
{

// Растрово-векторный Li-Openshaw с квадратной сеткой без перекрытий.
// Представитель прохода через ячейку — середина первого входа и последнего выхода.
// Сетка центрирована на начальной вершине; концы линии сохраняются.
// Результат может содержать новые точки и больше вершин при редкой оцифровке.
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
