#include "mainwindow.h"
#include "core/session.h"

#include <QApplication>
#include <QTimer>

int main(int argc, char** argv)
{
   QApplication app(argc, argv);

   const auto arguments = app.arguments();
   const bool scenario_mode = arguments.value(1) == QStringLiteral("--scenario");
   if (scenario_mode && arguments.size() != 4)
   {
      qCritical("Usage: gen-optimizer --scenario CONFIG.json SCENARIO_ID");
      return 2;
   }
   const QString path = arguments.value(scenario_mode ? 2 : 1);
   const QString filter = arguments.value(scenario_mode ? 3 : 2);

   core::Session session;
   MainWindow    window(&session);
   window.show();

   if (!path.isEmpty())
   {
      QTimer::singleShot(0, &session, [&session, path, filter, scenario_mode] {
         if (scenario_mode) session.loadScenario(path, filter);
         else session.loadPath(path, filter);
      });
   }

   return app.exec();
}
