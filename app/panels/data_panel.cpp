#include "app/panels/data_panel.h"
#include "core/session.h"
#include "core/metrics.h"

#include <QFormLayout>
#include <QLabel>
#include <QVBoxLayout>

DataPanel::DataPanel(QWidget* parent) : QWidget(parent)
{
   auto* title = new QLabel(tr("<b>Data</b>"));
   source_   = new QLabel(QStringLiteral("—"));
   crs_      = new QLabel(QStringLiteral("—"));
   layers_   = new QLabel(QStringLiteral("0"));
   features_ = new QLabel(QStringLiteral("0"));
   vertices_ = new QLabel(QStringLiteral("0"));

   for (QLabel* l : {source_, crs_, layers_, features_, vertices_})
      l->setTextInteractionFlags(Qt::TextSelectableByMouse);

   auto* form = new QFormLayout;
   form->setContentsMargins(0, 0, 0, 0);
   form->addRow(tr("Source:"),   source_);
   form->addRow(tr("CRS:"),      crs_);
   form->addRow(tr("Layers:"),   layers_);
   form->addRow(tr("Features:"), features_);
   form->addRow(tr("Vertices:"), vertices_);

   auto* root = new QVBoxLayout(this);
   root->setContentsMargins(8, 8, 8, 8);
   root->addWidget(title);
   root->addLayout(form);
   root->addStretch(1);
}

void DataPanel::updateFrom(const core::Session& session)
{
   const auto& data = session.data();

   if (data.layers.empty())
   {
      source_->setText(QStringLiteral("—"));
      crs_->setText(QStringLiteral("—"));
      layers_->setText(QStringLiteral("0"));
      features_->setText(QStringLiteral("0"));
      vertices_->setText(QStringLiteral("0"));
      return;
   }

   // Источник: показываем имя первого слоя + суффикс, если слоёв больше.
   QString src = QString::fromStdString(data.layers.front().info.name);
   if (data.layers.size() > 1)
      src += tr(" (+%1 more)").arg(data.layers.size() - 1);
   source_->setText(src);

   const auto& crs = data.reference_crs;
   if (!crs.isValid())
   {
      crs_->setText(tr("(none)"));
   }
   else
   {
      const QString label = crs.epsg != 0
                                ? QStringLiteral("EPSG:%1").arg(crs.epsg)
                                : tr("custom");
      const QString units = crs.units.empty()
                                ? tr("?")
                                : QString::fromStdString(crs.units);
      crs_->setText(QStringLiteral("%1 (%2)").arg(label, units));
   }

   std::size_t total_features = 0;
   std::size_t total_vertices = 0;
   for (const auto& layer : data.layers)
   {
      total_features += layer.features.size();
      total_vertices += core::metrics::countVertices(layer.features);
   }

   layers_->setText(QString::number(data.layers.size()));
   features_->setText(QString::number(total_features));
   vertices_->setText(QString::number(total_vertices));
}