#include "MainWindow.h"

#include <QApplication>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("Chiply");
    QApplication::setOrganizationName("Chiply");
    QApplication::setApplicationVersion(CHIPLY_VERSION);

    MainWindow w;
    w.show();
    for (const QString& path : QApplication::arguments().mid(1))
        w.openFile(path);
    return app.exec();
}
