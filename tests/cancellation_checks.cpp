#include "core/generalization_worker.h"
#include "core/session.h"
#include "mainwindow.h"

#include <QApplication>
#include <QAction>
#include <QScrollBar>
#include <QSpinBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QGraphicsView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QProgressBar>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTableWidget>
#include <QThread>
#include <QTimer>
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

template <class Function> void requireCancellation(Function function)
{
   bool cancelled = false;
   try
   {
      function();
   }
   catch (const gen::Cancelled &)
   {
      cancelled = true;
   }
   require(cancelled, "Cancellation was swallowed or not checked inside computation");
}

void checkLiOpenshawGrid()
{
   const auto algorithm = gen::makeAlgorithm("li-openshaw");
   const gen::ParamSet params{{"window_size", 1.0}};
   const auto expect = [&](const gi::LineString& input, const gi::LineString& expected) {
      const auto result = std::get<gi::LineString>(algorithm->simplify(input, params));
      require(result.size() == expected.size(), "Li-Openshaw unexpected number of cell representatives");
      for (std::size_t i = 0; i < result.size(); ++i)
         require(std::abs(result[i].x - expected[i].x) < 1e-10
                    && std::abs(result[i].y - expected[i].y) < 1e-10,
                 "Li-Openshaw wrong inlet/outlet midpoint");
   };
   expect({{0, 0}, {1, 0}, {3, 0}}, {{0, 0}, {1, 0}, {2, 0}, {3, 0}});
   expect({{0, 0}, {0.5, 0}, {1.5, 0}, {3, 0}}, {{0, 0}, {1, 0}, {2, 0}, {3, 0}});
   expect({{0, 0}, {-1, 0}, {-3, 0}}, {{0, 0}, {-1, 0}, {-2, 0}, {-3, 0}});
   expect({{0, 0}, {1, 1}, {3, 3}}, {{0, 0}, {1, 1}, {2, 2}, {3, 3}});
   expect({{0, 0}, {0.5, 0.5}, {1.5, 1.5}, {3, 3}},
          {{0, 0}, {1, 1}, {2, 2}, {3, 3}});
   expect({{0, 0}, {0.1, 0.2}, {0.2, 0}}, {{0, 0}, {0.2, 0}});
   expect({{0, 0}, {0, 0}, {1, 0}, {3, 0}}, {{0, 0}, {1, 0}, {2, 0}, {3, 0}});
   expect({{0, 0}, {2, 0}, {0, 0}}, {{0, 0}, {1, 0}, {1.5, 0}, {1, 0}, {0, 0}});
   // Изгиб в одной ячейке заменяется новой точкой между входом и выходом.
   expect({{0, 0}, {1, 0.4}, {2, 0}}, {{0, 0}, {1, 0.2}, {2, 0}});
   // Движение вдоль границы не создаёт фиктивных проходов по обеим сторонам.
   expect({{0, 0}, {0.5, 0}, {0.5, 2}, {0.5, 3}},
          {{0, 0}, {0.5, 0.25}, {0.5, 1}, {0.5, 2}, {0.5, 3}});
   int checks = 0;
   requireCancellation([&] {
      algorithm->simplify(gi::LineString{{0, 0}, {1, 0}, {1000000, 1}}, params,
                          [&] { return ++checks > 50; });
   });
}

