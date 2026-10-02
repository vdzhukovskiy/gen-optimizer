#pragma once

#include <QMainWindow>
#include <QGraphicsScene>
#include <QGraphicsView>

namespace core { class Session; }

class MainWindow : public QMainWindow
{
   Q_OBJECT

public:
   explicit MainWindow(core::Session* session, QWidget* parent = nullptr);

protected:
   void showEvent(QShowEvent* e) override;

private slots:
   void onDataChanged();
   void onStatusMessage(const QString& message);

private:
   void buildUi();
   void rebuildScene();

   core::Session*  session_ = nullptr;
   QGraphicsScene* scene_   = nullptr;
   QGraphicsView*  view_    = nullptr;

   bool fitted_ = false;
};