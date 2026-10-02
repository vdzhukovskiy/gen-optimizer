#include "mainwindow.h"
#include "core/session.h"

#include "map-renderer/layer_item.h"
#include "map-renderer/map_transform.h"

#include <QPainter>
#include <QStatusBar>

MainWindow::MainWindow(core::Session* session, QWidget* parent)
    : QMainWindow(parent), session_(session)
{
   buildUi();

   connect(session_, &core::Session::dataChanged,
           this,     &MainWindow::onDataChanged);
   connect(session_, &core::Session::statusMessage,
           this,     &MainWindow::onStatusMessage);
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
   scene_->setBackgroundBrush(Qt::white);
   scene_->setSceneRect(0, 0, 1, 1);
   view_->setScene(scene_);

   setCentralWidget(view_);
   statusBar()->showMessage(tr("ready"));
}

void MainWindow::onDataChanged()
{
   rebuildScene();
}

void MainWindow::onStatusMessage(const QString& message)
{
   statusBar()->showMessage(message);
}

void MainWindow::rebuildScene()
{
   scene_->clear();

   const auto& data = session_->data();
   if (!data.has_bounds || data.layers.empty())
      return;

   const QRectF viewport = view_->viewport()->rect();
   if (viewport.width() < 2 || viewport.height() < 2)
      return;   // окно ещё не показано; showEvent вызовет повторно

   const mr::MapTransform xf = mr::MapTransform::fit(data.union_bounds, viewport);

   for (const auto& layer : data.layers)
   {
      auto* item = new mr::LayerItem(layer.features, xf);
      scene_->addItem(item);
   }

   scene_->setSceneRect(scene_->itemsBoundingRect().adjusted(-20, -20, 20, 20));

   if (!fitted_)   // ← фикс скроллбара: подгоняем вьюпорт после первой загрузки
   {
      view_->fitInView(scene_->sceneRect(), Qt::KeepAspectRatio);
      fitted_ = true;
   }
}

void MainWindow::showEvent(QShowEvent* e)
{
   QMainWindow::showEvent(e);

   if (scene_->items().isEmpty() && !session_->data().layers.empty())
      rebuildScene();
}