void checkAlgorithmsAndMetrics()
{
   gi::LineString line;
   for (int i = 0; i < 10000; ++i)
      line.push_back({i * 0.001, std::sin(i * 0.1)});

   for (const auto &name : gen::availableAlgorithms())
   {
      const auto algorithm = gen::makeAlgorithm(name);
      gen::ParamSet params;
      for (const auto &spec : algorithm->paramSpecs())
         params[spec.name] = spec.min_value;
      int checks = 0;
      requireCancellation([&]
                          { algorithm->simplify(line, params, [&] { return ++checks >= 10; }); });

      // Отсутствие запроса отмены не должно менять геометрию.
      const gi::LineString small{{0, 0}, {1, 0.1}, {2, 0}, {3, 1}};
      const auto a = algorithm->simplify(small, params);
      const auto b = algorithm->simplify(small, params, [] { return false; });
      const auto &first = std::get<gi::LineString>(a);
      const auto &second = std::get<gi::LineString>(b);
      require(first.size() == second.size(), "Cancellation callback changed line size");
      for (std::size_t i = 0; i < first.size(); ++i)
         require(first[i].x == second[i].x && first[i].y == second[i].y,
                 "Cancellation callback changed vertices");

      for (const gi::LineString short_line :
           {gi::LineString{}, gi::LineString{{0, 0}}, gi::LineString{{0, 0}, {1, 1}}})
         require(std::get<gi::LineString>(algorithm->simplify(short_line, params)).size() ==
                    short_line.size(),
                 "Short line changed");

      const gi::Polygon polygon{{{0, 0}, {2, 0}, {2, 2}, {0, 2}, {0, 0}},
                                {{0.5, 0.5}, {1, 0.5}, {1, 1}, {0.5, 1}, {0.5, 0.5}}};
      const gi::MultiPolygon multi{{polygon, polygon}};
      const auto result = std::get<gi::MultiPolygon>(algorithm->simplify(multi, params));
      require(result.polys.size() == 2 && result.polys.front().size() == 2,
              "Polygon components or holes changed");
      for (const auto &poly : result.polys)
         for (const auto &ring : poly)
            require(ring.front().x == ring.back().x && ring.front().y == ring.back().y,
                    "Ring is no longer closed");
   }

   int checks = 0;
   requireCancellation(
      [&] { core::metrics::hausdorff(line, line, 1, [&] { return ++checks >= 10; }); });
   checks = 0;
   requireCancellation(
      [&] { core::metrics::averageDeviation(line, line, 1, [&] { return ++checks >= 10; }); });

   // Воркер должен прервать оценку одного большого объекта, не дожидаясь следующей фичи.
   auto data = std::make_shared<core::MapData>();
   data->layers.resize(1);
   gi::Feature feature;
   feature.geometry = line;
   data->layers.front().features.push_back(feature);
   core::GeneralizationWorker worker(data, "li-openshaw", {{"window_size", 0.001}});
   core::GeneralizationResult result;
   auto *thread = QThread::create(
      [&]
      {
         result = worker.run(*QThread::currentThread(),
                             [&](const core::GeneralizationProgress &progress)
                             {
                                if (progress.phase == QStringLiteral("Evaluating metrics"))
                                   QThread::currentThread()->requestInterruption();
                             });
      });
   thread->start();
   const bool stopped = thread->wait(5000);
   if (!stopped)
   {
      thread->requestInterruption();
      thread->wait();
   }
   delete thread;
   require(stopped && result.interrupted && result.error.isEmpty(),
           "Worker failed to cancel during metric evaluation");
}

QString writeFixture(const QTemporaryDir &directory)
{
   QJsonArray features;
   for (int li = 0; li < 20; ++li)
   {
      QJsonArray coordinates;
      for (int i = 0; i < 200; ++i)
         coordinates.append(QJsonArray{i * 0.01, li + 0.1 * std::sin(i * 0.1)});
      features.append(QJsonObject{
         {"type", "Feature"},
         {"properties", QJsonObject{{"id", li}}},
         {"geometry", QJsonObject{{"type", "LineString"}, {"coordinates", coordinates}}}});
   }
   const QString path = directory.filePath("lines.geojson");
   QFile file(path);
   require(file.open(QIODevice::WriteOnly), "Cannot create fixture");
   const auto bytes =
      QJsonDocument(QJsonObject{{"type", "FeatureCollection"}, {"features", features}}).toJson();
   require(file.write(bytes) == bytes.size(), "Cannot write fixture");
   return path;
}

void awaitIdle(core::Session &session)
{
   QEventLoop loop;
   QTimer poll;
   poll.setInterval(1);
   QObject::connect(&poll, &QTimer::timeout, &loop,
                    [&]
                    {
                       if (!session.isGeneralizing() && !session.hasPendingRestart())
                          loop.quit();
                    });
   QTimer watchdog;
   watchdog.setSingleShot(true);
   QObject::connect(&watchdog, &QTimer::timeout, &loop, &QEventLoop::quit);
   watchdog.start(20000);
   poll.start();
   loop.exec();
   require(!session.isGeneralizing() && !session.hasPendingRestart(),
           "Worker or restart timed out");
}

