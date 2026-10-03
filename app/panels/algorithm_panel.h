#pragma once

#include <QWidget>
#include <QDoubleSpinBox>
#include <QSlider>

#include <string>
#include <vector>

#include "generalization/params.h"

class QComboBox;
class QFormLayout;
class QLabel;
class QPushButton;
namespace core { class Session; }

// Панель выбора алгоритма и управления его параметрами.
// Виджеты параметров генерируются из ParamSpecs — панель не знает о
// конкретных алгоритмах. Пересчёт запускается только явно.
class AlgorithmPanel : public QWidget
{
   Q_OBJECT

public:
   explicit AlgorithmPanel(core::Session* session, QWidget* parent = nullptr);

private slots:
   void onAlgorithmComboChanged(int index);
   void onSessionAlgorithmChanged(const QString& name);
   void onSessionParamsChanged();
   void onResetClicked();
   void onGenerateClicked();
   void onSpinChanged(int row, double value);
   void onSliderChanged(int row, int pos);

private:
   struct ParamRow
   {
      gen::ParamSpec   spec;
      QDoubleSpinBox*  spin   = nullptr;
      QSlider*         slider = nullptr;
   };

   void rebuildRows();
   void syncFromSession();
   void setRowValue(int row, double value, bool from_spin);

   double sliderToValue(const gen::ParamSpec& spec, int pos) const;
   int    valueToSlider(const gen::ParamSpec& spec, double v) const;

   core::Session*  session_ = nullptr;
   QComboBox*      combo_   = nullptr;
   QWidget*        rows_host_ = nullptr;
   QFormLayout*    rows_layout_ = nullptr;
   QPushButton*    reset_   = nullptr;
   QPushButton*    generate_ = nullptr;
   QLabel*         hint_    = nullptr;

   std::vector<ParamRow> rows_;
   bool updating_ = false;   // защита от рекурсии при программной установке
};
