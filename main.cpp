#include "mainwindow.h"
#include "core/session.h"

#include <QApplication>
#include <QTimer>

int main(int argc, char** argv)
{
   QApplication app(argc, argv);

   const QString path   = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString();
   const QString filter = argc > 2 ? QString::fromLocal8Bit(argv[2]) : QString();

   core::Session session;
   MainWindow    window(&session);
   window.show();

   if (!path.isEmpty())
   {
      QTimer::singleShot(0, &session, [&session, path, filter] {
         session.loadPath(path, filter);
      });
   }

   return app.exec();
}