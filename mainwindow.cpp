#include "mainwindow.h"

#include "geometry-importer/importer.h"
#include "map-renderer/layer_item.h"
#include "map-renderer/map_transform.h"

#include <QCoreApplication>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QStatusBar>

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

      const QRectF viewport(0, 0, 1280, 800);

      for (const auto &name : imp.layerNames())
      {
         auto features = imp.readLayer(name);
         auto bounds = imp.layerBounds(name);
         auto xf = mr::MapTransform::fit(bounds, viewport);

         auto *item = new mr::LayerItem(std::move(features), xf);
         scene_->addItem(item);
         statusBar()->showMessage(tr("loaded layer %1: %2 features")
                                     .arg(QString::fromStdString(name))
                                     .arg(item->boundingRect().width(), 0, 'f', 0));
      }

      scene_->setSceneRect(scene_->itemsBoundingRect().adjusted(-20, -20, 20, 20));
   }
   catch (const std::exception &e)
   {
      statusBar()->showMessage(tr("load failed: %1").arg(e.what()));
   }
}

void MainWindow::showEvent(QShowEvent* e)
{
   QMainWindow::showEvent(e);
   if (!fitted_ && scene_ && !scene_->items().isEmpty())
   {
      view_->fitInView(scene_->sceneRect(), Qt::KeepAspectRatio);
      fitted_ = true;
   }
}