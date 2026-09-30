#include "mainwindow.h"

#include "map-renderer/layer_item.h"
#include "map-renderer/map_transform.h"

#include <QCoreApplication>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QStatusBar>

#include <algorithm>

MainWindow::MainWindow(const QString &shpPath, QWidget *parent) : QMainWindow(parent)
{
   buildUi();
   if (!shpPath.isEmpty())
      loadMap(shpPath);
}

void MainWindow::buildUi()
{
   setWindowTitle(tr("gen-optimizer"));
   resize(1280, 800);

   view_ = new QGraphicsView(this);
   view_->setRenderHint(QPainter::Antialiasing, true);
   view_->setDragMode(QGraphicsView::ScrollHandDrag);
   view_->setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
   view_->setViewportUpdateMode(QGraphicsView::SmartViewportUpdate);

   scene_ = new QGraphicsScene(this);
   view_->setScene(scene_);

   setCentralWidget(view_);
   statusBar()->showMessage(tr("ready"));
}

void MainWindow::loadMap(const QString &path)
{
   try
   {
      gi::Importer imp(path.toStdString());

      for (const auto &name : imp.layerNames())
      {
         auto features = imp.readLayer(name);
         auto bounds   = imp.layerBounds(name);

         if (!has_bounds_)
         {
            union_bounds_ = bounds;
            has_bounds_   = true;
         }
         else
         {
            union_bounds_.minX = std::min(union_bounds_.minX, bounds.minX);
            union_bounds_.minY = std::min(union_bounds_.minY, bounds.minY);
            union_bounds_.maxX = std::max(union_bounds_.maxX, bounds.maxX);
            union_bounds_.maxY = std::max(union_bounds_.maxY, bounds.maxY);
         }

         pending_layers_.push_back(
             {QString::fromStdString(name), std::move(features)});
      }

      if (pending_layers_.empty())
         statusBar()->showMessage(tr("no layers loaded"));
      else
         statusBar()->showMessage(tr("loaded %1 layer(s), waiting for viewport")
                                     .arg(pending_layers_.size()));
   }
   catch (const std::exception &e)
   {
      statusBar()->showMessage(tr("load failed: %1").arg(e.what()));
   }
}

void MainWindow::buildLayerItems()
{
   if (pending_layers_.empty() || !has_bounds_)
      return;

   const QRectF viewport = view_->viewport()->rect();
   const mr::MapTransform xf = mr::MapTransform::fit(union_bounds_, viewport);

   for (auto &layer : pending_layers_)
   {
      auto *item = new mr::LayerItem(std::move(layer.features), xf);
      scene_->addItem(item);
   }
   pending_layers_.clear();

   scene_->setSceneRect(scene_->itemsBoundingRect().adjusted(-20, -20, 20, 20));
}

void MainWindow::showEvent(QShowEvent *e)
{
   QMainWindow::showEvent(e);

   if (!fitted_ && scene_ && !pending_layers_.empty())
   {
      buildLayerItems();
      view_->fitInView(scene_->sceneRect(), Qt::KeepAspectRatio);
      fitted_ = true;
      statusBar()->showMessage(tr("map ready"));
   }
}