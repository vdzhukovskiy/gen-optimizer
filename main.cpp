#include "mainwindow.h"

#include <QApplication>

int main(int argc, char** argv)
{
   QApplication app(argc, argv);

   // argv[1] — файл или директория.
   // argv[2] — опциональный фильтр по имени слоя (подстрока, без учёта регистра).
   // Например: ./gen-optimizer ~/work/data/10m_physical coastline
   const QString path   = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString();
   const QString filter = argc > 2 ? QString::fromLocal8Bit(argv[2]) : QString();

   MainWindow w(path, filter);
   w.show();
   return app.exec();
}