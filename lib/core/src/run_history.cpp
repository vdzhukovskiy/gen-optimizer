#include "core/run_history.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStringList>
#include <cmath>
#include <stdexcept>

namespace core
{
QString runParametersJson(const gen::ParamSet &params)
{
   QJsonObject object;
   for (const auto &[name, value] : params)
      object.insert(QString::fromStdString(name), value);
   return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

namespace
{
QString number(double value)
{
   return std::isfinite(value) ? QString::number(value, 'g', 17) : QString();
}
QString count(std::size_t value)
{
   return QString::number(static_cast<qulonglong>(value));
}
QByteArray csvRow(QStringList cells)
{
   for (auto &cell : cells)
   {
      if (cell.contains(',') || cell.contains('"') || cell.contains('\r') || cell.contains('\n'))
      {
         cell.replace("\"", "\"\"");
         cell = '"' + cell + '"';
      }
   }
   return (cells.join(',') + QStringLiteral("\r\n")).toUtf8();
}
} // namespace

void exportRunsCsv(const QString &path, const std::vector<RunRecord> &runs)
{
   QSaveFile file(path);
   const auto fail = [&] { throw std::runtime_error(file.errorString().toStdString()); };
   if (!file.open(QIODevice::WriteOnly))
      fail();
   const auto write = [&](const QStringList &cells)
   {
      const auto row = csvRow(cells);
      if (file.write(row) != row.size())
         fail();
   };
   write({"schema_version",
          "run_id",
          "started_utc",
          "finished_utc",
          "data_path",
          "layer_filter",
          "scenario_id",
          "config_path",
          "config_sha256",
          "algorithm",
          "parameters_json",
          "evaluation_mode",
          "sample_stride",
          "layer_index",
          "layer",
          "crs_epsg",
          "crs_units",
          "crs_wkt",
          "orig_vertices",
          "result_vertices",
          "compression_ratio",
          "orig_features",
          "surviving_features",
          "evaluated_features",
          "invalid_comparisons",
          "orig_degenerate_features",
          "degenerate_features",
          "hausdorff",
          "average_deviation",
          "simplification_ms",
          "metrics_ms",
          "layer_ms",
          "calculation_ms"});
   for (const auto &run : runs)
      for (std::size_t i = 0; i < run.layers.size(); ++i)
      {
         const auto &layer = run.layers[i];
         const auto &m = layer.metrics;
         write({"1",
                run.id,
                run.started_utc.toString(Qt::ISODateWithMs),
                run.finished_utc.toString(Qt::ISODateWithMs),
                run.data_path,
                run.layer_filter,
                run.scenario_id,
                run.config_path,
                run.config_sha256,
                QString::fromStdString(run.algorithm),
                runParametersJson(run.params),
                run.evaluation.mode == metrics::EvaluationMode::Fast ? "fast" : "detailed",
                count(m.sample_stride),
                count(i),
                layer.name,
                QString::number(layer.crs.epsg),
                QString::fromStdString(layer.crs.units),
                QString::fromStdString(layer.crs.wkt),
                count(m.orig_vertices),
                count(m.result_vertices),
                number(m.compression_ratio),
                count(m.orig_features),
                count(m.surviving_features),
                count(m.evaluated_features),
                count(m.invalid_comparisons),
                count(m.orig_degenerate_features),
                count(m.degenerate_features),
                number(m.hausdorff),
                number(m.average_deviation),
                number(m.simplification_ms),
                number(m.metrics_ms),
                number(m.total_ms),
                number(run.calculation_ms)});
      }
   if (!file.commit())
      fail();
}
} // namespace core
