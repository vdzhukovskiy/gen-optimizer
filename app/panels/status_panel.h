#pragma once

#include <QWidget>

class QLabel;
class QTableWidget;
namespace core { class Session; }

class StatusPanel : public QWidget
{
   Q_OBJECT

public:
   explicit StatusPanel(QWidget* parent = nullptr);

   void updateFrom(const core::Session& session);

private:
   QLabel*        summary_ = nullptr;
   QTableWidget*  table_   = nullptr;
};