#include "core/session.h"

#include "geometry-importer/importer.h"

#include <algorithm>
#include <exception>

#include <QCoreApplication>

namespace core
{

Session::Session(QObject* parent) : QObject(parent) {}
Session::~Session() = default;

void Session::loadPath(const QString& path, const QString& filter)
{
   setState(State::Loading);
   emit statusMessage(tr("loading %1...").arg(path));

   data_ = MapData{};
   last_error_.clear();

   try
   {
      gi::Importer imp(path.toStdString());
      auto layers = imp.layers();

      if (!filter.isEmpty())
      {
         std::vector<gi::LayerInfo> kept;
         kept.reserve(layers.size());
         for (auto& info : layers)
            if (QString::fromStdString(info.name)
                    .contains(filter, Qt::CaseInsensitive))
               kept.push_back(std::move(info));
         layers = std::move(kept);
      }

      if (layers.empty())
      {
         setState(State::Ready);
         emit statusMessage(tr("no matching layers"));
         emit dataChanged();
         return;
      }

      const int total   = static_cast<int>(layers.size());
      int       index   = 0;
      int       skipped = 0;

      bool crs_checked  = false;
      bool crs_mismatch = false;

      for (auto& info : layers)
      {
         ++index;
         emit statusMessage(tr("loading [%1/%2] %3...")
                                .arg(index).arg(total)
                                .arg(QString::fromStdString(info.name)));
         QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

         std::vector<gi::Feature> features;
         try
         {
            features = imp.readLayer(info.name);
         }
         catch (const std::exception& e)
         {
            ++skipped;
            emit statusMessage(tr("skipped %1: %2")
                                   .arg(QString::fromStdString(info.name))
                                   .arg(e.what()));
            continue;
         }

         if (!crs_checked)
         {
            data_.reference_crs = info.crs;
            crs_checked = true;
         }
         else if (!data_.reference_crs.sameAs(info.crs))
         {
            crs_mismatch = true;
         }

         if (!data_.has_bounds)
         {
            data_.union_bounds = info.bounds;
            data_.has_bounds   = true;
         }
         else
         {
            data_.union_bounds.minX = std::min(data_.union_bounds.minX, info.bounds.minX);
            data_.union_bounds.minY = std::min(data_.union_bounds.minY, info.bounds.minY);
            data_.union_bounds.maxX = std::max(data_.union_bounds.maxX, info.bounds.maxX);
            data_.union_bounds.maxY = std::max(data_.union_bounds.maxY, info.bounds.maxY);
         }

         Layer layer;
         layer.info     = std::move(info);
         layer.features = std::move(features);
         data_.layers.push_back(std::move(layer));
      }

      if (data_.layers.empty())
      {
         setState(State::Ready);
         emit statusMessage(tr("nothing loaded"));
         emit dataChanged();
         return;
      }

      const auto& first_crs = data_.reference_crs;
      const QString units = first_crs.units.empty()
                                ? tr("unknown units")
                                : QString::fromStdString(first_crs.units);
      const QString crs_label = first_crs.epsg != 0
                                    ? QStringLiteral("EPSG:%1").arg(first_crs.epsg)
                                    : (first_crs.isValid() ? tr("custom") : tr("no CRS"));

      QString msg = tr("loaded %1 layer(s), skipped %2, CRS %3 (%4)")
                        .arg(data_.layers.size())
                        .arg(skipped)
                        .arg(crs_label, units);
      if (crs_mismatch)
         msg += tr(" [WARNING: layer CRS mismatch]");

      setState(State::Ready);
      emit statusMessage(msg);
      emit dataChanged();
   }
   catch (const std::exception& e)
   {
      last_error_ = QString::fromUtf8(e.what());
      setState(State::Error);
      emit errorOccurred(last_error_);
      emit statusMessage(tr("load failed: %1").arg(last_error_));
   }
}

void Session::setState(State s)
{
   if (state_ == s)
      return;
   state_ = s;
   emit stateChanged(state_);
}

} // namespace core