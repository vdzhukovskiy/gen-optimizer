#include "app/panels/status_panel.h"

#include "core/session.h"

#include <QHeaderView>
#include <QLabel>
#include <QTableWidget>
#include <QVBoxLayout>
#include <algorithm>

StatusPanel::StatusPanel(QWidget* parent) : QWidget(parent)
{
   auto* title = new QLabel(tr("<b>Metrics</b>"));

   summary_ = new QLabel(QStringLiteral("—"));
   summary_->setTextInteractionFlags(Qt::TextSelectableByMouse);
   summary_->setWordWrap(true);

   table_ = new QTableWidget(0, 5);
   table_->setHorizontalHeaderLabels({
       tr("Layer"), tr("V orig"), tr("V res"), tr("ratio"), tr("H / ms")
   });
   table_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
   table_->horizontalHeader()->setStretchLastSection(true);
   table_->verticalHeader()->setVisible(false);
   table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
   table_->setSelectionMode(QAbstractItemView::NoSelection);
   table_->setFocusPolicy(Qt::NoFocus);

   auto* root = new QVBoxLayout(this);
   root->setContentsMargins(8, 8, 8, 8);
   root->addWidget(title);
   root->addWidget(summary_);
   root->addWidget(table_, 1);
}

void StatusPanel::updateFrom(const core::Session& session)
{
   const auto& data = session.data();
   if (data.layers.empty() || !session.hasGeneralization())
   {
      summary_->setText(tr("no generalization yet"));
      table_->setRowCount(0);
      return;
   }

   const int n = static_cast<int>(data.layers.size());
   table_->setRowCount(n);

   double sum_orig = 0.0;
   double sum_res  = 0.0;
   double sum_time = 0.0;
   double max_h    = 0.0;

   for (int i = 0; i < n; ++i)
   {
      const auto& m = session.layerMetrics(i);
      sum_orig += static_cast<double>(m.orig_vertices);
      sum_res  += static_cast<double>(m.result_vertices);
      sum_time += m.elapsed_ms;
      max_h     = std::max(max_h, m.hausdorff);

      auto set = [&](int col, const QString& s) {
         auto* item = new QTableWidgetItem(s);
         item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
         table_->setItem(i, col, item);
      };

      set(0, QString::fromStdString(data.layers[i].info.name));
      set(1, QString::number(m.orig_vertices));
      set(2, QString::number(m.result_vertices));
      set(3, QString::number(m.compression_ratio, 'f', 3));
      set(4, QStringLiteral("%1 / %2")
                 .arg(m.hausdorff, 0, 'g', 3)
                 .arg(m.elapsed_ms, 0, 'f', 1));
   }

   const double total_ratio = (sum_orig > 0.0) ? sum_res / sum_orig : 1.0;
   QString settings = QString::fromStdString(session.resultAlgorithm());
   std::vector<std::pair<std::string, double>> params(session.resultParams().begin(),
                                                      session.resultParams().end());
   std::sort(params.begin(), params.end());
   for (const auto &[name, value] : params)
      settings += QStringLiteral(" %1=%2").arg(QString::fromStdString(name)).arg(value, 0, 'g', 6);
   if (!session.resultMatchesSettings())
      settings += tr(" (settings changed; showing previous result)");
   summary_->setText(settings + QStringLiteral("\n") +
                     tr("Total: %1 → %2 vertices (ratio %3), max H %4, time %5 ms")
                        .arg(static_cast<qulonglong>(sum_orig))
                        .arg(static_cast<qulonglong>(sum_res))
                        .arg(total_ratio, 0, 'f', 3)
                        .arg(max_h, 0, 'g', 3)
                        .arg(sum_time, 0, 'f', 1));
}
