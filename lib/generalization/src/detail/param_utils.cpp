#include "generalization/detail/param_utils.h"

#include <stdexcept>
#include <string>

namespace gen::detail
{

double requireParam(const ParamSet& params, std::string_view name)
{
   auto it = params.find(std::string(name));
   if (it == params.end())
      throw std::invalid_argument("missing parameter: " + std::string(name));
   return it->second;
}

} // namespace gen::detail