#include "core/scenario.h"

#include "generalization/algorithm.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <cmath>
#include <stdexcept>

namespace core
{
namespace
{
void require(bool condition, const QString &message)
{
   if (!condition)
      throw std::invalid_argument(message.toStdString());
}
double number(const QJsonObject &object, const QString &key)
{
   const auto value = object.value(key);
   require(value.isDouble() && std::isfinite(value.toDouble()),
           "Invalid numeric parameter: " + key);
   return value.toDouble();
}
} // namespace

std::vector<Scenario> readScenarios(const QString &path)
{
   QFile file(path);
   require(file.open(QIODevice::ReadOnly), "Cannot read scenario configuration: " + path);
   const auto bytes = file.readAll();
   QJsonParseError error;
   const auto document = QJsonDocument::fromJson(bytes, &error);
   require(error.error == QJsonParseError::NoError && document.isObject(), "Invalid scenario JSON");
   const auto root = document.object();
   require(root["version"].toInt() == 1 && root["scenarios"].isArray(),
           "Unsupported scenario configuration");
   const auto entries = root["scenarios"].toArray();
   require(!entries.isEmpty(), "No scenarios in configuration");
   std::vector<Scenario> scenarios;
   QSet<QString> ids;
   for (const auto &entry : entries)
   {
      require(entry.isObject(), "Scenario must be an object");
      const auto object = entry.toObject();
      Scenario scenario;
      scenario.id = object["id"].toString();
      require(!scenario.id.isEmpty() && !ids.contains(scenario.id),
              "Empty or duplicate scenario id");
      ids.insert(scenario.id);
      scenario.description = object["description"].toString();
      scenario.config_path = QFileInfo(path).absoluteFilePath();
      scenario.config_sha256 =
         QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
      const QString data_file = object["data_file"].toString();
      require(!data_file.isEmpty(), "Missing data_file in scenario: " + scenario.id);
      scenario.data_path = QDir(QFileInfo(path).absolutePath()).absoluteFilePath(data_file);
      const double epsg = number(object, "target_epsg");
      require(epsg > 0 && epsg <= 1000000 && std::floor(epsg) == epsg, "Invalid target_epsg");
      scenario.expected_epsg = static_cast<int>(epsg);
      scenario.default_algorithm =
         object.value("default_algorithm").toString("douglas-peucker").toStdString();
      require(object["algorithms"].isObject(), "Missing algorithms in scenario: " + scenario.id);
      const auto algorithms = object["algorithms"].toObject();
      require(!algorithms.isEmpty(), "Empty algorithms in scenario: " + scenario.id);
      for (auto it = algorithms.begin(); it != algorithms.end(); ++it)
      {
         const auto algorithm = gen::makeAlgorithm(it.key().toStdString());
         require(it.value().isObject(), "Algorithm settings must be an object: " + it.key());
         const auto parameters = it.value().toObject();
         const auto specs = algorithm->paramSpecs();
         require(parameters.size() == static_cast<int>(specs.size()),
                 "Missing or extra parameters: " + it.key());
         auto &settings = scenario.algorithms[it.key().toStdString()];
         for (const auto &spec : specs)
         {
            const QString name = QString::fromStdString(spec.name);
            require(parameters[name].isObject(), "Missing parameter settings: " + name);
            const auto parameter = parameters[name].toObject();
            for (auto key = parameter.begin(); key != parameter.end(); ++key)
               require(key.key() == "min" || key.key() == "max" || key.key() == "step" ||
                          key.key() == "initial" || key.key() == "logarithmic",
                       "Unknown parameter setting: " + key.key());
            require(!parameter.contains("logarithmic") || parameter["logarithmic"].isBool(),
                    "logarithmic must be boolean");
            ScenarioParameter value{number(parameter, "min"), number(parameter, "max"),
                                    number(parameter, "step"), number(parameter, "initial"),
                                    parameter.value("logarithmic").toBool(spec.logarithmic)};
            require(value.min_value < value.max_value && value.step > 0 &&
                       value.step <= value.max_value - value.min_value &&
                       value.initial >= value.min_value && value.initial <= value.max_value &&
                       (!value.logarithmic || value.min_value > 0),
                    "Invalid range for " + name);
            settings[spec.name] = value;
         }
      }
      require(scenario.algorithms.count(scenario.default_algorithm) != 0,
              "Default algorithm must be configured in scenario");
      scenarios.push_back(std::move(scenario));
   }
   return scenarios;
}
} // namespace core
