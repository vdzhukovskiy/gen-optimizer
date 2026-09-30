#pragma once

#include "generalization/algorithm.h"

namespace gen
{

class VisvalingamWhyatt : public GeneralizationAlgorithm
{
public:
   std::string_view name() const noexcept override { return "visvalingam-whyatt"; }
   ParamSpecs       paramSpecs() const override;

protected:
   gi::LineString simplifyLine(const gi::LineString& line,
                               const ParamSet& params) const override;
};

} // namespace gen