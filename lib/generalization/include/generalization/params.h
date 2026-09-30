#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace gen
{

// Значения параметров, передаваемые в конкретный прогон алгоритма.
using ParamSet = std::unordered_map<std::string, double>;

// Описание одного параметра — оптимизатор строит по нему пространство поиска.
struct ParamSpec
{
   std::string name;
   std::string description;
   double      min_value   = 0.0;
   double      max_value   = 1.0;
   double      step        = 0.01; // рекомендуемый шаг перебора
   bool        logarithmic = false;// true → шаг логарифмический (удобно для epsilon)
};

using ParamSpecs = std::vector<ParamSpec>;

} // namespace gen