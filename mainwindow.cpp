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
#include <QWheelEvent>
#include <QTimer>
#include <QAction>
#include <QToolBar>
#include <algorithm>
#include <cmath>

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
   connect(session_, &core::Session::paramsChanged, this,
           [this] { status_panel_->updateFrom(*session_); });
   connect(session_, &core::Session::algorithmChanged, this,
           [this] { status_panel_->updateFrom(*session_); });

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
   view_->setTransformationAnchor(QGraphicsView::NoAnchor);
   view_->setResizeAnchor(QGraphicsView::AnchorViewCenter);
   // Постоянный размер viewport: появление полос не меняет минимальный масштаб.
   // На масштабе всей карты полосы имеют нулевой диапазон и не перемещают карту.
   view_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
   view_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
   view_->setViewportUpdateMode(QGraphicsView::SmartViewportUpdate);
   view_->viewport()->installEventFilter(this);
   view_->setToolTip(tr("Mouse wheel: zoom. Drag with left mouse button: move map."));

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
   auto* fit_action = new QAction(tr("Fit map"), this);
   fit_action->setObjectName(QStringLiteral("fitMapAction"));
   fit_action->setShortcut(QKeySequence(Qt::Key_F));
   fit_action->setToolTip(tr("Show the entire map (F)"));
   connect(fit_action, &QAction::triggered, this, [this] {
      updateZoomLimits();
      fitMap();
   });
   auto* navigation = addToolBar(tr("Map navigation"));
   navigation->setMovable(false);
   navigation->addAction(fit_action);
   statusBar()->showMessage(tr("ready — O original, S simplified, A overlay, G regenerate"));
}

void MainWindow::onDataChanged()
{
   fitted_ = false;
   rebuildScene();
   data_panel_->updateFrom(*session_);
   status_panel_->updateFrom(*session_);
}

void MainWindow::onGeneralizationDone()
{
   updateSimplifiedLayers();
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
   map_content_rect_ = {};

   const auto& data = session_->data();
   if (!data.has_bounds || data.layers.empty())
      return;

   const QRectF viewport = view_->viewport()->rect();
   if (viewport.width() < 2 || viewport.height() < 2)
      return;

   // Координаты сцены зависят только от данных, а не от текущего размера окна.
   map_transform_ = mr::MapTransform::fit(data.union_bounds, QRectF(0, 0, 1000, 1000));
   const auto& xf = map_transform_;
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

   const QRectF content = scene_->itemsBoundingRect().adjusted(-20, -20, 20, 20);
   map_content_rect_ = content;
   // Скроллбары и перетаскивание ограничены картой, без искусственного пустого поля.
   scene_->setSceneRect(content);
   applyViewMode();

   if (!fitted_)
   {
      updateZoomLimits();
      fitMap();
      fitted_ = true;
   }
}

void MainWindow::updateSimplifiedLayers()
{
   // Оригинал, трансформация и границы сцены остаются прежними:
   // пересчёт не меняет скроллбары и положение камеры.
   if (layer_items_.size() != session_->data().layers.size())
      return;
   for (std::size_t i = 0; i < layer_items_.size(); ++i)
   {
      auto& pair = layer_items_[i];
      delete pair.simplified;
      pair.simplified = nullptr;
      if (session_->hasGeneralization())
      {
         mr::LayerStyle style{kSimplifiedColor, Qt::SolidLine, kSimplifiedZ};
         pair.simplified = new mr::LayerItem(session_->simplified(static_cast<int>(i)),
                                            map_transform_, style);
         scene_->addItem(pair.simplified);
      }
   }
   applyViewMode();
}

void MainWindow::fitMap()
{
   if (map_content_rect_.isEmpty())
      return;
   view_->setTransform(QTransform::fromScale(minimum_scale_, minimum_scale_));
   view_->centerOn(map_content_rect_.center());
}

void MainWindow::updateZoomLimits()
{
   if (map_content_rect_.isEmpty())
      return;
   const auto viewport = view_->viewport()->rect();
   if (viewport.width() <= 4 || viewport.height() <= 4)
      return;
   const bool was_fitted = !fitted_ || view_->transform().m11() <= minimum_scale_ * (1 + 1e-9);
   minimum_scale_ = std::min((viewport.width() - 4.0) / map_content_rect_.width(),
                             (viewport.height() - 4.0) / map_content_rect_.height());
   if (was_fitted || view_->transform().m11() < minimum_scale_)
      fitMap();
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event)
{
   if (watched == view_->viewport() && event->type() == QEvent::Resize && !zoom_limits_pending_)
   {
      // Дождаться раскладки панели и появления скроллбаров.
      zoom_limits_pending_ = true;
      QTimer::singleShot(0, this, [this] {
         zoom_limits_pending_ = false;
         updateZoomLimits();
      });
   }
   if (watched == view_->viewport() && event->type() == QEvent::Wheel)
   {
      auto* wheel = static_cast<QWheelEvent*>(event);
      const double delta = !wheel->angleDelta().isNull()
                              ? wheel->angleDelta().y() / 120.0
                              : wheel->pixelDelta().y() / 120.0;
      if (delta != 0.0 && !scene_->items().isEmpty())
      {
         const QPoint cursor = wheel->position().toPoint();
         const QPointF before = view_->mapToScene(cursor);
         const double current_scale = view_->transform().m11();
         const double target_scale = std::clamp(current_scale * std::pow(1.2, delta),
                                                minimum_scale_, minimum_scale_ * 1000.0);
         if (target_scale == minimum_scale_)
            fitMap();
         else
         {
            view_->scale(target_scale / current_scale, target_scale / current_scale);
            const QPointF after = view_->mapToScene(cursor);
            view_->centerOn(view_->mapToScene(view_->viewport()->rect().center()) + before - after);
         }
      }
      wheel->accept();
      return true;
   }
   return QMainWindow::eventFilter(watched, event);
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
      session_->regenerateAsync();
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