void checkSessionAndUi(const QString &path)
{
   core::Session session;
   session.loadPath(path);
   require(!session.data().layers.empty() && !session.hasGeneralization(),
           "Load failed or auto-generated");
   MainWindow window(&session);
   window.show();
   auto *generate = window.findChild<QPushButton *>("generateButton");
   auto *cancel = window.findChild<QPushButton *>("cancelButton");
   auto *progress = window.findChild<QProgressBar *>();
   auto *combo = window.findChild<QComboBox *>();
   require(generate && cancel && progress && combo, "Missing controls");

   combo->setCurrentText("visvalingam-whyatt");
   auto* spin = window.findChild<QDoubleSpinBox*>();
   require(spin->decimals() >= 6 && spin->minimum() == 1e-6, "Small parameter range rounded");
   spin->setValue(2e-6);
   require(session.currentParams().at("area_threshold") == 2e-6, "Small parameter value rounded");
   combo->setCurrentText("douglas-peucker");
   QApplication::processEvents();
   auto* view = window.findChild<QGraphicsView*>();
   const QPoint cursor = view->viewport()->rect().center() + QPoint(40, 20);
   const auto anchor = view->mapToScene(cursor);
   const double initial_scale = view->transform().m11();
   QWheelEvent wheel(cursor, view->viewport()->mapToGlobal(cursor), QPoint(), QPoint(0, 120),
                     Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
   QApplication::sendEvent(view->viewport(), &wheel);
   require(view->transform().m11() > initial_scale, "Wheel did not zoom map");
   // На оси, где карта целиком помещается, её центрирование важнее якоря курсора.
   const auto anchor_after = view->mapToScene(cursor);
   if (view->sceneRect().width() * view->transform().m11() > view->viewport()->width())
      require(std::abs(anchor.x() - anchor_after.x()) < 3.0 / initial_scale,
              "Zoom did not preserve horizontal cursor anchor");
   if (view->sceneRect().height() * view->transform().m11() > view->viewport()->height())
      require(std::abs(anchor.y() - anchor_after.y()) < 3.0 / initial_scale,
              "Zoom did not preserve vertical cursor anchor");
   QWheelEvent zoom_out(cursor, view->viewport()->mapToGlobal(cursor), QPoint(), QPoint(0, -12000),
                         Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
   QApplication::sendEvent(view->viewport(), &zoom_out);
   require(std::abs(view->transform().m11() - initial_scale) < 1e-9,
           "Zoom-out exceeded full-map scale");
   const auto full_map_center = view->mapToScene(view->viewport()->rect().center());
   QApplication::sendEvent(view->viewport(), &zoom_out);
   require(std::abs(view->transform().m11() - initial_scale) < 1e-9
              && QLineF(full_map_center, view->mapToScene(view->viewport()->rect().center())).length() < 1e-9,
           "Repeated zoom-out moved or shrank full map");
   QWheelEvent zoom_in(cursor, view->viewport()->mapToGlobal(cursor), QPoint(), QPoint(0, 1200),
                        Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
   QApplication::sendEvent(view->viewport(), &zoom_in);
   const auto center_before = view->mapToScene(view->viewport()->rect().center());
   QMouseEvent press(QEvent::MouseButtonPress, cursor, view->viewport()->mapToGlobal(cursor),
                     Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
   QApplication::sendEvent(view->viewport(), &press);
   const QPoint moved = cursor + QPoint(80, 50);
   QMouseEvent move(QEvent::MouseMove, moved, view->viewport()->mapToGlobal(moved),
                    Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
   QApplication::sendEvent(view->viewport(), &move);
   QMouseEvent release(QEvent::MouseButtonRelease, moved, view->viewport()->mapToGlobal(moved),
                       Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
   QApplication::sendEvent(view->viewport(), &release);
   require(QLineF(center_before, view->mapToScene(view->viewport()->rect().center())).length() > 1.0,
           "Mouse drag did not move map");

   const auto zoomed_transform = view->transform();
   const auto zoomed_center = view->mapToScene(view->viewport()->rect().center());
   window.resize(window.width() + 140, window.height() + 80);
   QApplication::processEvents();
   require(view->transform() == zoomed_transform
              && QLineF(zoomed_center, view->mapToScene(view->viewport()->rect().center())).length()
                    < 3.0 / zoomed_transform.m11(),
           "Resizing lost zoomed camera");
   const auto requireBoundedViewport = [&] {
      const QRectF bounds = view->sceneRect();
      const QRectF visible = view->mapToScene(view->viewport()->rect()).boundingRect();
      const double tolerance = 3.0 / view->transform().m11();
      if (visible.width() < bounds.width())
         require(visible.left() >= bounds.left() - tolerance && visible.right() <= bounds.right() + tolerance,
                 "Horizontal scrolling escaped map bounds");
      else
         require(std::abs(visible.center().x() - bounds.center().x()) < tolerance,
                 "Map not centered on non-scrollable horizontal axis");
      if (visible.height() < bounds.height())
         require(visible.top() >= bounds.top() - tolerance && visible.bottom() <= bounds.bottom() + tolerance,
                 "Vertical scrolling escaped map bounds");
      else
         require(std::abs(visible.center().y() - bounds.center().y()) < tolerance,
                 "Map not centered on non-scrollable vertical axis");
   };
   const int previous_horizontal = view->horizontalScrollBar()->value();
   const int previous_vertical = view->verticalScrollBar()->value();
   for (auto* bar : {view->horizontalScrollBar(), view->verticalScrollBar()})
   {
      bar->setValue(bar->minimum());
      requireBoundedViewport();
      bar->setValue(bar->maximum());
      requireBoundedViewport();
   }
   view->horizontalScrollBar()->setValue(previous_horizontal);
   view->verticalScrollBar()->setValue(previous_vertical);
   const auto comparison_center = view->mapToScene(view->viewport()->rect().center());
   const auto comparison_transform = view->transform();
   const auto comparison_scene_rect = view->scene()->sceneRect();
   const auto requireSameCamera = [&] {
      require(view->transform() == comparison_transform
                 && view->scene()->sceneRect() == comparison_scene_rect
                 && QLineF(comparison_center, view->mapToScene(view->viewport()->rect().center())).length()
                       < 3.0 / comparison_transform.m11(),
              "Generalization changed map coordinates, zoom or center");
   };
   int done = 0, cancelled = 0, starts = 0, heartbeat = 0, progress_updates = 0;
   qint64 last_completed = 0;
   QObject::connect(&session, &core::Session::generalizationDone, [&] { ++done; });
   QObject::connect(&session, &core::Session::generalizationCancelled, [&] { ++cancelled; });
   QObject::connect(&session, &core::Session::generalizationStarted,
                    [&]
                    {
                       ++starts;
                       last_completed = 0;
                    });
   QObject::connect(
      &session, &core::Session::generalizationProgressChanged,
      [&](qint64 completed, qint64 total, const QString &, const QString &, qint64, qint64)
      {
         require(QThread::currentThread() == qApp->thread(),
                 "Progress delivered outside GUI thread");
         require(completed >= last_completed && completed <= total,
                 "Progress moved backwards or overflowed");
         last_completed = completed;
         ++progress_updates;
      });
   QTimer timer;
   QObject::connect(&timer, &QTimer::timeout, [&] { ++heartbeat; });
   timer.start(0);

   auto *evaluation_mode = window.findChild<QComboBox *>("evaluationModeCombo");
   auto *evaluation_stride = window.findChild<QSpinBox *>("evaluationStrideSpin");
   require(evaluation_mode && evaluation_stride && evaluation_stride->value() == 8,
           "Missing evaluation controls or wrong default");
   bool invalid_stride_rejected = false;
   try
   {
      session.setEvaluationSettings({core::metrics::EvaluationMode::Fast, 0});
   }
   catch (const std::invalid_argument &)
   {
      invalid_stride_rejected = true;
   }
   require(invalid_stride_rejected && session.evaluationSettings().fast_stride == 8,
           "Invalid sampling step changed settings");
   evaluation_mode->setCurrentIndex(1);
   require(!evaluation_stride->isEnabled() && !session.isGeneralizing(),
           "Detailed mode step enabled or settings triggered calculation");
   generate->click();
   require(cancel->isEnabled() && generate->isEnabled() && combo->isEnabled(),
           "Controls locked during task");
   cancel->click();
   require(session.isCancelling(), "Cancel not requested");
   awaitIdle(session);
   require(done == 0 && cancelled == 1 && !session.hasGeneralization(),
           "Cancelled task published a result");

   generate->click();
   awaitIdle(session);
   require(done == 1 && session.hasGeneralization() && progress->value() == 1000,
           "Successful task did not publish complete result");
   requireSameCamera();
   double layer_totals = 0;
   for (int i = 0; i < static_cast<int>(session.data().layers.size()); ++i)
   {
      const auto &timing = session.layerMetrics(i);
      require(timing.simplification_ms >= 0 && timing.metrics_ms >= 0 &&
                 timing.total_ms + 1e-6 >= timing.simplification_ms + timing.metrics_ms,
              "Layer timing boundaries inconsistent");
      layer_totals += timing.total_ms;
   }
   require(session.generalizationTotalMs() + 1e-6 >= layer_totals, "Job time excludes layer work");
   require(progress_updates > 0 && heartbeat > 0, "Missing progress or GUI heartbeat");
   require(session.layerMetrics(0).sample_stride == 1 &&
              session.resultEvaluationSettings().mode == core::metrics::EvaluationMode::Detailed,
           "Detailed evaluation metadata missing");
   evaluation_mode->setCurrentIndex(0);
   evaluation_stride->setValue(3);
   require(evaluation_stride->isEnabled() && !session.resultMatchesSettings() &&
              session.layerMetrics(0).sample_stride == 1,
           "Editing evaluation changed the previous result");
   const auto previous_vertices = session.layerMetrics(0).result_vertices;
   const auto previous_items = window.findChild<QGraphicsView *>()->scene()->items();

   generate->click();
   combo->setCurrentText("visvalingam-whyatt");
   window.findChild<QDoubleSpinBox *>()->setValue(0.01);
   require(session.hasGeneralization() && !session.resultMatchesSettings(),
           "Previous result lost on setting change");
   generate->click();
   require(session.isCancelling() && session.hasPendingRestart(), "Restart not queued");
   combo->setCurrentText("li-openshaw");
   window.findChild<QDoubleSpinBox *>()->setValue(0.5);
   evaluation_mode->setCurrentIndex(1);
   generate->click(); // Последняя заявка заменяет предыдущую.
   evaluation_mode->setCurrentIndex(0); // Не изменяет снимок оценки.
   window.findChild<QDoubleSpinBox *>()->setValue(1.0); // Не изменяет снимок заявки.
   awaitIdle(session);
   requireSameCamera();
   require(done == 2 && starts == 4 && cancelled == 2,
           "Restart launched duplicate tasks or published partial result");
   require(session.resultAlgorithm() == "li-openshaw" &&
              session.resultParams().at("window_size") == 0.5,
           "Restart did not use latest requested snapshot");
   require(session.resultEvaluationSettings().mode == core::metrics::EvaluationMode::Detailed &&
              session.layerMetrics(0).sample_stride == 1,
           "Queued restart did not preserve evaluation snapshot");
   require(!session.resultMatchesSettings(), "Result confused with edited settings");
   bool labelled = false;
   for (auto *label : window.findChildren<QLabel *>())
      labelled |=
         label->text().contains("window_size=0.5") && label->text().contains("settings changed") &&
         label->text().contains("Evaluation: detailed");
   require(labelled, "Displayed result settings missing");

   generate->click();
   generate->click();
   cancel->click(); // Cancel также удаляет отложенную заявку.
   const auto retained_vertices = session.layerMetrics(0).result_vertices;
   const auto retained_items = window.findChild<QGraphicsView *>()->scene()->items();
   awaitIdle(session);
   require(done == 2 && !session.hasPendingRestart(), "Cancel did not clear queued restart");
   require(session.resultEvaluationSettings().mode == core::metrics::EvaluationMode::Detailed &&
              session.layerMetrics(0).sample_stride == 1 &&
              session.layerMetrics(0).result_vertices == retained_vertices &&
              window.findChild<QGraphicsView *>()->scene()->items() == retained_items,
           "Cancel changed previous metrics or scene");
   require(!previous_items.empty() && previous_vertices > 0,
           "Original completed scene was missing");
   require(!cancel->isEnabled() && generate->isEnabled(),
           "Controls not restored after cancellation");

   auto* fit_action = window.findChild<QAction*>("fitMapAction");
   require(fit_action, "Fit map action missing");
   fit_action->trigger();
   require(view->horizontalScrollBar()->minimum() == view->horizontalScrollBar()->maximum()
              && view->verticalScrollBar()->minimum() == view->verticalScrollBar()->maximum(),
           "Full-map view still allows scrolling off map");
   requireBoundedViewport();
   const auto fitted_transform = view->transform();
   QApplication::sendEvent(view->viewport(), &zoom_out);
   require(view->transform() == fitted_transform, "Zoom-out exceeded fit-map scale after reset");

   // Перезагрузка не должна публиковать результат от прежнего набора.
   generate->click();
   session.loadPath(path);
   awaitIdle(session);
   require(!session.hasGeneralization() && done == 2, "Old dataset result published after reload");

   for (int i = 0; i < 5; ++i)
   {
      core::Session closing;
      closing.loadPath(path);
      closing.regenerateAsync();
   }
}

void checkDistanceCoverageUi()
{
   QTemporaryDir directory;
   require(directory.isValid(), "Cannot create quality UI fixture directory");
   const QString path = directory.filePath("quality.geojson");
   auto document = QJsonDocument::fromJson(R"({"type":"FeatureCollection","features":[
      {"type":"Feature","properties":{},"geometry":{"type":"LineString","coordinates":[[0,0],[10,0]]}},
      {"type":"Feature","properties":{},"geometry":{"type":"Polygon","coordinates":[[[2,0],[2.001,0],[2,0.001],[2,0]]]}}
   ]})");
   const auto save = [&] {
      QFile file(path);
      require(file.open(QIODevice::WriteOnly | QIODevice::Truncate), "Cannot write quality UI fixture");
      const auto bytes = document.toJson();
      require(file.write(bytes) == bytes.size(), "Incomplete quality UI fixture");
   };
   save();
   core::Session session;
   session.loadPath(path);
   session.setParam("epsilon", 1.0);
   MainWindow window(&session);
   window.show();
   session.regenerateAsync();
   awaitIdle(session);
   auto* table = window.findChild<QTableWidget*>();
   require(table && table->rowCount() == 1 && table->columnCount() == 11, "Missing quality table");
   require(table->item(0, 4)->text() == "0" && table->item(0, 5)->text() == "0" &&
              table->item(0, 6)->text() == "1 / 1" && table->item(0, 7)->text() == "0 / 1",
           "UI did not show valid distance alongside excluded and degenerate counts");
   auto object = document.object();
   object["features"] = QJsonArray{object["features"].toArray().last()};
   document.setObject(object);
   save();
   session.loadPath(path);
   session.regenerateAsync();
   awaitIdle(session);
   require(table->item(0, 4)->text() == "N/A" && table->item(0, 5)->text() == "N/A" &&
              table->item(0, 6)->text() == "0 / 1",
           "UI displayed zero distance with no evaluated features");
}

void checkProjectedScenario(const QString &path)
{
   core::Session session;
   session.loadPath(path);
   require(session.state() == core::Session::State::Ready && !session.data().layers.empty() &&
              session.data().reference_crs.units == "metre",
           "Projected scenario not loaded in metres");
   MainWindow window(&session);
   window.show();
   for (const auto &name : gen::availableAlgorithms())
   {
      session.setAlgorithm(name);
      const auto &spec = session.currentSpecs().front();
      require(spec.min_value == 1 && spec.max_value >= 10000, "Metre parameter range not updated");
      auto *spin = window.findChild<QDoubleSpinBox *>();
      require(spin && spin->minimum() == spec.min_value && spin->maximum() == spec.max_value,
              "Projected range not reflected in UI");
      session.setParam(spec.name, name == "visvalingam-whyatt" ? 100000 : 1000);
      session.regenerateAsync();
      awaitIdle(session);
      require(session.hasGeneralization() && session.resultMatchesSettings(),
              "Projected calculation failed");
      double sum_generation = 0, sum_metrics = 0, sum_total = 0;
      for (int i = 0; i < static_cast<int>(session.data().layers.size()); ++i)
      {
         const auto &metrics = session.layerMetrics(i);
         require(metrics.total_ms >= metrics.simplification_ms + metrics.metrics_ms,
                 "Projected timing inconsistent");
         require(metrics.evaluated_features + metrics.invalid_comparisons == metrics.orig_features,
                 "Projected coverage inconsistent");
         sum_generation += metrics.simplification_ms;
         sum_metrics += metrics.metrics_ms;
         sum_total += metrics.total_ms;
      }
      require(session.generalizationTotalMs() >= sum_total, "Projected job timing inconsistent");
      std::cout << path.toStdString() << " " << name << ": gen=" << sum_generation
                << " ms, eval=" << sum_metrics << " ms, job=" << session.generalizationTotalMs()
                << " ms\n";
   }
}

void checkConfiguredScenarios(const QString& config)
{
   for (const auto& scenario : core::readScenarios(config))
   {
      core::Session session;
      MainWindow window(&session);
      window.show();
      require(session.loadScenario(config, scenario.id), "Cannot load real configured scenario");
      require(!session.hasGeneralization() && !session.isGeneralizing(), "Scenario auto-generated");
      auto* label = window.findChild<QLabel*>("scenarioNameLabel");
      require(label && label->text() == scenario.id && window.findChild<QAction*>("openScenarioAction"),
              "Scenario identity or open action missing");
      for (const auto& [algorithm, settings] : scenario.algorithms)
      {
         session.setAlgorithm(algorithm);
         for (const auto& spec : session.currentSpecs())
         {
            const auto& expected = settings.at(spec.name);
            require(spec.min_value == expected.min_value && spec.max_value == expected.max_value
                       && spec.step == expected.step && spec.logarithmic == expected.logarithmic
                       && session.currentParams().at(spec.name) == expected.initial,
                    "Scenario setting not applied");
         }
         auto* spin = window.findChild<QDoubleSpinBox*>();
         const auto& expected = settings.begin()->second;
         require(spin && spin->minimum() == expected.min_value && spin->maximum() == expected.max_value
                    && spin->value() == expected.initial, "UI does not match scenario");
         std::size_t vertices = 0;
         double fast_h = 0;
         for (const auto mode : {core::metrics::EvaluationMode::Fast,
                                core::metrics::EvaluationMode::Detailed})
         {
            session.setEvaluationSettings({mode, 8});
            session.regenerateAsync();
            // Редактирование настройки во время работы не изменяет снимок задачи.
            session.setEvaluationSettings({mode, 5});
            awaitIdle(session);
            const auto &m = session.layerMetrics(0);
            const auto stride = mode == core::metrics::EvaluationMode::Fast ? 8u : 1u;
            require(session.hasGeneralization() && m.sample_stride == stride &&
                       session.resultEvaluationSettings().fast_stride == 8,
                    "Scenario evaluation snapshot failed");
            const auto reference = core::metrics::evaluate(session.data().layers[0].features,
                                                           session.simplified(0), 0, stride);
            require((std::isnan(m.hausdorff) && std::isnan(reference.hausdorff)) ||
                       std::abs(m.hausdorff - reference.hausdorff) < 1e-9,
                    "Worker did not apply selected evaluation stride");
            if (mode == core::metrics::EvaluationMode::Fast)
            {
               vertices = m.result_vertices;
               fast_h = m.hausdorff;
            }
            else
               require(m.result_vertices == vertices &&
                          (!std::isfinite(fast_h) || m.hausdorff + 1e-9 >= fast_h),
                       "Detailed evaluation changed simplification or missed sampled maximum");
            std::cout << scenario.id.toStdString() << " " << algorithm << " step=" << stride
                      << " H=" << m.hausdorff << " Avg=" << m.average_deviation
                      << " Eval=" << m.metrics_ms << " ms\n";
         }
      }
      std::cout << "Configured scenario verified: " << scenario.id.toStdString() << '\n';
   }
}

void checkLargeCancellation(const QString &path)
{
   core::Session session;
   session.loadPath(path);
   require(!session.data().layers.empty(), "Large dataset failed to load");
   // Прямой вызов исключает фолбэк воркера: ошибка обхода сетки провалит тест.
   const auto li = gen::makeAlgorithm("li-openshaw");
   std::size_t original_vertices = 0, result_vertices = 0;
   for (const auto& layer : session.data().layers)
      for (const auto& feature : layer.features)
      {
         original_vertices += core::metrics::countVertices(feature.geometry);
         result_vertices += core::metrics::countVertices(li->simplify(feature.geometry, {{"window_size", 0.05}}));
      }
   require(result_vertices > 0, "Li-Openshaw produced no real-data vertices");
   std::cout << "Real-data Li-Openshaw vertices: " << original_vertices << " -> " << result_vertices << '\n';

   MainWindow window(&session);
   window.show();
   session.setEvaluationSettings({core::metrics::EvaluationMode::Detailed, 8});
   bool metrics_started = false;
   QElapsedTimer cancellation_time;
   QObject::connect(&session, &core::Session::generalizationProgressChanged,
                    [&](qint64, qint64, const QString &, const QString &phase, qint64, qint64)
                    {
                       if (!metrics_started && phase == QStringLiteral("Evaluating metrics"))
                       {
                          metrics_started = true;
                          // Дать метрикам время войти в вычисления, затем остановить через UI.
                          QTimer::singleShot(
                             10, &window,
                             [&]
                             {
                                cancellation_time.start();
                                window.findChild<QPushButton *>("cancelButton")->click();
                             });
                       }
                    });
   window.findChild<QPushButton *>("generateButton")->click();
   awaitIdle(session);
   require(metrics_started && cancellation_time.isValid() && cancellation_time.elapsed() < 5000,
           "Cancellation during large dataset metrics timed out");
   require(!session.hasGeneralization(), "Large cancelled result was published");
   std::cout << "Large dataset metric cancellation: " << cancellation_time.elapsed() << " ms\n";
}

} // namespace

int main(int argc, char **argv)
{
   QApplication application(argc, argv);
   try
   {
      checkLiOpenshawGrid();
      checkAlgorithmsAndMetrics();
      checkDistanceCoverageUi();
      QTemporaryDir directory;
      require(directory.isValid(), "Cannot create temporary directory");
      checkSessionAndUi(writeFixture(directory));
      if (argc == 3 && QString::fromLocal8Bit(argv[1]) == "--scenarios")
      {
         checkConfiguredScenarios(QString::fromLocal8Bit(argv[2]));
         std::cout << "PASS: configured scenarios and UI\n";
         return 0;
      }
      if (argc == 3 && QString::fromLocal8Bit(argv[1]) == "--cancel-metrics")
      {
         checkLargeCancellation(QString::fromLocal8Bit(argv[2]));
         return 0;
      }
      if (argc > 1)
         checkSessionAndUi(QString::fromLocal8Bit(argv[1]));
      if (argc > 2)
         checkLargeCancellation(QString::fromLocal8Bit(argv[2]));
      for (int i = 3; i < argc; ++i)
         checkProjectedScenario(QString::fromLocal8Bit(argv[i]));
      std::cout << "PASS: algorithms, metrics, progress, cancellation, queued restart, snapshots, "
                   "UI, reload, shutdown\n";
      return 0;
   }
   catch (const std::exception &error)
   {
      std::cerr << error.what() << '\n';
      return 1;
   }
}
