#pragma once

#include "generalization/cancellation.h"
#include "geometry-importer/geometry.h"

#include <functional>

namespace gen::detail
{

using LineSimplifier = std::function<gi::LineString(const gi::LineString&)>;

// Применяет simplifyLine ко всем линиям и кольцам, сохраняя структуру геометрии.
gi::Geometry simplifyGeometry(const gi::Geometry &g, const LineSimplifier &f,
                              const CancellationCheck &cancelled = {});

} // namespace gen::detail