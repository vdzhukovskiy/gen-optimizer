#include "generalization/algorithm.h"

#include "generalization/algorithms/douglas_peucker.h"
#include "generalization/algorithms/li_openshaw.h"
#include "generalization/algorithms/visvalingam_whyatt.h"
#include "generalization/detail/geometry_ops.h"

#include <stdexcept>

namespace gen
{

gi::Geometry GeneralizationAlgorithm::simplify(const gi::Geometry& input,
                                               const ParamSet& params) const
{
   auto fn = [this, &params](const gi::LineString& ls) {
      return simplifyLine(ls, params);
   };
   return detail::simplifyGeometry(input, fn);
}

std::unique_ptr<GeneralizationAlgorithm> makeAlgorithm(std::string_view name)
{
   if (name == "douglas-peucker")     return std::make_unique<DouglasPeucker>();
   if (name == "visvalingam-whyatt")  return std::make_unique<VisvalingamWhyatt>();
   if (name == "li-openshaw")         return std::make_unique<LiOpenshaw>();
   throw std::invalid_argument("unknown algorithm: " + std::string(name));
}

std::vector<std::string> availableAlgorithms()
{
   return {"douglas-peucker", "visvalingam-whyatt", "li-openshaw"};
}

} // namespace gen