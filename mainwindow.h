#pragma once

#include <QMainWindow>
#include <QGraphicsScene>
#include <QGraphicsView>

#include <vector>
#include "map-renderer/map_transform.h"

namespace core { class Session; }
class DataPanel;
class AlgorithmPanel;
class StatusPanel;

class MainWindow : public QMainWindow
{
   Q_OBJECT

public:
   enum class ViewMode { Overlay, OriginalOnly, SimplifiedOnly };

   explicit MainWindow(core::Session* session, QWidget* parent = nullptr);

protected:
   void showEvent(QShowEvent* e) override;
   void keyPressEvent(QKeyEvent* e) override;
   bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
   void onDataChanged();
   void onGeneralizationDone();
   void onStatusMessage(const QString& message);

private:
   void buildUi();
   void rebuildScene();
   void updateSimplifiedLayers();
   void updateZoomLimits();
   void fitMap();
   void applyViewMode();

   core::Session*  session_  = nullptr;
   QGraphicsScene* scene_    = nullptr;
   QGraphicsView*  view_     = nullptr;

   DataPanel*      data_panel_ = nullptr;
   AlgorithmPanel* algo_panel_ = nullptr;
   StatusPanel*    status_panel_ = nullptr;

   struct LayerPair
   {
      QGraphicsItem* original   = nullptr;
      QGraphicsItem* simplified = nullptr;
   };
   std::vector<LayerPair> layer_items_;

   ViewMode mode_   = ViewMode::Overlay;
   bool     fitted_ = false;
   mr::MapTransform map_transform_;
   QRectF map_content_rect_;
   double minimum_scale_ = 1.0;
   bool zoom_limits_pending_ = false;
};
