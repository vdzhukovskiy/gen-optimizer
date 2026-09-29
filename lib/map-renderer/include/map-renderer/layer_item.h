#pragma once

#include <QGraphicsItem>
#include <QPainterPath>
#include <vector>

#include "geometry-importer/feature.h"
#include "map-renderer/map_transform.h"

namespace mr
{

class LayerItem : public QGraphicsItem
{
public:
   LayerItem(std::vector<gi::Feature> features,
             MapTransform transform,
             QGraphicsItem* parent = nullptr);

   QRectF boundingRect() const override;
   void paint(QPainter* p,
              const QStyleOptionGraphicsItem* opt,
              QWidget* widget) override;

private:
   void rebuild();

   std::vector<gi::Feature>  features_;
   std::vector<QPainterPath> paths_;
   MapTransform              transform_;
   QRectF                    bounds_;
};

} // namespace mr