#pragma once

#include "core/map_data.h"
#include "core/metrics.h"
#include "generalization/params.h"

#include <QString>
#include <memory>
#include <string>

class QThread;

namespace core
{

struct GeneralizationResult
{
   std::vector<std::vector<gi::Feature>> layers;
   std::vector<metrics::SimplificationMetrics> metrics;
   QString error;
   bool interrupted = false;
};

// Вызывается только в рабочем потоке. Входные данные и настройки — снимок запуска.
class GeneralizationWorker
{
public:
   GeneralizationWorker(std::shared_ptr<const MapData> data,
                        std::string algorithm, gen::ParamSet params);
   GeneralizationResult run(QThread& thread) const;

private:
   std::shared_ptr<const MapData> data_;
   std::string algorithm_;
   gen::ParamSet params_;
};

} // namespace core
