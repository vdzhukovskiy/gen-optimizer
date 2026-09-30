#pragma once

#include <functional>

#include "geometry-importer/geometry.h"

namespace gen::detail
{

using LineSimplifier = std::function<gi::LineString(const gi::LineString&)>;

// Применяет simplifyLine ко всем линиям и кольцам, сохраняя структуру геометрии.
gi::Geometry simplifyGeometry(const gi::Geometry& g, const LineSimplifier& f);

} // namespace gen::detail