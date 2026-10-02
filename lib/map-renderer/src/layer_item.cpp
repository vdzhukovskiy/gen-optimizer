#include "map-renderer/layer_item.h"

#include <QPainter>
#include <QStyleOptionGraphicsItem>
#include <type_traits>

namespace mr
{
namespace
{

void appendPoint(QPainterPath &path, const MapTransform &t, const gi::Point &p)
{
   path.addEllipse(t.toScene(p), 1.5, 1.5);
}

void appendLineString(QPainterPath &path, const MapTransform &t, const gi::LineString &ls)
{
   if (ls.empty())
      return;
   path.moveTo(t.toScene(ls.front()));
   for (std::size_t i = 1; i < ls.size(); ++i)
      path.lineTo(t.toScene(ls[i]));
}

void appendRing(QPainterPath &path, const MapTransform &t, const gi::LinearRing &ring)
{
   if (ring.empty())
      return;
   path.moveTo(t.toScene(ring.front()));
   for (std::size_t i = 1; i < ring.size(); ++i)
      path.lineTo(t.toScene(ring[i]));
   path.closeSubpath();
}

void appendPolygon(QPainterPath &path, const MapTransform &t, const gi::Polygon &poly)
{
   if (poly.empty())
      return;
   appendRing(path, t, poly.front());
   for (std::size_t i = 1; i < poly.size(); ++i)
      appendRing(path, t, poly[i]);
}

QPainterPath buildPath(const gi::Geometry &g, const MapTransform &t)
{
   QPainterPath path;
   std::visit(
      [&](const auto &v)
      {
         using T = std::decay_t<decltype(v)>;
         if constexpr (std::is_same_v<T, gi::Point>)
            appendPoint(path, t, v);
         else if constexpr (std::is_same_v<T, gi::LineString>)
            appendLineString(path, t, v);
         else if constexpr (std::is_same_v<T, gi::Polygon>)
            appendPolygon(path, t, v);
         else if constexpr (std::is_same_v<T, gi::MultiPoint>)
            for (const auto &p : v.points)
               appendPoint(path, t, p);
         else if constexpr (std::is_same_v<T, gi::MultiLine>)
            for (const auto &l : v.lines)
               appendLineString(path, t, l);
         else if constexpr (std::is_same_v<T, gi::MultiPolygon>)
            for (const auto &p : v.polys)
               appendPolygon(path, t, p);
      },
      g);
   return path;
}

} // namespace

LayerItem::LayerItem(std::vector<gi::Feature> features, MapTransform transform,
                     LayerStyle style, QGraphicsItem *parent)
    : QGraphicsItem(parent),
      features_(std::move(features)),
      transform_(transform),
      style_(style)
{
   setZValue(style_.z_value);
   rebuild();
}

void LayerItem::setStyle(LayerStyle style)
{
   style_ = style;
   setZValue(style_.z_value);
   update();
}

void LayerItem::paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *)
{
   p->setRenderHint(QPainter::Antialiasing, true);
   p->setPen(QPen(style_.color, 0, style_.pen_style));
   p->setBrush(Qt::NoBrush);

   for (const auto &path : paths_)
      p->drawPath(path);
}

void LayerItem::setFeatures(std::vector<gi::Feature> features)
{
   prepareGeometryChange();
   features_ = std::move(features);
   rebuild();
   update();
}

void LayerItem::setTransform(MapTransform transform)
{
   prepareGeometryChange();
   transform_ = transform;
   rebuild();
   update();
}

void LayerItem::rebuild()
{
   paths_.clear();
   paths_.reserve(features_.size());

   QRectF total;
   bool first = true;

   for (const auto &f : features_)
   {
      QPainterPath p = buildPath(f.geometry, transform_);
      if (p.isEmpty())
         continue;
      total = first ? p.boundingRect() : total.united(p.boundingRect());
      first = false;
      paths_.push_back(std::move(p));
   }

   bounds_ = first ? QRectF{} : total.adjusted(-2, -2, 2, 2);
}

QRectF LayerItem::boundingRect() const
{
   return bounds_;
}

} // namespace mr