#pragma once

#include "generalization/params.h"

#include <QString>
#include <map>
#include <vector>

namespace core
{
struct ScenarioParameter
{
   double min_value;
   double max_value;
   double step;
   double initial;
   bool logarithmic;
};

struct Scenario
{
   QString id;
   QString description;
   QString config_path;
   QString config_sha256;
   QString data_path;
   int expected_epsg = 0;
   std::string default_algorithm;
   std::map<std::string, std::map<std::string, ScenarioParameter>> algorithms;
};

// Проверяет структуру и все настройки. Пути разрешаются относительно JSON.
// Ошибки конфигурации бросают std::invalid_argument; GDAL здесь не используется.
std::vector<Scenario> readScenarios(const QString &path);
} // namespace core
