#include "core/generalization_worker.h"
#include "core/session.h"
#include "mainwindow.h"

#include <QApplication>
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
   require(QLineF(anchor, view->mapToScene(cursor)).length() < 3.0 / initial_scale,
           "Zoom did not preserve cursor anchor");
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
   require(progress_updates > 0 && heartbeat > 0, "Missing progress or GUI heartbeat");
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
   generate->click(); // Последняя заявка заменяет предыдущую.
   window.findChild<QDoubleSpinBox *>()->setValue(1.0); // Не изменяет снимок заявки.
   awaitIdle(session);
   require(done == 2 && starts == 4 && cancelled == 2,
           "Restart launched duplicate tasks or published partial result");
   require(session.resultAlgorithm() == "li-openshaw" &&
              session.resultParams().at("window_size") == 0.5,
           "Restart did not use latest requested snapshot");
   require(!session.resultMatchesSettings(), "Result confused with edited settings");
   bool labelled = false;
   for (auto *label : window.findChildren<QLabel *>())
      labelled |=
         label->text().contains("window_size=0.5") && label->text().contains("settings changed");
   require(labelled, "Displayed result settings missing");

   generate->click();
   generate->click();
   cancel->click(); // Cancel также удаляет отложенную заявку.
   const auto retained_vertices = session.layerMetrics(0).result_vertices;
   const auto retained_items = window.findChild<QGraphicsView *>()->scene()->items();
   awaitIdle(session);
   require(done == 2 && !session.hasPendingRestart(), "Cancel did not clear queued restart");
   require(session.layerMetrics(0).result_vertices == retained_vertices &&
              window.findChild<QGraphicsView *>()->scene()->items() == retained_items,
           "Cancel changed previous metrics or scene");
   require(!previous_items.empty() && previous_vertices > 0,
           "Original completed scene was missing");
   require(!cancel->isEnabled() && generate->isEnabled(),
           "Controls not restored after cancellation");

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

void checkLargeCancellation(const QString &path)
{
   core::Session session;
   session.loadPath(path);
   require(!session.data().layers.empty(), "Large dataset failed to load");
   MainWindow window(&session);
   window.show();
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
      checkAlgorithmsAndMetrics();
      QTemporaryDir directory;
      require(directory.isValid(), "Cannot create temporary directory");
      checkSessionAndUi(writeFixture(directory));
      if (argc > 1)
         checkSessionAndUi(QString::fromLocal8Bit(argv[1]));
      if (argc > 2)
         checkLargeCancellation(QString::fromLocal8Bit(argv[2]));
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
