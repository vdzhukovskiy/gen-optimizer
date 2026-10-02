#pragma once

#include <QColor>
#include <QGraphicsItem>
#include <QPainterPath>
#include <Qt>
#include <vector>

#include "geometry-importer/feature.h"
#include "map-renderer/map_transform.h"

namespace mr
{

struct LayerStyle
{
   QColor       color      = Qt::black;
   Qt::PenStyle pen_style  = Qt::SolidLine;
   int          z_value    = 0;
};

class LayerItem : public QGraphicsItem
{
public:
   LayerItem(std::vector<gi::Feature> features,
             MapTransform transform,
             LayerStyle style,
             QGraphicsItem* parent = nullptr);

   void setFeatures(std::vector<gi::Feature> features);
   void setTransform(MapTransform transform);
   void setStyle(LayerStyle style);

   QRectF boundingRect() const override;
   void paint(QPainter* p,
              const QStyleOptionGraphicsItem* opt,
              QWidget* widget) override;

private:
   void rebuild();

   std::vector<gi::Feature>  features_;
   std::vector<QPainterPath> paths_;
   MapTransform              transform_;
   LayerStyle                style_;
   QRectF                    bounds_;
};

} // namespace mr