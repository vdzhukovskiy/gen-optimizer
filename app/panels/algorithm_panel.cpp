#include "app/panels/algorithm_panel.h"
#include "core/session.h"

#include "generalization/algorithm.h"

#include <QComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace
{

constexpr int    kSliderSteps        = 1000;
constexpr int    kDebounceMs         = 200;

int decimalsFor(double step)
{
   if (step <= 0.0) return 4;
   if (step >= 1.0) return 1;
   if (step >= 0.1) return 2;
   if (step >= 0.01) return 3;
   return 4;
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

   hint_ = new QLabel(tr("<i>Changes recompute automatically.</i>"));
   hint_->setWordWrap(true);

   debounce_ = new QTimer(this);
   debounce_->setSingleShot(true);
   debounce_->setInterval(kDebounceMs);

   auto* root = new QVBoxLayout(this);
   root->setContentsMargins(8, 8, 8, 8);
   root->addWidget(title);
   root->addWidget(combo_);
   root->addWidget(rows_host_);
   root->addWidget(reset_);
   root->addWidget(hint_);
   root->addStretch(1);

   connect(combo_, &QComboBox::currentIndexChanged,
           this, &AlgorithmPanel::onAlgorithmComboChanged);
   connect(reset_, &QPushButton::clicked,
           this, &AlgorithmPanel::onResetClicked);
   connect(debounce_, &QTimer::timeout,
           this, &AlgorithmPanel::onDebounceTimeout);

   connect(session_, &core::Session::algorithmChanged,
           this, &AlgorithmPanel::onSessionAlgorithmChanged);
   connect(session_, &core::Session::paramsChanged,
           this, &AlgorithmPanel::onSessionParamsChanged);

   // Session уже мог установить алгоритм по умолчанию до нашего подключения.
   rebuildRows();
   syncFromSession();
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
   debounce_->start(kDebounceMs);
}

void AlgorithmPanel::onDebounceTimeout()
{
   session_->regenerate();
}

void AlgorithmPanel::rebuildRows()
{
   // Очищаем форму.
   while (rows_layout_->rowCount() > 0)
   {
      auto item = rows_layout_->takeAt(0);
      if (item->layout())
      {
         while (item->layout()->count() > 0)
            delete item->layout()->takeAt(0)->widget();
         delete item->layout();
      }
      delete item;
   }
   rows_.clear();

   const auto& specs = session_->currentSpecs();
   rows_.reserve(specs.size());

   for (const auto& spec : specs)
   {
      ParamRow row;
      row.spec = spec;

      row.spin = new QDoubleSpinBox;
      row.spin->setRange(spec.min_value, spec.max_value);
      row.spin->setDecimals(decimalsFor(spec.step));
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
   debounce_->start(kDebounceMs);
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