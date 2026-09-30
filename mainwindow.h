#pragma once

#include <QMainWindow>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QString>
#include <vector>

#include "geometry-importer/feature.h"
#include "geometry-importer/importer.h"

class MainWindow : public QMainWindow
{
public:
   explicit MainWindow(const QString& shpPath, QWidget* parent = nullptr);

protected:
   void showEvent(QShowEvent* e) override;

private:
   struct PendingLayer
   {
      QString                  name;
      std::vector<gi::Feature> features;
   };

   void buildUi();
   void loadMap(const QString& path);
   void buildLayerItems();

   QGraphicsScene* scene_ = nullptr;
   QGraphicsView*  view_  = nullptr;

   std::vector<PendingLayer> pending_layers_;
   gi::Bounds                union_bounds_{};
   bool                      has_bounds_ = false;
   bool                      fitted_     = false;
};