#pragma once

#include <QWidget>

class QLabel;
namespace core { class Session; }

// Отображает характеристики загруженного набора: CRS, число слоёв,
// суммарные фичи и вершины. Обновляется по запросу из MainWindow.
class DataPanel : public QWidget
{
   Q_OBJECT

public:
   explicit DataPanel(QWidget* parent = nullptr);

   void updateFrom(const core::Session& session);

private:
   QLabel* source_   = nullptr;
   QLabel* crs_      = nullptr;
   QLabel* layers_   = nullptr;
   QLabel* features_ = nullptr;
   QLabel* vertices_ = nullptr;
};