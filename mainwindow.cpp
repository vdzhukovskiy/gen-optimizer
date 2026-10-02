#include "mainwindow.h"
#include "core/session.h"
#include "app/panels/data_panel.h"
#include "app/panels/algorithm_panel.h"
#include "app/panels/status_panel.h"

#include "map-renderer/layer_item.h"
#include "map-renderer/map_transform.h"

#include <QKeyEvent>
#include <QPainter>
#include <QSplitter>
#include <QStatusBar>
#include <QVBoxLayout>

namespace
{

constexpr QColor kOriginalColor   = QColor(0, 0, 0);
constexpr QColor kSimplifiedColor = QColor(255, 0, 255);
constexpr int    kOriginalZ       = 0;
constexpr int    kSimplifiedZ     = 1;

} // namespace

MainWindow::MainWindow(core::Session* session, QWidget* parent)
    : QMainWindow(parent), session_(session)
{
   buildUi();

   connect(session_, &core::Session::dataChanged,
           this,     &MainWindow::onDataChanged);
   connect(session_, &core::Session::generalizationDone,
           this,     &MainWindow::onGeneralizationDone);
   connect(session_, &core::Session::statusMessage,
           this,     &MainWindow::onStatusMessage);

   data_panel_->updateFrom(*session_);
   status_panel_->updateFrom(*session_);
}

void MainWindow::buildUi()
{
   setWindowTitle(tr("gen-optimizer — O original, S simplified, A overlay, G regenerate"));
   resize(1400, 860);

   view_ = new QGraphicsView(this);
   view_->setRenderHint(QPainter::Antialiasing, true);
   view_->setDragMode(QGraphicsView::ScrollHandDrag);
   view_->setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
   view_->setViewportUpdateMode(QGraphicsView::SmartViewportUpdate);

   scene_ = new QGraphicsScene(this);
   scene_->setBackgroundBrush(Qt::white);
   scene_->setSceneRect(0, 0, 1, 1);
   view_->setScene(scene_);

   data_panel_   = new DataPanel(this);
   algo_panel_   = new AlgorithmPanel(session_, this);
   status_panel_ = new StatusPanel(this);

   auto* right = new QWidget(this);
   auto* rl = new QVBoxLayout(right);
   rl->setContentsMargins(0, 0, 0, 0);
   rl->addWidget(data_panel_);
   rl->addWidget(algo_panel_);
   rl->addWidget(status_panel_, 1);

   auto* split = new QSplitter(Qt::Horizontal, this);
   split->addWidget(view_);
   split->addWidget(right);
   split->setStretchFactor(0, 1);
   split->setStretchFactor(1, 0);
   split->setSizes({1000, 380});

   setCentralWidget(split);
   statusBar()->showMessage(tr("ready — O original, S simplified, A overlay, G regenerate"));
}

void MainWindow::onDataChanged()
{
   rebuildScene();
   data_panel_->updateFrom(*session_);
}

void MainWindow::onGeneralizationDone()
{
   rebuildScene();
   status_panel_->updateFrom(*session_);
}

void MainWindow::onStatusMessage(const QString& message)
{
   statusBar()->showMessage(message);
}

void MainWindow::rebuildScene()
{
   scene_->clear();
   layer_items_.clear();

   const auto& data = session_->data();
   if (!data.has_bounds || data.layers.empty())
      return;

   const QRectF viewport = view_->viewport()->rect();
   if (viewport.width() < 2 || viewport.height() < 2)
      return;

   const mr::MapTransform xf = mr::MapTransform::fit(data.union_bounds, viewport);
   const bool has_gen = session_->hasGeneralization();

   layer_items_.reserve(data.layers.size());

   for (int i = 0; i < static_cast<int>(data.layers.size()); ++i)
   {
      LayerPair pair;

      mr::LayerStyle orig_style{kOriginalColor, Qt::SolidLine, kOriginalZ};
      pair.original = new mr::LayerItem(data.layers[i].features, xf, orig_style);
      scene_->addItem(pair.original);

      if (has_gen)
      {
         mr::LayerStyle gen_style{kSimplifiedColor, Qt::SolidLine, kSimplifiedZ};
         pair.simplified = new mr::LayerItem(session_->simplified(i), xf, gen_style);
         scene_->addItem(pair.simplified);
      }

      layer_items_.push_back(pair);
   }

   scene_->setSceneRect(scene_->itemsBoundingRect().adjusted(-20, -20, 20, 20));
   applyViewMode();

   if (!fitted_)
   {
      view_->fitInView(scene_->sceneRect(), Qt::KeepAspectRatio);
      fitted_ = true;
   }
}

void MainWindow::applyViewMode()
{
   const bool show_orig = (mode_ == ViewMode::Overlay || mode_ == ViewMode::OriginalOnly);
   const bool show_gen  = (mode_ == ViewMode::Overlay || mode_ == ViewMode::SimplifiedOnly);

   for (auto& p : layer_items_)
   {
      if (p.original)   p.original->setVisible(show_orig);
      if (p.simplified) p.simplified->setVisible(show_gen);
   }
}

void MainWindow::keyPressEvent(QKeyEvent* e)
{
   switch (e->key())
   {
   case Qt::Key_O:
      mode_ = ViewMode::OriginalOnly;
      applyViewMode();
      statusBar()->showMessage(tr("view: original only"));
      return;
   case Qt::Key_S:
      mode_ = ViewMode::SimplifiedOnly;
      applyViewMode();
      statusBar()->showMessage(tr("view: simplified only"));
      return;
   case Qt::Key_A:
      mode_ = ViewMode::Overlay;
      applyViewMode();
      statusBar()->showMessage(tr("view: overlay"));
      return;
   case Qt::Key_G:
      session_->regenerate();
      return;
   default:
      QMainWindow::keyPressEvent(e);
   }
}

void MainWindow::showEvent(QShowEvent* e)
{
   QMainWindow::showEvent(e);
   if (scene_->items().isEmpty() && !session_->data().layers.empty())
      rebuildScene();
}