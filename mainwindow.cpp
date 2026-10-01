#include "mainwindow.h"

#include "map-renderer/layer_item.h"
#include "map-renderer/map_transform.h"

#include <QCoreApplication>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QStatusBar>

#include <algorithm>

MainWindow::MainWindow(const QString &path, const QString &filter, QWidget *parent)
    : QMainWindow(parent), filter_(filter)
{
   buildUi();
   if (!path.isEmpty())
      loadPath(path);
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

void MainWindow::loadPath(const QString &path)
{
   try
   {
      gi::Importer imp(path.toStdString());

      auto all_layers = imp.layers();

      // Фильтр по имени слоя (подстрока, без учёта регистра).
      if (!filter_.isEmpty())
      {
         std::vector<gi::LayerInfo> kept;
         kept.reserve(all_layers.size());
         for (auto &info : all_layers)
         {
            if (QString::fromStdString(info.name)
                    .contains(filter_, Qt::CaseInsensitive))
               kept.push_back(std::move(info));
         }
         all_layers = std::move(kept);
      }

      if (all_layers.empty())
      {
         statusBar()->showMessage(tr("no matching layers"));
         return;
      }

      const int total = static_cast<int>(all_layers.size());
      int index = 0;

      gi::Crs reference_crs;
      bool    crs_checked  = false;
      bool    crs_mismatch = false;

      for (auto &info : all_layers)
      {
         ++index;
         const QString short_name = QString::fromStdString(info.name);
         statusBar()->showMessage(
             tr("loading [%1/%2] %3...").arg(index).arg(total).arg(short_name));
         QCoreApplication::processEvents();

         std::vector<gi::Feature> features;
         try
         {
            features = imp.readLayer(info.name);
         }
         catch (const std::exception &e)
         {
            ++skipped_count_;
            statusBar()->showMessage(
                tr("skipped %1: %2").arg(short_name, e.what()));
            QCoreApplication::processEvents();
            continue;
         }

         if (!crs_checked)
         {
            reference_crs = info.crs;
            crs_checked   = true;
         }
         else if (!reference_crs.sameAs(info.crs))
         {
            crs_mismatch = true;
         }

         if (!has_bounds_)
         {
            union_bounds_ = info.bounds;
            has_bounds_   = true;
         }
         else
         {
            union_bounds_.minX = std::min(union_bounds_.minX, info.bounds.minX);
            union_bounds_.minY = std::min(union_bounds_.minY, info.bounds.minY);
            union_bounds_.maxX = std::max(union_bounds_.maxX, info.bounds.maxX);
            union_bounds_.maxY = std::max(union_bounds_.maxY, info.bounds.maxY);
         }

         pending_layers_.push_back({std::move(info), std::move(features)});
         ++loaded_count_;
      }

      if (pending_layers_.empty())
      {
         statusBar()->showMessage(tr("nothing loaded"));
         return;
      }

      const auto &first_crs = pending_layers_.front().info.crs;
      const QString units = first_crs.units.empty()
                                ? tr("unknown units")
                                : QString::fromStdString(first_crs.units);
      const QString crs_label = first_crs.epsg != 0
                                    ? QStringLiteral("EPSG:%1").arg(first_crs.epsg)
                                    : (first_crs.isValid() ? tr("custom") : tr("no CRS"));

      QString msg = tr("loaded %1 layer(s), skipped %2, CRS %3 (%4)")
                        .arg(loaded_count_)
                        .arg(skipped_count_)
                        .arg(crs_label, units);
      if (crs_mismatch)
         msg += tr(" [WARNING: layer CRS mismatch]");

      statusBar()->showMessage(msg);
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
      statusBar()->showMessage(
          tr("map ready: %1 layers").arg(loaded_count_));
   }
}