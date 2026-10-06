#include "app/panels/status_panel.h"

#include "core/session.h"

#include <QHeaderView>
#include <QLabel>
#include <QTableWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace
{
QString distanceLabel(double value)
{
   return std::isfinite(value) ? QString::number(value, 'g', 3) : QStringLiteral("N/A");
}
} // namespace

StatusPanel::StatusPanel(QWidget *parent) : QWidget(parent)
{
   auto *title = new QLabel(tr("<b>Metrics</b>"));

   summary_ = new QLabel(QStringLiteral("—"));
   summary_->setTextInteractionFlags(Qt::TextSelectableByMouse);
   summary_->setWordWrap(true);

   table_ = new QTableWidget(0, 11);
   table_->setHorizontalHeaderLabels({tr("Layer"), tr("V orig"), tr("V res"), tr("ratio"), tr("H"),
                                      tr("Avg"), tr("Used / skipped"), tr("Deg orig / res"),
                                      tr("Gen ms"), tr("Eval ms"), tr("Layer ms")});
   table_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
   table_->horizontalHeader()->setStretchLastSection(true);
   table_->verticalHeader()->setVisible(false);
   table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
   table_->setSelectionMode(QAbstractItemView::NoSelection);
   table_->setFocusPolicy(Qt::NoFocus);

   auto *root = new QVBoxLayout(this);
   root->setContentsMargins(8, 8, 8, 8);
   root->addWidget(title);
   root->addWidget(summary_);
   root->addWidget(table_, 1);
}

void StatusPanel::updateFrom(const core::Session &session)
{
   const auto &data = session.data();
   if (data.layers.empty() || !session.hasGeneralization())
   {
      summary_->setText(tr("no generalization yet"));
      table_->setRowCount(0);
      return;
   }

   const int n = static_cast<int>(data.layers.size());
   table_->setRowCount(n);

   double sum_orig = 0.0;
   double sum_res = 0.0;
   double sum_generation = 0.0;
   double sum_metrics = 0.0;
   double max_h = std::numeric_limits<double>::quiet_NaN();
   std::size_t evaluated_features = 0;
   std::size_t orig_degenerate_features = 0;
   std::size_t invalid_comparisons = 0;
   std::size_t degenerate_features = 0;

   for (int i = 0; i < n; ++i)
   {
      const auto &m = session.layerMetrics(i);
      sum_orig += static_cast<double>(m.orig_vertices);
      sum_res += static_cast<double>(m.result_vertices);
      sum_generation += m.simplification_ms;
      sum_metrics += m.metrics_ms;
      if (m.evaluated_features > 0)
         max_h = std::isfinite(max_h) ? std::max(max_h, m.hausdorff) : m.hausdorff;
      evaluated_features += m.evaluated_features;
      orig_degenerate_features += m.orig_degenerate_features;
      invalid_comparisons += m.invalid_comparisons;
      degenerate_features += m.degenerate_features;

      auto set = [&](int col, const QString &s)
      {
         auto *item = new QTableWidgetItem(s);
         item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
         table_->setItem(i, col, item);
      };

      set(0, QString::fromStdString(data.layers[i].info.name));
      set(1, QString::number(m.orig_vertices));
      set(2, QString::number(m.result_vertices));
      set(3, QString::number(m.compression_ratio, 'f', 3));
      set(4, distanceLabel(m.hausdorff));
      set(5, distanceLabel(m.average_deviation));
      set(6, QStringLiteral("%1 / %2")
                .arg(static_cast<qulonglong>(m.evaluated_features))
                .arg(static_cast<qulonglong>(m.invalid_comparisons)));
      set(7, QStringLiteral("%1 / %2")
                .arg(static_cast<qulonglong>(m.orig_degenerate_features))
                .arg(static_cast<qulonglong>(m.degenerate_features)));
      set(8, QString::number(m.simplification_ms, 'f', 2));
      set(9, QString::number(m.metrics_ms, 'f', 2));
      set(10, QString::number(m.total_ms, 'f', 2));
      table_->item(i, 8)->setToolTip(
         tr("Result preparation and simplification, including progress updates."));
      table_->item(i, 9)->setToolTip(
         tr("Quality metrics, including geometry checks and progress updates."));
      table_->item(i, 10)->setToolTip(
         tr("Full layer calculation. Loading and rendering are excluded."));
      const QString quality_scope = tr("Distances use only comparable, non-degenerate features. "
                                       "Evaluated: %1; excluded: %2. "
                                       "N/A means no suitable features.")
                                       .arg(static_cast<qulonglong>(m.evaluated_features))
                                       .arg(static_cast<qulonglong>(m.invalid_comparisons));
      for (int column : {4, 5, 6})
         table_->item(i, column)->setToolTip(quality_scope);
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
   settings +=
      tr("\nDistances: %1 features evaluated, %2 excluded. Degenerate original/result: %3/%4")
         .arg(static_cast<qulonglong>(evaluated_features))
         .arg(static_cast<qulonglong>(invalid_comparisons))
         .arg(static_cast<qulonglong>(orig_degenerate_features))
         .arg(static_cast<qulonglong>(degenerate_features));
   summary_->setText(settings + QStringLiteral("\n") +
                     tr("Total: %1 → %2 vertices (ratio %3), max H (evaluated) %4\nGeneration %5 "
                        "ms, metrics %6 ms, calculation %7 ms")
                        .arg(static_cast<qulonglong>(sum_orig))
                        .arg(static_cast<qulonglong>(sum_res))
                        .arg(total_ratio, 0, 'f', 3)
                        .arg(distanceLabel(max_h))
                        .arg(sum_generation, 0, 'f', 2)
                        .arg(sum_metrics, 0, 'f', 2)
                        .arg(session.generalizationTotalMs(), 0, 'f', 2));
}
