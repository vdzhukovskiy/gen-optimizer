#pragma once

#include <QObject>
#include <QString>

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "core/map_data.h"
#include "core/metrics.h"
#include "generalization/algorithm.h"

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

   State          state() const noexcept { return state_; }
   const MapData& data()  const noexcept { return data_; }
   QString        lastError() const      { return last_error_; }

   // ─── Генерализация ───────────────────────────────────────────────
   // Устанавливает алгоритм и сбрасывает параметры к дефолтным.
   // Если имя неизвестно — бросает std::invalid_argument.
   // Автоматически НЕ запускает пересчёт — вызывайте regenerate().
   void setAlgorithm(std::string_view name);

   // Меняет один параметр. Автоматически НЕ запускает пересчёт.
   void setParam(std::string_view name, double value);

   // Возвращает параметры к дефолтным значениям из spec текущего алгоритма.
   // Автоматически НЕ запускает пересчёт.
   void resetParams();

   // Прогоняет текущий алгоритм с текущими параметрами по всем слоям
   // и пересчитывает метрики. Синхронная операция.
   void regenerate();

   std::string                    currentAlgorithm() const;
   const gen::ParamSet&           currentParams()    const noexcept;
   const gen::ParamSpecs&         currentSpecs()     const noexcept;

   bool hasGeneralization() const noexcept;

   // Упрощённые фичи слоя. Пустой вектор, если пересчёт не выполнялся.
   const std::vector<gi::Feature>& simplified(int layer_index) const;

   // Метрики слоя. Значения по умолчанию (нули), если пересчёта не было.
   const metrics::SimplificationMetrics& layerMetrics(int layer_index) const;

signals:
   void stateChanged(State s);
   void dataChanged();
   void algorithmChanged(const QString& name);
   void paramsChanged();
   void generalizationDone();
   void errorOccurred(const QString& message);
   void statusMessage(const QString& message);

private:
   void setState(State s);
   void clearGeneralization();
   static double defaultParamValue(const gen::ParamSpec& spec);

   State   state_ = State::Idle;
   MapData data_;
   QString last_error_;

   std::unique_ptr<gen::GeneralizationAlgorithm> algorithm_;
   gen::ParamSet                                 params_;
   gen::ParamSpecs                               specs_;

   std::vector<std::vector<gi::Feature>>     simplified_layers_;
   std::vector<metrics::SimplificationMetrics> layer_metrics_;
};

} // namespace core