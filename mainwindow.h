#pragma once

#include <QMainWindow>
#include <QGraphicsScene>
#include <QGraphicsView>

class MainWindow : public QMainWindow
{
public:
   explicit MainWindow(const QString& shpPath, QWidget* parent = nullptr);

protected:
   void showEvent(QShowEvent* e) override;

private:
   void buildUi();
   void loadMap(const QString& path);

   QGraphicsScene* scene_ = nullptr;
   QGraphicsView*  view_  = nullptr;
   bool            fitted_ = false;
};