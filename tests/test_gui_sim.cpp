// Simulation mode in the real window, on the Tiny Tapeout template.
#include "EditorSession.h"
#include "MainWindow.h"
#include "SchematicItems.h"
#include "SchematicView.h"
#include "SimRunner.h"
#include "core/WokwiJson.h"

#include <QGraphicsScene>
#include <QMenuBar>
#include <QTabWidget>
#include <QTest>
#include <QSettings>
#include <QToolBar>

using namespace chiply;
using chiply::sim::V;

class SimGuiTest : public QObject {
    Q_OBJECT
    MainWindow* w = nullptr;
    EditorSession* s = nullptr;
    SchematicView* v = nullptr;
    std::string original;

    PartItem* part(const std::string& id)
    {
        for (QGraphicsItem* it : v->scene()->items())
            if (it->type() == PartItem::Type && static_cast<PartItem*>(it)->partId() == id)
                return static_cast<PartItem*>(it);
        return nullptr;
    }
    QPoint at(PartItem* p, QPointF local)
    {
        v->centerOn(p);
        return v->mapFromScene(p->mapToScene(local));
    }
    QAction* action(const char* name) { return w->findChild<QAction*>(name); }
    V val(const char* pin) { return s->sim()->simulator().value(*PinRef::parse(pin)); }

private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName("ChiplyTest");
        QCoreApplication::setApplicationName("ChiplySimGuiTest");
        w = new MainWindow;
        w->resize(1400, 900);
        w->show();
        QVERIFY(QTest::qWaitForWindowExposed(w));
        QVERIFY(w->openFile(QStringLiteral(CHIPLY_REFERENCE_DIR "/tt_template_354858054593504257.diagram.json")));
        QApplication::processEvents();
        v = qobject_cast<SchematicView*>(w->findChild<QTabWidget*>()->currentWidget());
        s = qobject_cast<EditorSession*>(v->property("session").value<QObject*>());
        QVERIFY(v && s);
        original = saveWokwi(s->document());
        v->resetTransform();
        v->scale(2, 2);
    }

    void playEntersSimulationMode()
    {
        QVERIFY(!s->simulating());
        action("playAction")->trigger();
        QVERIFY(s->simulating());
        QVERIFY(s->sim()->running());
        QVERIFY(v->simMode());
        QCOMPARE(action("playAction")->property("running").toBool(), true); // shows Pause
        QVERIFY(action("stopAction")->isEnabled());
        QTest::qWait(120);
        QVERIFY(s->sim()->now() > 0); // time advances
    }

    void clickingDipSwitchChangesTheDisplay()
    {
        PartItem* dip = part("sw1");
        QVERIFY(dip);
        QCOMPARE(*s->sim()->simulator().segments("sevseg1"), 0x0Fu);
        QTest::mouseClick(v->viewport(), Qt::LeftButton, {}, at(dip, QPointF(8.1, 28))); // switch 1
        QCOMPARE(s->sim()->simulator().switchState("sw1", 0), std::optional<bool>(true));
        QCOMPARE(val("ttin:IN0"), V::H);
        QCOMPARE(*s->sim()->simulator().segments("sevseg1"), 0x0Eu); // segment A dark
        QVERIFY(part("sevseg1")->simActive());
        // The wire into the first inverter now shows a high value.
        bool sawHigh = false;
        for (QGraphicsItem* it : v->scene()->items())
            if (it->type() == WireItem::Type && static_cast<WireItem*>(it)->simValue() == 1) {
                sawHigh = true;
                QVERIFY(it->toolTip().endsWith(" = 1")); // value in the tooltip
            }
        QVERIFY(sawHigh);
    }

    void resetButtonHeldWithTheMouse()
    {
        PartItem* btn = part("btn2");
        const QPoint p = at(btn, QPointF(34, 22));
        QCOMPARE(val("ttin:RST_N"), V::H);
        QTest::mousePress(v->viewport(), Qt::LeftButton, {}, p);
        QCOMPARE(val("ttin:RST_N"), V::L);
        QTest::mouseRelease(v->viewport(), Qt::LeftButton, {}, p);
        QCOMPARE(val("ttin:RST_N"), V::H);
    }

    void stepKeyPressesTheStepButton()
    {
        // Template's Step button has key "s"; the slide switch routes it to CLK.
        v->setFocus();
        QTest::keyPress(v, Qt::Key_S, {}, 0);
        QCOMPARE(val("ttin:CLK"), V::H);
        QTest::keyRelease(v, Qt::Key_S, {}, 0);
        QCOMPARE(val("ttin:CLK"), V::L);
    }

    void clickingTheSlideSwitchMovesIt()
    {
        PartItem* sw = part("sw2");
        QCOMPARE(s->sim()->simulator().switchState("sw2"), std::optional<bool>(true));
        QTest::mouseClick(v->viewport(), Qt::LeftButton, {}, at(sw, QPointF(16, 14)));
        QCOMPARE(s->sim()->simulator().switchState("sw2"), std::optional<bool>(false));
    }

    void noEditingWhileSimulating()
    {
        PartItem* gate = part("not1");
        QTest::mouseClick(v->viewport(), Qt::LeftButton, {}, at(gate, QPointF(40, 19)));
        QVERIFY(s->selectedPartIds().empty());
        QTest::keyClick(v, Qt::Key_Delete);
        QVERIFY(s->document().findPart("not1"));
        bool editEnabled = false;
        for (QAction* a : w->menuBar()->actions())
            if (a->text().contains("Edit"))
                for (QAction* e : a->menu()->actions())
                    if (e->text().contains("Paste") || e->text().contains("Delete"))
                        editEnabled |= e->isEnabled();
        QVERIFY(!editEnabled);
    }

    void pauseAndStep()
    {
        action("playAction")->trigger(); // pause
        QVERIFY(!s->sim()->running());
        QVERIFY(action("stepAction")->isEnabled());
        const auto t0 = s->sim()->now();
        action("stepAction")->trigger();
        QCOMPARE(s->sim()->now() - t0, chiply::sim::Time(100'000'000)); // one 10 kHz period
    }

    void stopReturnsToEditingAndLeavesTheFileAlone()
    {
        action("stopAction")->trigger();
        QVERIFY(!s->simulating());
        QVERIFY(!v->simMode());
        for (QGraphicsItem* it : v->scene()->items())
            if (it->type() == WireItem::Type)
                QCOMPARE(static_cast<WireItem*>(it)->simValue(), -1);
        QVERIFY(!part("sw1")->simActive());
        QCOMPARE(saveWokwi(s->document()), original);
        QVERIFY(!s->isModified());
        // Clicking selects again (the output block: the template's NOT
        // gates overlap each other).
        QTest::mouseClick(v->viewport(), Qt::LeftButton, {}, at(part("ttout"), QPointF(50, 50)));
        QCOMPARE(s->selectedPartIds(), std::vector<std::string>{"ttout"});
        QSettings().clear();
    }
};

QTEST_MAIN(SimGuiTest)
#include "test_gui_sim.moc"
