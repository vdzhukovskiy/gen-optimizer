#include "app/panels/algorithm_panel.h"

#include "core/session.h"
#include "generalization/algorithm.h"

#include <QComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace
{

constexpr int    kSliderSteps        = 1000;

int decimalsFor(double step)
{
   if (!std::isfinite(step) || step <= 0.0)
      return 4;
   // Дополнительные знаки сохраняют промежуточные значения лог-слайдера.
   return std::clamp(static_cast<int>(std::ceil(-std::log10(step))) + 2, 4, 15);
}

} // namespace

AlgorithmPanel::AlgorithmPanel(core::Session* session, QWidget* parent)
    : QWidget(parent), session_(session)
{
   auto* title = new QLabel(tr("<b>Algorithm</b>"));

   combo_ = new QComboBox;
   for (const auto& name : gen::availableAlgorithms())
      combo_->addItem(QString::fromStdString(name));
   {
      const QString current = QString::fromStdString(session_->currentAlgorithm());
      const int idx = combo_->findText(current);
      if (idx >= 0) combo_->setCurrentIndex(idx);
   }

   rows_host_ = new QWidget;
   rows_layout_ = new QFormLayout(rows_host_);
   rows_layout_->setContentsMargins(0, 0, 0, 0);

   reset_ = new QPushButton(tr("Reset params"));
   generate_ = new QPushButton(tr("Generate"));
   generate_->setObjectName(QStringLiteral("generateButton"));
   cancel_ = new QPushButton(tr("Cancel"));
   cancel_->setObjectName(QStringLiteral("cancelButton"));
   progress_ = new QProgressBar;
   progress_->setRange(0, 1000);
   progress_->setValue(0);
   progress_label_ = new QLabel(tr("No calculation running"));
   progress_label_->setWordWrap(true);

   hint_ = new QLabel(
      tr("<i>Generate or G applies settings. During calculation, Restart queues a new run.</i>"));
   hint_->setWordWrap(true);

   auto* root = new QVBoxLayout(this);
   root->setContentsMargins(8, 8, 8, 8);
   root->addWidget(title);
   root->addWidget(combo_);
   root->addWidget(rows_host_);
   root->addWidget(reset_);
   root->addWidget(generate_);
   root->addWidget(cancel_);
   root->addWidget(progress_);
   root->addWidget(progress_label_);
   root->addWidget(hint_);
   root->addStretch(1);

   connect(combo_, &QComboBox::currentIndexChanged,
           this, &AlgorithmPanel::onAlgorithmComboChanged);
   connect(reset_, &QPushButton::clicked,
           this, &AlgorithmPanel::onResetClicked);
   connect(generate_, &QPushButton::clicked,
           this, &AlgorithmPanel::onGenerateClicked);
   connect(cancel_, &QPushButton::clicked, session_, &core::Session::cancelGeneralization);

   connect(session_, &core::Session::algorithmChanged,
           this, &AlgorithmPanel::onSessionAlgorithmChanged);
   connect(session_, &core::Session::paramsChanged,
           this, &AlgorithmPanel::onSessionParamsChanged);

   connect(session_, &core::Session::generalizationStarted, this,
           [this]
           {
              progress_->setValue(0);
              progress_label_->setText(tr("Starting calculation..."));
              updateControls();
           });
   connect(session_, &core::Session::generalizationCancellationRequested, this,
           [this]
           {
              progress_label_->setText(session_->hasPendingRestart()
                                          ? tr("Cancelling... New calculation queued")
                                          : tr("Cancelling..."));
              updateControls();
           });
   connect(session_, &core::Session::generalizationProgressChanged, this,
           [this](qint64 completed, qint64 total, const QString &layer, const QString &phase,
                  qint64 layer_completed, qint64 layer_total)
           {
              progress_->setValue(total > 0 ? static_cast<int>(1000.0 * completed / total) : 0);
              const QString phase_label = phase == QStringLiteral("Simplifying")
                                             ? tr("Simplifying")
                                             : tr("Evaluating metrics");
              progress_label_->setText(tr("%1 — %2: %3 / %4 features")
                                          .arg(layer, phase_label)
                                          .arg(layer_completed)
                                          .arg(layer_total));
           });
   connect(session_, &core::Session::generalizationDone, this,
           [this]
           {
              progress_->setValue(1000);
              progress_label_->setText(tr("Calculation complete"));
           });
   connect(session_, &core::Session::generalizationCancelled, this,
           [this]
           {
              progress_label_->setText(session_->hasGeneralization()
                                          ? tr("Cancelled — previous result retained")
                                          : tr("Cancelled"));
           });
   connect(session_, &core::Session::errorOccurred, this, [this](const QString &message)
           { progress_label_->setText(tr("Error: %1").arg(message)); });
   connect(session_, &core::Session::generalizationFinished, this, &AlgorithmPanel::updateControls);
   connect(session_, &core::Session::stateChanged, this, &AlgorithmPanel::updateControls);
   connect(session_, &core::Session::dataChanged, this,
           [this]
           {
              progress_->setValue(0);
              progress_label_->setText(tr("No calculation running"));
              updateControls();
           });
   updateControls();

   // Session уже мог установить алгоритм по умолчанию до нашего подключения.
   rebuildRows();
   syncFromSession();
}

