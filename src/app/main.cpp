#include "MainWindow.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QTimer>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("Chiply");
    QApplication::setOrganizationName("Chiply");
    QApplication::setApplicationVersion(CHIPLY_VERSION);

    QCommandLineParser cli;
    cli.setApplicationDescription("Wokwi-compatible logic schematic editor");
    cli.addHelpOption();
    cli.addVersionOption();
    QCommandLineOption shot("screenshot",
        "Render the window to <file> (PNG) after loading, then exit. Works with "
        "QT_QPA_PLATFORM=offscreen for headless use.", "file");
    QCommandLineOption size("size", "Window size for --screenshot, e.g. 1400x900.", "WxH", "1400x900");
    cli.addOption(shot);
    cli.addOption(size);
    cli.addPositionalArgument("files", "Diagram files to open, one tab each.", "[files...]");
    cli.process(app);

    MainWindow w;
    if (cli.isSet(shot)) {
        const QStringList wh = cli.value(size).split('x');
        if (wh.size() == 2)
            w.resize(wh[0].toInt(), wh[1].toInt());
    }
    w.show();
    for (const QString& path : cli.positionalArguments())
        w.openFile(path);

    if (cli.isSet(shot)) {
        const QString file = cli.value(shot);
        QTimer::singleShot(300, &w, [&w, file] {
            w.refitAll();
            QTimer::singleShot(200, &w, [&w, file] {
                const bool ok = w.grab().save(file);
                QApplication::exit(ok ? 0 : 1);
            });
        });
    }
    return app.exec();
}
