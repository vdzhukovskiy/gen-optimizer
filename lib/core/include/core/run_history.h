#pragma once

#include "core/metrics.h"
#include "generalization/params.h"
#include "geometry-importer/crs.h"

#include <QDateTime>
#include <QString>
#include <vector>

namespace core
{
struct RunLayer
{
   QString name;
   gi::Crs crs;
   metrics::SimplificationMetrics metrics;
};

// Только снимок метаданных и метрик: геометрии в журнале не дублируются.
struct RunRecord
{
   QString id;
   QDateTime started_utc;
   QDateTime finished_utc;
   QString data_path;
   QString layer_filter;
   QString scenario_id;
   QString config_path;
   QString config_sha256;
   std::string algorithm;
   gen::ParamSet params;
   metrics::EvaluationSettings evaluation;
   double calculation_ms = 0;
   std::vector<RunLayer> layers;
};

QString runParametersJson(const gen::ParamSet &params);
// UTF-8, запятая, десятичная точка, CRLF; нечисловые метрики — пустая ячейка.
// Одна строка на слой каждого завершённого прогона. Атомарная замена файла.
// Ошибка записи бросает std::runtime_error; существующий файл не повреждается.
void exportRunsCsv(const QString &path, const std::vector<RunRecord> &runs);
} // namespace core
