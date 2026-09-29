#include "map-renderer/map_transform.h"

#include <algorithm>

namespace mr
{

MapTransform MapTransform::fit(const gi::Bounds& b, const QRectF& vp)
{
   MapTransform t;

   const double w = b.maxX - b.minX;
   const double h = b.maxY - b.minY;
   if (w <= 0.0 || h <= 0.0 || vp.width() <= 0.0 || vp.height() <= 0.0)
      return t;

   t.scale_ = std::min(vp.width() / w, vp.height() / h);
   t.wx_ = (b.minX + b.maxX) * 0.5;
   t.wy_ = (b.minY + b.maxY) * 0.5;
   t.sx_ = vp.center().x();
   t.sy_ = vp.center().y();
   return t;
}

QPointF MapTransform::toScene(const gi::Point& p) const
{
   return toScene(p.x, p.y);
}

QPointF MapTransform::toScene(double x, double y) const
{
   return {
       sx_ + (x - wx_) * scale_,
       sy_ - (y - wy_) * scale_   // Y-flip: scene Y растёт вниз, карта — вверх
   };
}

} // namespace mr