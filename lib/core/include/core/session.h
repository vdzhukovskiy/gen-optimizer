#pragma once

#include "core/generalization_worker.h"
#include "core/map_data.h"
#include "core/scenario.h"
#include "core/metrics.h"
#include "generalization/algorithm.h"

#include <QObject>
#include <QString>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

class QThread;

namespace core
{

class Session : public QObject
{
   Q_OBJECT

public:
   enum class State { Idle, Loading, Ready, Error };
   Q_ENUM(State)

   explicit Session(QObject* parent = nullptr);
   ~Session() override;

   // ─── Данные ──────────────────────────────────────────────────────
   void loadPath(const QString& path, const QString& filter = {});
   bool loadScenario(const QString& config_path, const QString& scenario_id);
   const std::optional<Scenario>& scenario() const noexcept { return scenario_; }

   State          state() const noexcept { return state_; }
   const MapData& data()  const noexcept { return *data_; }
   QString        lastError() const      { return last_error_; }

   // ─── Генерализация ───────────────────────────────────────────────
   // Устанавливает алгоритм и сбрасывает параметры к дефолтным.
   // Если имя неизвестно — бросает std::invalid_argument.
   // Автоматически НЕ запускает пересчёт — вызывайте regenerateAsync().
   void setAlgorithm(std::string_view name);

   // Меняет один параметр. Автоматически НЕ запускает пересчёт.
   void setParam(std::string_view name, double value);

   // Возвращает initial из сценария или дефолтные значения текущего алгоритма.
   // Автоматически НЕ запускает пересчёт.
   void resetParams();

   // Прогоняет текущий алгоритм с текущими параметрами по всем слоям
   // и пересчитывает метрики в фоне. Повторный запуск отменяет текущую задачу
   // и ставит снимок новых настроек в очередь (одна последняя задача).
   void regenerateAsync();
   void cancelGeneralization();
   bool isGeneralizing() const noexcept { return generalization_thread_ != nullptr; }
   bool isCancelling() const noexcept
   {
      return cancellation_requested_;
   }
   bool hasPendingRestart() const noexcept
   {
      return pending_request_.has_value();
   }
   const GeneralizationProgress &generalizationProgress() const noexcept
   {
      return progress_;
   }

   std::string                    currentAlgorithm() const;
   const gen::ParamSet&           currentParams()    const noexcept;
   const gen::ParamSpecs&         currentSpecs()     const noexcept;

   bool hasGeneralization() const noexcept;
   const std::string &resultAlgorithm() const noexcept
   {
      return result_algorithm_;
   }
   const gen::ParamSet &resultParams() const noexcept
   {
      return result_params_;
   }
   bool resultMatchesSettings() const;
   double generalizationTotalMs() const noexcept
   {
      return result_total_ms_;
   }

   // Упрощённые фичи слоя. Пустой вектор, если пересчёт не выполнялся.
   const std::vector<gi::Feature>& simplified(int layer_index) const;

   // Метрики слоя. Значения по умолчанию (нули), если пересчёта не было.
   const metrics::SimplificationMetrics& layerMetrics(int layer_index) const;

signals:
   void stateChanged(State s);
   void dataChanged();
   void algorithmChanged(const QString& name);
   void paramsChanged();
   void generalizationStarted();
   void generalizationCancellationRequested();
   void generalizationCancelled();
   void generalizationProgressChanged(qint64 completed, qint64 total, const QString &layer,
                                      const QString &phase, qint64 layer_completed,
                                      qint64 layer_total);
   void generalizationFinished();
   void generalizationDone();
   void errorOccurred(const QString& message);
   void statusMessage(const QString& message);

private:
  struct GeneralizationRequest
  {
     std::shared_ptr<const MapData> data;
     std::string algorithm;
     gen::ParamSet params;
  };

  void startGeneralization(GeneralizationRequest request);
  void loadData(const QString& path, const QString& filter, std::optional<Scenario> scenario);
  double initialParamValue(const gen::ParamSpec& spec) const;
  gen::ParamSpecs specsForCurrentData() const;
  void refreshParamSpecs();
  void setState(State s);
  void clearGeneralization();
  static double defaultParamValue(const gen::ParamSpec &spec);

  State state_ = State::Idle;
  std::optional<Scenario> scenario_;
  std::shared_ptr<MapData> data_ = std::make_shared<MapData>();
  QString last_error_;
  QThread *generalization_thread_ = nullptr;
  std::uint64_t job_id_ = 0;
  bool cancellation_requested_ = false;
  std::optional<GeneralizationRequest> pending_request_;
  GeneralizationProgress progress_;
  std::string result_algorithm_;
  gen::ParamSet result_params_;
  double result_total_ms_ = 0.0;

  std::unique_ptr<gen::GeneralizationAlgorithm> algorithm_;
  gen::ParamSet params_;
  gen::ParamSpecs specs_;

  std::vector<std::vector<gi::Feature>> simplified_layers_;
  std::vector<metrics::SimplificationMetrics> layer_metrics_;
};

} // namespace core
