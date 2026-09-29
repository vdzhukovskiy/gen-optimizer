#pragma once

#include <QPointF>
#include <QRectF>

#include "geometry-importer/importer.h"

namespace mr
{

class MapTransform
{
public:
   static MapTransform fit(const gi::Bounds& b, const QRectF& viewport);

   QPointF toScene(const gi::Point& p) const;
   QPointF toScene(double x, double y) const;

   double scale() const { return scale_; }

private:
   double scale_ = 1.0;
   double wx_ = 0.0;   // world center X
   double wy_ = 0.0;   // world center Y
   double sx_ = 0.0;   // scene center X
   double sy_ = 0.0;   // scene center Y
};

} // namespace mr