#pragma once

#include "core/map_data.h"
#include "core/metrics.h"
#include "generalization/params.h"

#include <QString>
#include <functional>
#include <memory>
#include <string>

class QThread;

namespace core
{

struct GeneralizationProgress
{
   qint64 completed = 0;
   qint64 total = 0;
   QString layer;
   QString phase;
   qint64 layer_completed = 0;
   qint64 layer_total = 0;
};

struct GeneralizationResult
{
   std::vector<std::vector<gi::Feature>> layers;
   std::vector<metrics::SimplificationMetrics> metrics;
   metrics::EvaluationSettings evaluation;
   double total_ms = 0.0; // полный run(), включая подготовку и все слои
   QString error;
   bool interrupted = false;
};

// Вызывается только в рабочем потоке. Входные данные и настройки — снимок запуска.
class GeneralizationWorker
{
public:
   GeneralizationWorker(std::shared_ptr<const MapData> data,
                        std::string algorithm, gen::ParamSet params,
                        metrics::EvaluationSettings evaluation = {});
   GeneralizationResult
   run(QThread &thread,
       const std::function<void(const GeneralizationProgress &)> &progress = {}) const;

 private:
   std::shared_ptr<const MapData> data_;
   std::string algorithm_;
   gen::ParamSet params_;
   metrics::EvaluationSettings evaluation_;
};

} // namespace core