void AlgorithmPanel::updateControls()
{
   const bool ready = session_->state() == core::Session::State::Ready;
   generate_->setEnabled(ready && !session_->data().layers.empty());
   generate_->setText(session_->isGeneralizing() ? tr("Restart") : tr("Generate"));
   cancel_->setEnabled(session_->isGeneralizing() &&
                       (!session_->isCancelling() || session_->hasPendingRestart()));
   cancel_->setText(session_->isCancelling() ? tr("Cancelling...") : tr("Cancel"));
}

void AlgorithmPanel::onAlgorithmComboChanged(int index)
{
   if (updating_ || index < 0)
      return;
   const QString name = combo_->itemText(index);
   session_->setAlgorithm(name.toStdString());
}

void AlgorithmPanel::onSessionAlgorithmChanged(const QString& name)
{
   updating_ = true;
   const int idx = combo_->findText(name);
   if (idx >= 0 && idx != combo_->currentIndex())
      combo_->setCurrentIndex(idx);
   updating_ = false;

   rebuildRows();
   syncFromSession();
}

void AlgorithmPanel::onSessionParamsChanged()
{
   syncFromSession();
}

void AlgorithmPanel::onResetClicked()
{
   session_->resetParams();
}

void AlgorithmPanel::onGenerateClicked()
{
   session_->regenerateAsync();
}

void AlgorithmPanel::rebuildRows()
{
   // Очищаем форму.
   while (rows_layout_->rowCount() > 0)
      rows_layout_->removeRow(0);
   rows_.clear();

   const auto& specs = session_->currentSpecs();
   rows_.reserve(specs.size());

   for (const auto& spec : specs)
   {
      ParamRow row;
      row.spec = spec;

      row.spin = new QDoubleSpinBox;
      row.spin->setDecimals(decimalsFor(spec.step));
      row.spin->setRange(spec.min_value, spec.max_value);
      row.spin->setSingleStep(spec.step);
      row.spin->setKeyboardTracking(false);

      row.slider = new QSlider(Qt::Horizontal);
      row.slider->setRange(0, kSliderSteps);

      auto* host = new QWidget;
      auto* hl = new QVBoxLayout(host);
      hl->setContentsMargins(0, 0, 0, 0);
      hl->setSpacing(2);
      hl->addWidget(row.spin);
      hl->addWidget(row.slider);

      rows_layout_->addRow(QString::fromStdString(spec.name), host);

      const int idx = static_cast<int>(rows_.size());
      connect(row.spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
              this, [this, idx](double v) { onSpinChanged(idx, v); });
      connect(row.slider, &QSlider::valueChanged,
              this, [this, idx](int p) { onSliderChanged(idx, p); });

      rows_.push_back(row);
   }
}

void AlgorithmPanel::syncFromSession()
{
   updating_ = true;
   const auto& params = session_->currentParams();
   for (int i = 0; i < static_cast<int>(rows_.size()); ++i)
   {
      const auto& spec = rows_[i].spec;
      auto it = params.find(spec.name);
      const double value = (it != params.end()) ? it->second
                                                : spec.min_value;
      rows_[i].spin->setValue(value);
      rows_[i].slider->setValue(valueToSlider(spec, value));
   }
   updating_ = false;
}

void AlgorithmPanel::setRowValue(int row, double value, bool from_spin)
{
   if (row < 0 || row >= static_cast<int>(rows_.size()))
      return;

   const auto& spec = rows_[row].spec;
   value = std::clamp(value, spec.min_value, spec.max_value);

   updating_ = true;
   if (from_spin)
      rows_[row].slider->setValue(valueToSlider(spec, value));
   else
      rows_[row].spin->setValue(value);
   updating_ = false;

   session_->setParam(spec.name, value);
}

void AlgorithmPanel::onSpinChanged(int row, double value)
{
   if (updating_) return;
   setRowValue(row, value, /*from_spin=*/true);
}

void AlgorithmPanel::onSliderChanged(int row, int pos)
{
   if (updating_) return;
   const double v = sliderToValue(rows_[row].spec, pos);
   setRowValue(row, v, /*from_spin=*/false);
}

double AlgorithmPanel::sliderToValue(const gen::ParamSpec& spec, int pos) const
{
   const double t = static_cast<double>(pos) / kSliderSteps;
   if (spec.logarithmic && spec.min_value > 0.0 && spec.max_value > 0.0)
      return spec.min_value * std::pow(spec.max_value / spec.min_value, t);
   return spec.min_value + t * (spec.max_value - spec.min_value);
}

int AlgorithmPanel::valueToSlider(const gen::ParamSpec& spec, double v) const
{
   if (spec.max_value <= spec.min_value)
      return 0;
   double t = 0.0;
   if (spec.logarithmic && spec.min_value > 0.0 && v > 0.0)
      t = std::log(v / spec.min_value) /
          std::log(spec.max_value / spec.min_value);
   else
      t = (v - spec.min_value) / (spec.max_value - spec.min_value);
   return std::clamp(static_cast<int>(t * kSliderSteps), 0, kSliderSteps);
}
