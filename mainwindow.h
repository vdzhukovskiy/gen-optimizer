#pragma once

#include <QMainWindow>
#include <QGraphicsScene>
#include <QGraphicsView>

namespace core { class Session; }

class MainWindow : public QMainWindow
{
   Q_OBJECT

public:
   enum class ViewMode { Overlay, OriginalOnly, SimplifiedOnly };

   explicit MainWindow(core::Session* session, QWidget* parent = nullptr);

protected:
   void showEvent(QShowEvent* e) override;
   void keyPressEvent(QKeyEvent* e) override;

private slots:
   void onDataChanged();
   void onStatusMessage(const QString& message);

private:
   void buildUi();
   void rebuildScene();
   void applyViewMode();

   core::Session*  session_ = nullptr;
   QGraphicsScene* scene_   = nullptr;
   QGraphicsView*  view_    = nullptr;

   // Пара «оригинал / генерализация» на каждый слой. Оба указателя
   // могут быть null — до загрузки и после clear().
   struct LayerPair
   {
      QGraphicsItem* original   = nullptr;
      QGraphicsItem* simplified = nullptr;
   };
   std::vector<LayerPair> layer_items_;

   ViewMode mode_   = ViewMode::Overlay;
   bool     fitted_ = false;
};