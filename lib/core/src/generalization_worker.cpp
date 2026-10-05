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

GeneralizationResult
GeneralizationWorker::run(QThread &thread,
                          const std::function<void(const GeneralizationProgress &)> &progress) const
{
   GeneralizationResult result;
   try
   {
      const gen::CancellationCheck cancelled = [&thread]
      { return thread.isInterruptionRequested(); };
      gen::checkCancelled(cancelled);
      GeneralizationProgress current;
      for (const auto &layer : data_->layers)
         current.total += 2 * static_cast<qint64>(layer.features.size());
      QElapsedTimer progress_timer;
      progress_timer.start();
      const auto report = [&](bool force = false)
      {
         if (progress && (force || progress_timer.elapsed() >= 50))
         {
            progress(current);
            progress_timer.restart();
         }
      };
      const auto algorithm = gen::makeAlgorithm(algorithm_);
      result.layers.resize(data_->layers.size());
      result.metrics.resize(data_->layers.size());

      for (std::size_t li = 0; li < data_->layers.size(); ++li)
      {
         const auto& original = data_->layers[li].features;
         auto& simplified = result.layers[li];
         simplified.reserve(original.size());
         current.layer = QString::fromStdString(data_->layers[li].info.name);
         current.phase = QStringLiteral("Simplifying");
         current.layer_completed = 0;
         current.layer_total = static_cast<qint64>(original.size());
         report(true);
         QElapsedTimer timer;
         timer.start();

         for (const auto& feature : original)
         {
            gen::checkCancelled(cancelled);

            gi::Feature out;
            out.id = feature.id;
            out.attributes = feature.attributes;
            try
            {
               out.geometry = algorithm->simplify(feature.geometry, params_, cancelled);
            }
            catch (const gen::Cancelled &)
            {
               throw;
            }
            catch (const std::exception&)
            {
               out.geometry = feature.geometry;
            }
            simplified.push_back(std::move(out));
            ++current.completed;
            ++current.layer_completed;
            report();
         }

         const double elapsed_ms = timer.nsecsElapsed() / 1e6;
         gen::checkCancelled(cancelled);
         current.phase = QStringLiteral("Evaluating metrics");
         current.layer_completed = 0;
         report(true);
         result.metrics[li] = metrics::evaluate(original, simplified, elapsed_ms, 8, cancelled,
                                                [&](std::size_t completed)
                                                {
                                                   ++current.completed;
                                                   current.layer_completed =
                                                      static_cast<qint64>(completed);
                                                   report();
                                                });
         report(true);
      }
      gen::checkCancelled(cancelled);
   }
   catch (const gen::Cancelled &)
   {
      result.interrupted = true;
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
