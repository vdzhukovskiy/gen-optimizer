#pragma once

#include <string_view>
#include "generalization/params.h"

namespace gen::detail
{

// Возвращает значение параметра или бросает std::invalid_argument.
double requireParam(const ParamSet& params, std::string_view name);

} // namespace gen::detail