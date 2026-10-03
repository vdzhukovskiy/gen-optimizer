#include "core/generalization_worker.h"

#include "generalization/algorithm.h"

#include <QElapsedTimer>
#include <QThread>
#include <exception>
#include <utility>

namespace core
{

GeneralizationWorker::GeneralizationWorker(std::shared_ptr<const MapData> data,
                                         std::string algorithm, gen::ParamSet params)
    : data_(std::move(data)), algorithm_(std::move(algorithm)), params_(std::move(params))
{
}

GeneralizationResult GeneralizationWorker::run(QThread& thread) const
{
   GeneralizationResult result;
   try
   {
      const auto algorithm = gen::makeAlgorithm(algorithm_);
      result.layers.resize(data_->layers.size());
      result.metrics.resize(data_->layers.size());

      for (std::size_t li = 0; li < data_->layers.size(); ++li)
      {
         const auto& original = data_->layers[li].features;
         auto& simplified = result.layers[li];
         simplified.reserve(original.size());
         QElapsedTimer timer;
         timer.start();

         for (const auto& feature : original)
         {
            if (thread.isInterruptionRequested())
            {
               result.interrupted = true;
               return result;
            }

            gi::Feature out;
            out.id = feature.id;
            out.attributes = feature.attributes;
            try
            {
               out.geometry = algorithm->simplify(feature.geometry, params_);
            }
            catch (const std::exception&)
            {
               out.geometry = feature.geometry;
            }
            simplified.push_back(std::move(out));
         }

         const double elapsed_ms = timer.nsecsElapsed() / 1e6;
         if (thread.isInterruptionRequested())
         {
            result.interrupted = true;
            return result;
         }
         result.metrics[li] = metrics::evaluate(original, simplified, elapsed_ms, 8);
      }
      result.interrupted = thread.isInterruptionRequested();
   }
   catch (const std::exception& error)
   {
      result.error = QString::fromUtf8(error.what());
   }
   catch (...)
   {
      result.error = QStringLiteral("Unknown generalization error");
   }
   return result;
}

} // namespace core
