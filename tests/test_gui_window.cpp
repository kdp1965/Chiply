// Main window layout: selecting must not resize docks or shift the canvas.
#include "EditorSession.h"
#include "Inspector.h"
#include "MainWindow.h"
#include "SchematicItems.h"
#include "SchematicView.h"
#include <QGraphicsScene>
#include "core/Geometry.h"

#include <QDockWidget>
#include <QSettings>
#include <QTabWidget>
#include <QTest>

class WindowTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase()
    {
        // Keep the tests away from the user's real preferences.
        QCoreApplication::setOrganizationName("ChiplyTest");
        QCoreApplication::setApplicationName("ChiplyWindowTest");
        QSettings().clear();
    }

    void layoutIsRestored()
    {
        {
            MainWindow w;
            w.show();
            QVERIFY(QTest::qWaitForWindowExposed(&w));
            auto* insp = w.findChild<QDockWidget*>("inspector");
            QVERIFY(insp);
            QCOMPARE(w.dockWidgetArea(insp), Qt::RightDockWidgetArea);
            w.addDockWidget(Qt::LeftDockWidgetArea, insp);
            w.resize(700, 590); // fits the 800x800 offscreen test screen
            QApplication::processEvents();
            QVERIFY(w.close());
        }
        MainWindow w2;
        w2.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w2));
        auto* insp2 = w2.findChild<QDockWidget*>("inspector");
        QCOMPARE(w2.dockWidgetArea(insp2), Qt::LeftDockWidgetArea);
        QCOMPARE(w2.size(), QSize(700, 590));
        w2.close();
        QSettings().clear();
    }

    void inspectorIsResizableAndSticky()
    {
        int chosen = 0;
        {
            MainWindow w;
            w.resize(780, 600);
            w.show();
            QVERIFY(QTest::qWaitForWindowExposed(&w));
            QVERIFY(w.openFile(QStringLiteral(CHIPLY_REFERENCE_DIR "/wokwi_414123795172381697.diagram.json")));
            QApplication::processEvents();
            auto* dock = w.findChild<QDockWidget*>("inspector");
            w.resizeDocks({dock}, {240}, Qt::Horizontal); // what dragging the splitter does
            QApplication::processEvents();
            chosen = dock->width();
            QVERIFY2(chosen < 300, qPrintable(QString::number(chosen)));
            // Selecting parts with wide forms must not change it.
            auto* v = qobject_cast<SchematicView*>(w.findChild<QTabWidget*>()->currentWidget());
            for (QGraphicsItem* it : v->scene()->items()) {
                if (itemPartId(it) == "ttin" || itemPartId(it) == "clock1") {
                    v->selectOnly(it);
                    QApplication::processEvents();
                    QCOMPARE(dock->width(), chosen);
                }
            }
            QVERIFY(w.close());
        }
        MainWindow w2;
        w2.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w2));
        QApplication::processEvents();
        QCOMPARE(w2.findChild<QDockWidget*>("inspector")->width(), chosen);
        w2.close();
        QSettings().clear();
    }

    void selectionDoesNotShiftCanvas()
    {
        MainWindow w;
        w.resize(1500, 950);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        QVERIFY(w.openFile(QStringLiteral(CHIPLY_REFERENCE_DIR "/wokwi_414123795172381697.diagram.json")));
        QApplication::processEvents(); // let the replaced Untitled tab go away
        auto* v = qobject_cast<SchematicView*>(w.findChild<QTabWidget*>()->currentWidget());
        auto* s = qobject_cast<EditorSession*>(v->property("session").value<QObject*>());
        QVERIFY(v && s);
        QApplication::processEvents();
        const QRect before = v->geometry();
        const QRect inspBefore = w.findChild<Inspector*>()->geometry();

        const std::vector<std::string> ids{"flop238", "clock1", "ttin", "sevseg1", "text5"};
        for (const std::string& id : ids) {
            QGraphicsItem* item = nullptr;
            for (QGraphicsItem* it : v->scene()->items())
                if (itemPartId(it) == id)
                    item = it;
            QVERIFY2(item, id.c_str());
            v->centerOn(item);
            v->selectOnly(item);
            QApplication::processEvents();
            QCOMPARE(s->selectedPartIds(), std::vector<std::string>{id});
            QCOMPARE(v->geometry(), before);
            QCOMPARE(w.findChild<Inspector*>()->geometry().width(), inspBefore.width());
        }
        v->clearSelection();
        QApplication::processEvents();
        QCOMPARE(v->geometry(), before);
    }
};

QTEST_MAIN(WindowTest)
#include "test_gui_window.moc"
