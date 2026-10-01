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
   explicit MainWindow(const QString& path,
                       const QString& filter = {},
                       QWidget* parent = nullptr);

protected:
   void showEvent(QShowEvent* e) override;

private:
   struct PendingLayer
   {
      gi::LayerInfo            info;
      std::vector<gi::Feature> features;
   };

   void buildUi();
   void loadPath(const QString& path);
   void buildLayerItems();

   QGraphicsScene* scene_ = nullptr;
   QGraphicsView*  view_  = nullptr;

   QString                   filter_;
   std::vector<PendingLayer> pending_layers_;
   gi::Bounds                union_bounds_{};
   bool                      has_bounds_       = false;
   bool                      fitted_           = false;
   int                       loaded_count_     = 0;
   int                       skipped_count_    = 0;
};