#include "core/run_history.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QTemporaryDir>
#include <iostream>
#include <stdexcept>

namespace
{
void require(bool value, const char *message)
{
   if (!value)
      throw std::runtime_error(message);
}
// Чтение CSV с кавычками, удвоенными кавычками и переносами внутри ячейки.
std::vector<QStringList> readCsv(const QString &path)
{
   QFile file(path);
   require(file.open(QIODevice::ReadOnly), "Cannot read export");
   const QString text = QString::fromUtf8(file.readAll());
   std::vector<QStringList> rows;
   QStringList row;
   QString cell;
   bool quoted = false;
   for (int i = 0; i < text.size(); ++i)
   {
      const auto c = text[i];
      if (c == '"')
      {
         if (quoted && i + 1 < text.size() && text[i + 1] == '"')
         {
            cell += '"';
            ++i;
         }
         else
            quoted = !quoted;
      }
      else if (!quoted && c == ',')
      {
         row << cell;
         cell.clear();
      }
      else if (!quoted && c == '\r' && i + 1 < text.size() && text[i + 1] == '\n')
      {
         row << cell;
         cell.clear();
         rows.push_back(row);
         row.clear();
         ++i;
      }
      else
         cell += c;
   }
   require(!quoted && cell.isEmpty() && row.isEmpty(), "Incomplete CSV");
   return rows;
}
} // namespace
int main(int argc, char **argv)
{
   QCoreApplication app(argc, argv);
   try
   {
      QTemporaryDir directory;
      require(directory.isValid(), "Temporary directory failed");
      QLocale::setDefault(QLocale(QLocale::Russian));
      core::RunRecord run;
      run.id = "first";
      run.started_utc = QDateTime::fromString("2026-10-06T10:00:00.123Z", Qt::ISODateWithMs);
      run.finished_utc = run.started_utc.addMSecs(10);
      run.data_path = "данные,\"реки\"\nслой.gpkg";
      run.scenario_id = "test";
      run.config_sha256 = "config-hash";
      run.algorithm = "douglas-peucker";
      run.params = {{"epsilon", 0.12345678901234567}};
      run.calculation_ms = 10.123456789;
      core::RunLayer layer;
      layer.name = "реки,\"название\"\r\nпродолжение";
      layer.crs.epsg = 32632;
      layer.crs.units = "metre";
      layer.crs.wkt = "PROJCRS[\"a,b\"]";
      layer.metrics.sample_stride = 8;
      layer.metrics.orig_vertices = 100;
      layer.metrics.result_vertices = 20;
      layer.metrics.hausdorff = 0;
      // Avg остаётся NaN и должен отличаться от настоящего нуля H.
      run.layers = {layer, layer};
      auto repeated = run;
      repeated.id = "second";
      repeated.evaluation.mode = core::metrics::EvaluationMode::Detailed;
      for (auto &item : repeated.layers)
         item.metrics.sample_stride = 1;
      const auto path = directory.filePath("runs.csv");
      core::exportRunsCsv(path, {run, repeated});
      const auto rows = readCsv(path);
      require(rows.size() == 5, "Repeated runs or layers lost");
      const auto column = [&](const char *name) { return rows[0].indexOf(name); };
      const auto get = [&](int row, const char *name) { return rows.at(row).at(column(name)); };
      for (const auto &row : rows)
         require(row.size() == 33, "Column count mismatch");
      require(get(1, "data_path") == run.data_path && get(1, "layer") == layer.name &&
                 get(1, "crs_wkt") == QString::fromStdString(layer.crs.wkt),
              "CSV escaping failed");
      require(get(1, "hausdorff") == "0" && get(1, "average_deviation").isEmpty(),
              "Missing metric became zero");
      require(get(1, "run_id") == "first" && get(3, "run_id") == "second" &&
                 get(3, "evaluation_mode") == "detailed" && get(3, "sample_stride") == "1" &&
                 get(2, "layer_index") == "1",
              "Run identity or settings lost");
      require(get(1, "calculation_ms").toDouble() == run.calculation_ms &&
                 !get(1, "calculation_ms").contains(',') &&
                 QJsonDocument::fromJson(get(1, "parameters_json").toUtf8())
                       .object()["epsilon"]
                       .toDouble() == run.params.at("epsilon"),
              "Precision or locale failed");
      require(get(1, "started_utc") == "2026-10-06T10:00:00.123Z", "UTC lost");
      core::exportRunsCsv(path, {});
      require(readCsv(path).size() == 1, "Export appended instead of replacing atomically");
      bool failed = false;
      try
      {
         core::exportRunsCsv(directory.filePath("missing/runs.csv"), {run});
      }
      catch (const std::runtime_error &)
      {
         failed = true;
      }
      require(failed, "Write error not reported");
      std::cout << "PASS: CSV escaping, repeated runs, layers, NaN, precision, UTC, write errors\n";
      return 0;
   }
   catch (const std::exception &e)
   {
      std::cerr << e.what() << '\n';
      return 1;
   }
}
