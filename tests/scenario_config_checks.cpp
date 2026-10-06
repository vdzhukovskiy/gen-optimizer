#include "core/scenario.h"
#include "core/session.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace
{
void require(bool condition, const char *message)
{
   if (!condition)
      throw std::runtime_error(message);
}
void save(const QString &path, const QByteArray &bytes)
{
   QFile file(path);
   require(file.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
              file.write(bytes) == bytes.size(),
           "Cannot write fixture");
}
} // namespace

int main(int argc, char **argv)
{
   QCoreApplication app(argc, argv);
   try
   {
      QTemporaryDir directory;
      require(directory.isValid(), "Cannot create config fixture directory");
      const auto data = directory.filePath("data.geojson");
      save(data, R"({"type":"FeatureCollection","features":[{"type":"Feature","properties":{},
         "geometry":{"type":"LineString","coordinates":[[0,0],[1,0.1],[2,0]]}}]})");
      auto root = QJsonDocument::fromJson(R"({"version":1,"scenarios":[{
         "id":"sample","description":"Test scenario","data_file":"data.geojson","target_epsg":4326,
         "default_algorithm":"visvalingam-whyatt","algorithms":{
            "douglas-peucker":{"epsilon":{"min":0.01,"max":0.9,"step":0.01,"initial":0.2,"logarithmic":false}},
            "visvalingam-whyatt":{"area_threshold":{"min":0.000001,"max":0.01,"step":0.000001,"initial":0.00005}},
            "li-openshaw":{"window_size":{"min":0.02,"max":0.8,"step":0.01,"initial":0.3}}
         }}]})")
                     .object();
      const auto config = directory.filePath("config.json");
      const auto writeRoot = [&](const QJsonObject &object)
      { save(config, QJsonDocument(object).toJson()); };
      writeRoot(root);
      const auto scenarios = core::readScenarios(config);
      require(scenarios.size() == 1 && scenarios.front().data_path == data &&
                 !scenarios.front().config_sha256.isEmpty(),
              "Path resolution or provenance wrong");
      core::Session session;
      const auto previous_directory = QDir::currentPath();
      require(QDir::setCurrent(QDir::tempPath()), "Cannot switch test working directory");
      require(session.loadScenario(config, "sample"), "Cannot load valid scenario");
      require(QDir::setCurrent(previous_directory), "Cannot restore test working directory");
      require(session.scenario() && session.scenario()->id == "sample" &&
                 session.currentAlgorithm() == "visvalingam-whyatt" &&
                 session.currentParams().at("area_threshold") == 0.00005 &&
                 !session.hasGeneralization() && !session.isGeneralizing(),
              "Preset or manual start broken");
      session.setAlgorithm("douglas-peucker");
      require(session.currentSpecs().front().min_value == 0.01 &&
                 session.currentSpecs().front().max_value == 0.9 &&
                 !session.currentSpecs().front().logarithmic &&
                 session.currentParams().at("epsilon") == 0.2,
              "Algorithm did not use configured range");
      session.setParam("epsilon", 0.4);
      session.resetParams();
      require(session.currentParams().at("epsilon") == 0.2, "Reset ignored initial");

      const auto *original_data = &session.data();
      const auto expectInvalid = [&](QJsonObject changed)
      {
         writeRoot(changed);
         require(!session.loadScenario(config, "sample") && &session.data() == original_data &&
                    session.scenario() && session.currentParams().at("epsilon") == 0.2,
                 "Invalid config replaced valid session");
      };
      for (const auto &[key, value] : std::vector<std::pair<QString, QJsonValue>>{
              {"min", 1.0}, {"max", 0.001}, {"step", 0.0}, {"initial", 20.0}, {"unknown", 1}})
      {
         auto changed = root;
         auto scenario = changed["scenarios"].toArray().at(0).toObject();
         auto algorithms = scenario["algorithms"].toObject();
         auto algorithm = algorithms["douglas-peucker"].toObject();
         auto parameter = algorithm["epsilon"].toObject();
         parameter[key] = value;
         algorithm["epsilon"] = parameter;
         algorithms["douglas-peucker"] = algorithm;
         scenario["algorithms"] = algorithms;
         changed["scenarios"] = QJsonArray{scenario};
         expectInvalid(changed);
      }
      for (const auto &[key, value] :
           std::vector<std::pair<QString, QJsonValue>>{{"target_epsg", 32632},
                                                       {"data_file", "missing.gpkg"},
                                                       {"default_algorithm", "unknown"}})
      {
         auto changed = root;
         auto scenario = changed["scenarios"].toArray().at(0).toObject();
         scenario[key] = value;
         changed["scenarios"] = QJsonArray{scenario};
         expectInvalid(changed);
      }
      writeRoot(root);
      require(!session.loadScenario(config, "missing"), "Unknown id accepted");
      session.loadPath(data);
      require(!session.scenario() && session.currentSpecs().front().min_value == 0.001 &&
                 session.currentSpecs().front().max_value == 1.0 &&
                 std::abs(session.currentParams().at("epsilon") - std::sqrt(0.001)) < 1e-10,
              "Plain file retained scenario overrides");
      auto scenario = root["scenarios"].toArray().at(0).toObject();
      root["scenarios"] = QJsonArray{scenario, scenario};
      writeRoot(root);
      require(!session.loadScenario(config, "sample"), "Duplicate scenario accepted");
      std::cout << "PASS: scenario presets, reset, validation, relative paths, CRS, fallback, no "
                   "auto-run\n";
      return 0;
   }
   catch (const std::exception &error)
   {
      std::cerr << error.what() << '\n';
      return 1;
   }
}
