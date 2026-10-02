#include "core/session.h"

#include "geometry-importer/importer.h"

#include <QCoreApplication>
#include <QElapsedTimer>

#include <algorithm>
#include <cmath>
#include <exception>
#include <stdexcept>

namespace core
{

namespace 
{
   constexpr std::size_t kHausdorffStride = 8;

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

Session::~Session() = default;

// ─── Загрузка данных ────────────────────────────────────────────────

void Session::loadPath(const QString& path, const QString& filter)
{
   setState(State::Loading);
   emit statusMessage(tr("loading %1...").arg(path));

   data_ = MapData{};
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
            data_.reference_crs = info.crs;
            crs_checked = true;
         }
         else if (!data_.reference_crs.sameAs(info.crs))
         {
            crs_mismatch = true;
         }

         if (!data_.has_bounds)
         {
            data_.union_bounds = info.bounds;
            data_.has_bounds   = true;
         }
         else
         {
            data_.union_bounds.minX = std::min(data_.union_bounds.minX, info.bounds.minX);
            data_.union_bounds.minY = std::min(data_.union_bounds.minY, info.bounds.minY);
            data_.union_bounds.maxX = std::max(data_.union_bounds.maxX, info.bounds.maxX);
            data_.union_bounds.maxY = std::max(data_.union_bounds.maxY, info.bounds.maxY);
         }

         Layer layer;
         layer.info     = std::move(info);
         layer.features = std::move(features);
         data_.layers.push_back(std::move(layer));
      }

      if (data_.layers.empty())
      {
         setState(State::Ready);
         emit statusMessage(tr("nothing loaded"));
         emit dataChanged();
         return;
      }

      const auto& first_crs = data_.reference_crs;
      const QString units = first_crs.units.empty()
                                ? tr("unknown units")
                                : QString::fromStdString(first_crs.units);
      const QString crs_label = first_crs.epsg != 0
                                    ? QStringLiteral("EPSG:%1").arg(first_crs.epsg)
                                    : (first_crs.isValid() ? tr("custom") : tr("no CRS"));

      QString msg = tr("loaded %1 layer(s), skipped %2, CRS %3 (%4)")
                        .arg(data_.layers.size())
                        .arg(skipped)
                        .arg(crs_label, units);
      if (crs_mismatch)
         msg += tr(" [WARNING: layer CRS mismatch]");

      setState(State::Ready);
      emit statusMessage(msg);
      emit dataChanged();
   }
   catch (const std::exception& e)
   {
      last_error_ = QString::fromUtf8(e.what());
      setState(State::Error);
      emit errorOccurred(last_error_);
      emit statusMessage(tr("load failed: %1").arg(last_error_));
   }
}

// ─── Генерализация ──────────────────────────────────────────────────

void Session::setAlgorithm(std::string_view name)
{
   algorithm_ = gen::makeAlgorithm(name);
   specs_     = algorithm_->paramSpecs();
   params_.clear();
   for (const auto& s : specs_)
      params_[s.name] = defaultParamValue(s);

   clearGeneralization();

   emit algorithmChanged(QString::fromStdString(std::string(name)));
   emit paramsChanged();
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
      params_[s.name] = defaultParamValue(s);
   emit paramsChanged();
}

void Session::regenerate()
{
   if (!algorithm_ || data_.layers.empty())
   {
      emit generalizationDone();
      return;
   }

   simplified_layers_.clear();
   layer_metrics_.clear();
   simplified_layers_.resize(data_.layers.size());
   layer_metrics_.resize(data_.layers.size());

   const int total = static_cast<int>(data_.layers.size());

   for (int li = 0; li < total; ++li)
   {
      const auto& original = data_.layers[li].features;
      auto&       result   = simplified_layers_[li];
      result.reserve(original.size());

      QElapsedTimer timer;
      timer.start();

      for (const auto& f : original)
      {
         gi::Feature out;
         out.id         = f.id;
         out.attributes = f.attributes;
         try
         {
            out.geometry = algorithm_->simplify(f.geometry, params_);
         }
         catch (const std::exception&)
         {
            // Если алгоритм упал на конкретной фиче — оставляем оригинал.
            // Метрика всё равно покажет, что улучшения нет.
            out.geometry = f.geometry;
         }
         result.push_back(std::move(out));
      }

      const double elapsed_ms = timer.nsecsElapsed() / 1e6;
      layer_metrics_[li] = metrics::evaluate(original, result, elapsed_ms,
                                             kHausdorffStride);

      emit statusMessage(tr("generalized [%1/%2] %3 — compression %4, H %5")
                             .arg(li + 1).arg(total)
                             .arg(QString::fromStdString(data_.layers[li].info.name))
                             .arg(layer_metrics_[li].compression_ratio, 0, 'f', 3)
                             .arg(layer_metrics_[li].hausdorff, 0, 'g', 4));
      QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
   }

   emit generalizationDone();
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