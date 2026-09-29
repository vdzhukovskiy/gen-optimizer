#include "mainwindow.h"

#include <QApplication>

int main(int argc, char** argv)
{
   QApplication app(argc, argv);

   const QString path = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString();
   MainWindow w(path);
   w.show();
   return app.exec();
}