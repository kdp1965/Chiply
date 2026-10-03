// Main window layout: selecting must not resize docks or shift the canvas.
#include "EditorSession.h"
#include "Inspector.h"
#include "MainWindow.h"
#include "SchematicItems.h"
#include "SchematicView.h"
#include <QGraphicsScene>
#include "core/Geometry.h"

#include <QTabWidget>
#include <QTest>

class WindowTest : public QObject {
    Q_OBJECT
private slots:
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
