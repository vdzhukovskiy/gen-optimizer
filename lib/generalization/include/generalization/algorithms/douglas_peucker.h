#pragma once

#include "generalization/algorithm.h"

namespace gen
{

class DouglasPeucker : public GeneralizationAlgorithm
{
public:
   std::string_view name() const noexcept override { return "douglas-peucker"; }
   ParamSpecs       paramSpecs() const override;

protected:
   gi::LineString simplifyLine(const gi::LineString& line,
                               const ParamSet& params) const override;
};

} // namespace gen