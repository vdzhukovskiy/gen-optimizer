#include "core/session.h"

#include "core/generalization_worker.h"
#include "geometry-importer/importer.h"

#include <QCoreApplication>
#include <QThread>
#include <algorithm>
#include <cmath>
#include <exception>
#include <stdexcept>
#include <utility>

namespace core
{

namespace 
{
   bool is_noise(const std::string& name)
   {
      static const std::vector<std::string> noise = {"bathymetry",         "graticules",
                                                     "label_points",       "_seams",
                                                     "wgs84_bounding_box", "antarctic_ice_shelves"};
      for (const auto &n : noise)
         if (name.find(n) != std::string::npos)
            return true;
      return false;
   }
} // namespace

Session::Session(QObject* parent) : QObject(parent)
{
   // Алгоритм по умолчанию — чтобы UI сразу имел что показать.
   setAlgorithm("douglas-peucker");
}

Session::~Session()
{
   if (generalization_thread_)
   {
      generalization_thread_->requestInterruption();
      generalization_thread_->wait();
   }
}

// ─── Загрузка данных ────────────────────────────────────────────────

void Session::loadPath(const QString& path, const QString& filter)
{
   loadData(path, filter, std::nullopt);
}

bool Session::loadScenario(const QString& config_path, const QString& scenario_id)
{
   try
   {
      const auto scenarios = readScenarios(config_path);
      const auto selected = std::find_if(scenarios.begin(), scenarios.end(),
                                        [&](const Scenario& s) { return s.id == scenario_id; });
      if (selected == scenarios.end()) throw std::invalid_argument("Unknown scenario id");
      // Проверить файл и CRS до изменения текущих данных и настроек.
      gi::Importer importer(selected->data_path.toStdString());
      const auto layers = importer.layers();
      if (layers.empty()) throw std::invalid_argument("Scenario file has no layers");
      for (const auto& layer : layers)
         if (layer.crs.epsg != selected->expected_epsg)
            throw std::invalid_argument("Scenario data CRS differs from target_epsg");
      loadData(selected->data_path, {}, *selected);
      return state_ == State::Ready && !data_->layers.empty();
   }
   catch (const std::exception& error)
   {
      last_error_ = QString::fromUtf8(error.what());
      emit errorOccurred(last_error_);
      emit statusMessage(tr("scenario failed: %1").arg(last_error_));
      return false;
   }
}

void Session::loadData(const QString& path, const QString& filter, std::optional<Scenario> scenario)
{
   cancelGeneralization();
   const bool leaving_scenario = scenario_.has_value() && !scenario.has_value();
   scenario_ = std::move(scenario);
   setState(State::Loading);
   emit statusMessage(tr("loading %1...").arg(path));

   data_ = std::make_shared<MapData>();
   last_error_.clear();
   clearGeneralization();

   try
   {
      gi::Importer imp(path.toStdString());
      auto layers = imp.layers();

      std::vector<gi::LayerInfo> kept;
      kept.reserve(layers.size());
      for (auto& info : layers)
      {
         const QString name = QString::fromStdString(info.name);

         if (is_noise(info.name))
            continue;

         if (!filter.isEmpty() && !name.contains(filter, Qt::CaseInsensitive))
            continue;

         kept.push_back(std::move(info));
      }
      layers = std::move(kept);

      if (layers.empty())
      {
         if (scenario_) throw std::runtime_error("Scenario has no matching layers");
         if (leaving_scenario) setAlgorithm(currentAlgorithm());
         else refreshParamSpecs();
         setState(State::Ready);
         emit statusMessage(tr("no matching layers"));
         emit dataChanged();
         return;
      }

      const int total   = static_cast<int>(layers.size());
      int       index   = 0;
      int       skipped = 0;

      bool crs_checked  = false;
      bool crs_mismatch = false;

      for (auto& info : layers)
      {
         ++index;
         emit statusMessage(tr("loading [%1/%2] %3...")
                                .arg(index).arg(total)
                                .arg(QString::fromStdString(info.name)));
         QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

         std::vector<gi::Feature> features;
         try
         {
            features = imp.readLayer(info.name);
         }
         catch (const std::exception& e)
         {
            ++skipped;
            emit statusMessage(tr("skipped %1: %2")
                                   .arg(QString::fromStdString(info.name))
                                   .arg(e.what()));
            continue;
         }

         if (!crs_checked)
         {
            data_->reference_crs = info.crs;
            crs_checked = true;
         }
         else if (!data_->reference_crs.sameAs(info.crs))
         {
            crs_mismatch = true;
         }

         if (!data_->has_bounds)
         {
            data_->union_bounds = info.bounds;
            data_->has_bounds   = true;
         }
         else
         {
            data_->union_bounds.minX = std::min(data_->union_bounds.minX, info.bounds.minX);
            data_->union_bounds.minY = std::min(data_->union_bounds.minY, info.bounds.minY);
            data_->union_bounds.maxX = std::max(data_->union_bounds.maxX, info.bounds.maxX);
            data_->union_bounds.maxY = std::max(data_->union_bounds.maxY, info.bounds.maxY);
         }

         Layer layer;
         layer.info     = std::move(info);
         layer.features = std::move(features);
         data_->layers.push_back(std::move(layer));
      }

      if (data_->layers.empty())
      {
         if (scenario_) throw std::runtime_error("Scenario data could not be read");
         if (leaving_scenario) setAlgorithm(currentAlgorithm());
         else refreshParamSpecs();
         setState(State::Ready);
         emit statusMessage(tr("nothing loaded"));
         emit dataChanged();
         return;
      }

      const auto& first_crs = data_->reference_crs;
      const QString units = first_crs.units.empty()
                                ? tr("unknown units")
                                : QString::fromStdString(first_crs.units);
      const QString crs_label = first_crs.epsg != 0
                                    ? QStringLiteral("EPSG:%1").arg(first_crs.epsg)
                                    : (first_crs.isValid() ? tr("custom") : tr("no CRS"));

      QString msg = tr("loaded %1 layer(s), skipped %2, CRS %3 (%4)")
                        .arg(data_->layers.size())
                        .arg(skipped)
                        .arg(crs_label, units);
      if (crs_mismatch)
         msg += tr(" [WARNING: layer CRS mismatch]");

      if (scenario_)
      {
         if (crs_mismatch || data_->reference_crs.epsg != scenario_->expected_epsg)
            throw std::runtime_error("Scenario CRS mismatch");
         setAlgorithm(scenario_->default_algorithm);
         msg = tr("scenario %1: ").arg(scenario_->id) + msg;
      }
      else if (leaving_scenario)
         setAlgorithm(currentAlgorithm());
      else
         refreshParamSpecs();
      setState(State::Ready);
      emit statusMessage(msg);
      emit dataChanged();
   }
   catch (const std::exception& e)
   {
      last_error_ = QString::fromUtf8(e.what());
      scenario_.reset();
      data_ = std::make_shared<MapData>();
      refreshParamSpecs();
      setState(State::Error);
      emit dataChanged();
      emit errorOccurred(last_error_);
      emit statusMessage(tr("load failed: %1").arg(last_error_));
   }
}

// ─── Генерализация ──────────────────────────────────────────────────

void Session::setAlgorithm(std::string_view name)
{
   auto algorithm = gen::makeAlgorithm(name);
   algorithm_ = std::move(algorithm);
   specs_ = specsForCurrentData();
   params_.clear();
   for (const auto& s : specs_)
      params_[s.name] = initialParamValue(s);

   emit algorithmChanged(QString::fromStdString(std::string(name)));
   emit paramsChanged();
}

gen::ParamSpecs Session::specsForCurrentData() const
{
   auto specs = algorithm_->paramSpecs();
   const auto &crs = data_->reference_crs;
   const bool metres = !crs.is_geographic && (crs.units == "metre" || crs.units == "meter");
   if (metres)
      for (auto &spec : specs)
      {
         if (spec.dimension == gen::ParamDimension::Length)
         {
            spec.min_value = 1;
            spec.max_value = 10000;
            spec.step = 1;
         }
         else if (spec.dimension == gen::ParamDimension::Area)
         {
            spec.min_value = 1;
            spec.max_value = 1e8;
            spec.step = 1;
         }
      }
   if (scenario_)
   {
      const auto algorithm_settings = scenario_->algorithms.find(currentAlgorithm());
      if (algorithm_settings != scenario_->algorithms.end())
         for (auto& spec : specs)
         {
            const auto& configured = algorithm_settings->second.at(spec.name);
            spec.min_value = configured.min_value;
            spec.max_value = configured.max_value;
            spec.step = configured.step;
            spec.logarithmic = configured.logarithmic;
         }
   }
   return specs;
}

double Session::initialParamValue(const gen::ParamSpec& spec) const
{
   if (scenario_)
   {
      const auto algorithm_settings = scenario_->algorithms.find(currentAlgorithm());
      if (algorithm_settings != scenario_->algorithms.end())
         return algorithm_settings->second.at(spec.name).initial;
   }
   return defaultParamValue(spec);
}

void Session::refreshParamSpecs()
{
   const auto updated = specsForCurrentData();
   bool changed = updated.size() != specs_.size();
   for (std::size_t i = 0; !changed && i < updated.size(); ++i)
      changed = updated[i].min_value != specs_[i].min_value ||
                updated[i].max_value != specs_[i].max_value || updated[i].step != specs_[i].step
                || updated[i].logarithmic != specs_[i].logarithmic;
   if (changed)
   {
      specs_ = updated;
      resetParams();
      emit algorithmChanged(QString::fromStdString(currentAlgorithm()));
   }
}

void Session::setParam(std::string_view name, double value)
{
   const std::string key(name);
   auto it = params_.find(key);
   if (it == params_.end())
      return;   // неизвестный параметр — игнорируем тихо
   if (it->second == value)
      return;
   it->second = value;
   emit paramsChanged();
}

void Session::resetParams()
{
   for (const auto& s : specs_)
      params_[s.name] = initialParamValue(s);
   emit paramsChanged();
}

void Session::regenerateAsync()
{
   if (!algorithm_ || state_ != State::Ready || data_->layers.empty())
   {
      emit statusMessage(tr("no data to generalize"));
      return;
   }

   GeneralizationRequest request{data_, currentAlgorithm(), params_, evaluation_};
   if (isGeneralizing())
   {
      pending_request_ = std::move(request);
      cancellation_requested_ = true;
      generalization_thread_->requestInterruption();
      emit generalizationCancellationRequested();
      emit statusMessage(tr("cancelling current calculation; new settings queued"));
      return;
   }
   startGeneralization(std::move(request));
}

void Session::cancelGeneralization()
{
   pending_request_.reset();
   if (!isGeneralizing())
      return;
   cancellation_requested_ = true;
   generalization_thread_->requestInterruption();
   emit generalizationCancellationRequested();
   emit statusMessage(tr("cancelling generalization..."));
}

void Session::startGeneralization(GeneralizationRequest request)
{
   const auto job_id = ++job_id_;
   auto result = std::make_shared<GeneralizationResult>();
   GeneralizationWorker worker(request.data, request.algorithm, request.params, request.evaluation);
   progress_ = {};
   for (const auto &layer : request.data->layers)
      progress_.total += 2 * static_cast<qint64>(layer.features.size());
   cancellation_requested_ = false;
   auto *thread = QThread::create(
      [this, worker = std::move(worker), result, job_id]
      {
         *result =
            worker.run(*QThread::currentThread(),
                       [this, job_id](const GeneralizationProgress &progress)
                       {
                          // Только замыкание передаётся в GUI-поток: дополнительные метатипы не
                          // нужны.
                          QMetaObject::invokeMethod(
                             this,
                             [this, job_id, progress]
                             {
                                if (job_id != job_id_ || !isGeneralizing() || isCancelling())
                                   return;
                                progress_ = progress;
                                emit generalizationProgressChanged(
                                   progress.completed, progress.total, progress.layer,
                                   progress.phase, progress.layer_completed, progress.layer_total);
                             },
                             Qt::QueuedConnection);
                       });
      });
   thread->setParent(this);
   generalization_thread_ = thread;

   connect(
      thread, &QThread::finished, this,
      [this, thread, result, request = std::move(request)]
      {
         thread->wait();
         const bool cancelled = cancellation_requested_ || result->interrupted;
         generalization_thread_ = nullptr;
         cancellation_requested_ = false;
         thread->deleteLater();

         if (cancelled)
         {
            emit statusMessage(tr("generalization cancelled"));
            emit generalizationCancelled();
         }
         else if (!result->error.isEmpty())
         {
            last_error_ = result->error;
            emit errorOccurred(last_error_);
            emit statusMessage(tr("generalization failed: %1").arg(last_error_));
         }
         else if (request.data != data_)
         {
            emit statusMessage(tr("generalization result discarded: data changed"));
         }
         else
         {
            simplified_layers_ = std::move(result->layers);
            layer_metrics_ = std::move(result->metrics);
            result_algorithm_ = request.algorithm;
            result_params_ = request.params;
            result_evaluation_ = result->evaluation;
            result_total_ms_ = result->total_ms;
            last_error_.clear();
            progress_.completed = progress_.total;
            emit statusMessage(tr("generalization complete"));
            emit generalizationDone();
         }
         emit generalizationFinished();

         if (pending_request_ && !isGeneralizing())
         {
            auto next = std::move(*pending_request_);
            pending_request_.reset();
            if (next.data == data_ && state_ == State::Ready)
               startGeneralization(std::move(next));
         }
      },
      Qt::QueuedConnection);

   thread->start();
   emit generalizationStarted();
   emit statusMessage(tr("generalizing..."));
}

void Session::setEvaluationSettings(metrics::EvaluationSettings settings)
{
   if ((settings.mode != metrics::EvaluationMode::Fast &&
        settings.mode != metrics::EvaluationMode::Detailed) ||
       settings.fast_stride < 1 || settings.fast_stride > 1000000)
      throw std::invalid_argument("Invalid evaluation settings");
   if (settings == evaluation_)
      return;
   evaluation_ = settings;
   emit evaluationSettingsChanged();
}

bool Session::resultMatchesSettings() const
{
   return hasGeneralization() && result_algorithm_ == currentAlgorithm() &&
          result_params_ == params_ && result_evaluation_.mode == evaluation_.mode &&
          result_evaluation_.stride() == evaluation_.stride();
}

std::string Session::currentAlgorithm() const
{
   return algorithm_ ? std::string(algorithm_->name()) : std::string{};
}

const gen::ParamSet& Session::currentParams() const noexcept { return params_; }
const gen::ParamSpecs& Session::currentSpecs() const noexcept { return specs_; }

bool Session::hasGeneralization() const noexcept
{
   return !simplified_layers_.empty();
}

const std::vector<gi::Feature>& Session::simplified(int layer_index) const
{
   static const std::vector<gi::Feature> empty;
   if (layer_index < 0 ||
       layer_index >= static_cast<int>(simplified_layers_.size()))
      return empty;
   return simplified_layers_[layer_index];
}

const metrics::SimplificationMetrics& Session::layerMetrics(int layer_index) const
{
   static const metrics::SimplificationMetrics zeros;
   if (layer_index < 0 ||
       layer_index >= static_cast<int>(layer_metrics_.size()))
      return zeros;
   return layer_metrics_[layer_index];
}

// ─── Внутреннее ─────────────────────────────────────────────────────

void Session::setState(State s)
{
   if (state_ == s)
      return;
   state_ = s;
   emit stateChanged(state_);
}

void Session::clearGeneralization()
{
   simplified_layers_.clear();
   layer_metrics_.clear();
   result_algorithm_.clear();
   result_params_.clear();
   result_evaluation_ = {};
   result_total_ms_ = 0.0;
}

double Session::defaultParamValue(const gen::ParamSpec& spec)
{
   // Для логарифмических параметров (epsilon, area_threshold) — геометрическое
   // среднее, для остальных — арифметическое. Даёт «умеренное» значение,
   // при котором упрощение заметно, но данные не разрушены.
   if (spec.logarithmic && spec.min_value > 0.0 && spec.max_value > 0.0)
      return std::sqrt(spec.min_value * spec.max_value);
   return 0.5 * (spec.min_value + spec.max_value);
}

} // namespace core