// Drawing new wires from pins on the reference design.
#include "EditorSession.h"
#include "SchematicItems.h"
#include "SchematicView.h"
#include "core/Geometry.h"

#include <QGraphicsScene>
#include <QTemporaryDir>
#include <QFile>
#include <QTest>

#include <cmath>

using namespace chiply;

class WireDrawTest : public QObject {
    Q_OBJECT
    EditorSession* s = nullptr;
    SchematicView* v = nullptr;

    QPointF pin(const char* ref)
    {
        auto r = PinRef::parse(ref);
        Point p = *pinPosition(s->document(), PartLibrary::builtin(), *r);
        return {p.x, p.y};
    }
    QPoint at(QPointF p) { return v->mapFromScene(p); }
    void click(QPointF p, Qt::MouseButton b = Qt::LeftButton)
    {
        QTest::mouseMove(v->viewport(), at(p));
        QTest::mouseClick(v->viewport(), b, {}, at(p));
    }
    const Wire& lastWire() { return s->document().wires.back(); }
    std::vector<Point> route(const Wire& w)
    {
        return simplifyPolyline(routePolyline(*pinPosition(s->document(), PartLibrary::builtin(), w.from),
                                              *pinPosition(s->document(), PartLibrary::builtin(), w.to), w.path));
    }

private slots:
    void initTestCase()
    {
        s = new EditorSession(this);
        s->load(QStringLiteral(CHIPLY_REFERENCE_DIR "/wokwi_414123795172381697.diagram.json"));
        v = s->view();
        v->resize(1400, 900);
        v->show();
        QVERIFY(QTest::qWaitForWindowExposed(v));
    }

    void clickPinBendPin()
    {
        const QPointF a = pin("flop238:NOTQ"), b = pin("flop239:D");
        v->centerOn((a + b) / 2);
        const std::size_t wires = s->document().wires.size();
        click(a);
        QVERIFY(v->drawingWire());
        QVERIFY(s->selectedPartIds().empty()); // the pin click did not select flop238
        // A detour: bend to the left of both pins, half-way down.
        const QPointF bend(std::min(a.x(), b.x()) - 4 * 9.6, (a.y() + b.y()) / 2);
        click(bend);
        QVERIFY(v->drawingWire());
        click(b);
        QVERIFY(!v->drawingWire());
        QCOMPARE(s->document().wires.size(), wires + 1);
        const Wire& w = lastWire();
        QCOMPARE(w.from.str(), std::string("flop238:NOTQ"));
        QCOMPARE(w.to.str(), std::string("flop239:D"));
        QCOMPARE(w.color, std::string("green"));
        const auto r = route(w);
        QVERIFY(r.size() >= 3);
        // The route passes through the (grid-snapped) bend point.
        const double bx = std::round(bend.x() / 9.6) * 9.6, by = std::round(bend.y() / 9.6) * 9.6;
        bool through = false;
        for (std::size_t i = 0; i + 1 < r.size(); ++i) {
            const Point& p = r[i];
            const Point& q = r[i + 1];
            through |= std::fabs(p.x - q.x) < 0.01 && std::fabs(p.x - bx) < 0.01 && std::min(p.y, q.y) - 0.01 <= by
                && by <= std::max(p.y, q.y) + 0.01;
            through |= std::fabs(p.y - q.y) < 0.01 && std::fabs(p.y - by) < 0.01 && std::min(p.x, q.x) - 0.01 <= bx
                && bx <= std::max(p.x, q.x) + 0.01;
        }
        if (!through) {
            QString d = QString("bend %1,%2 a %3,%4 b %5,%6 route:").arg(bx).arg(by).arg(a.x()).arg(a.y()).arg(b.x()).arg(b.y());
            for (const Point& q : r)
                d += QString(" (%1,%2)").arg(q.x).arg(q.y);
            QFAIL(qPrintable(d));
        }
        QCOMPARE(s->selectionSummary().wires, 1); // new wire selected
        s->undoStack()->undo();
        QCOMPARE(s->document().wires.size(), wires);
    }

    void escAndRightClickCancel()
    {
        const std::size_t wires = s->document().wires.size();
        click(pin("flop238:NOTQ"));
        QVERIFY(v->drawingWire());
        QTest::keyClick(v, Qt::Key_Escape);
        QVERIFY(!v->drawingWire());
        click(pin("flop238:NOTQ"));
        click(pin("flop238:NOTQ") + QPointF(40, 40), Qt::RightButton);
        QVERIFY(!v->drawingWire());
        QCOMPARE(s->document().wires.size(), wires);
    }

    void colorKeyWhileDrawing()
    {
        const QPointF a = pin("flop238:NOTQ"), b = pin("flop239:D");
        click(a);
        QTest::keyClick(v, Qt::Key_6);
        click(b);
        QCOMPARE(lastWire().color, std::string("blue"));
        s->undoStack()->undo();
    }

    void gndWiresStartBlack()
    {
        const QPointF g = pin("pwr2:GND");
        v->centerOn(g);
        click(g);
        QVERIFY(v->drawingWire());
        click(pin("sevseg1:DP"));
        QCOMPARE(lastWire().color, std::string("black"));
        s->undoStack()->undo();
        v->centerOn(pin("flop238:NOTQ"));
    }

    void newWireTakesTheColourOfThePinsConnection()
    {
        // flop238:NOTQ has no wire yet: the first one is the default green.
        const QPointF a = pin("flop238:NOTQ"), b = pin("flop239:D");
        for (const Wire& w : s->document().wires)
            QVERIFY(w.from.str() != "flop238:NOTQ" && w.to.str() != "flop238:NOTQ");
        click(a);
        click(b);
        QCOMPARE(lastWire().color, std::string("green"));
        // Recolour it; a second wire from the same pin starts in that colour.
        s->setWireColor(int(s->document().wires.size()) - 1, "magenta");
        click(a);
        QVERIFY(v->drawingWire());
        click(pin("flop239:CLK"));
        QCOMPARE(lastWire().from.str(), std::string("flop238:NOTQ"));
        QCOMPARE(lastWire().color, std::string("magenta"));
        // So does a wire started from the pin at the first wire's other end.
        click(b);
        click(pin("flop238:CLK"));
        QCOMPARE(lastWire().from.str(), std::string("flop239:D"));
        QCOMPARE(lastWire().color, std::string("magenta"));
        // A colour key still overrides while drawing.
        click(a);
        QTest::keyClick(v, Qt::Key_6);
        click(pin("flop240:D"));
        QCOMPARE(lastWire().color, std::string("blue"));
        // The most recent connection on the pin decides.
        click(a);
        click(pin("flop240:CLK"));
        QCOMPARE(lastWire().color, std::string("blue"));
        for (int i = 0; i < 6; ++i)
            s->undoStack()->undo();
    }

    void pressDragRelease()
    {
        const QPointF a = pin("flop238:NOTQ"), b = pin("flop239:D");
        const std::size_t wires = s->document().wires.size();
        QTest::mousePress(v->viewport(), Qt::LeftButton, {}, at(a));
        for (int i = 1; i <= 6; ++i)
            QTest::mouseMove(v->viewport(), at(a + (b - a) * i / 6.0));
        QTest::mouseRelease(v->viewport(), Qt::LeftButton, {}, at(b));
        QVERIFY(!v->drawingWire());
        QCOMPARE(s->document().wires.size(), wires + 1);
        QCOMPARE(lastWire().to.str(), std::string("flop239:D"));
        s->undoStack()->undo();
    }

    void colorKeysRecolorSelectedWires()
    {
        const QPointF a = pin("ttin:IN1");
        v->centerOn(a);
        click(a + QPointF(12, 0)); // on the ttin:IN1 -> flop238:D wire, off the pin
        QCOMPARE(s->selectionSummary().wires, 1);
        const int idx = s->selectedWireIndices().front();
        const std::string before = s->document().wires[size_t(idx)].color;
        QTest::keyClick(v, Qt::Key_M);
        QCOMPARE(s->document().wires[size_t(idx)].color, std::string("magenta"));
        QCOMPARE(s->selectionSummary().wires, 1); // still selected for more keys
        QTest::keyClick(v, Qt::Key_6);
        QCOMPARE(s->document().wires[size_t(idx)].color, std::string("blue"));
        s->undoStack()->undo();
        s->undoStack()->undo();
        QCOMPARE(s->document().wires[size_t(idx)].color, before);
    }

    void doubleClickDeletesWire()
    {
        const QPointF a = pin("ttin:IN1");
        v->centerOn(a);
        const std::size_t wires = s->document().wires.size();
        QTest::mouseDClick(v->viewport(), Qt::LeftButton, {}, at(a + QPointF(12, 0)));
        QCOMPARE(s->document().wires.size(), wires - 1);
        s->undoStack()->undo();
        QCOMPARE(s->document().wires.size(), wires);
    }

    void clickingPartBodyStillSelects()
    {
        const chiply::Part* p = s->document().findPart("flop238");
        Rect r = partBounds(*p, *PartLibrary::builtin().find(p->type));
        click(QPointF(r.x + r.w / 2, r.y + r.h / 2));
        QVERIFY(!v->drawingWire());
        QCOMPARE(s->selectedPartIds(), std::vector<std::string>{"flop238"});
    }

    void busRouteFromSelectedParts()
    {
        // A column of three AND gates (OUT at x 96; y 19.2, 76.8, 134.4) and a
        // column of three flops (D at x 384; y 201.6, 259.2, 316.8).
        std::string parts;
        for (int i = 0; i < 3; ++i) {
            parts += "{\"type\": \"wokwi-gate-and-2\", \"id\": \"g" + std::to_string(i) + "\", \"top\": "
                + std::to_string(i * 57.6) + ", \"left\": 0, \"attrs\": {}},";
            parts += "{\"type\": \"wokwi-flip-flop-d\", \"id\": \"f" + std::to_string(i) + "\", \"top\": "
                + std::to_string(192 + i * 57.6) + ", \"left\": 384, \"attrs\": {}}" + (i < 2 ? "," : "");
        }
        QTemporaryDir dir;
        const QString path = dir.filePath("bus.json");
        {
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(QByteArray::fromStdString("{\"version\": 1, \"author\": \"\", \"editor\": \"wokwi\", \"parts\": [" + parts
                                              + "], \"connections\": [], \"dependencies\": {}}"));
        }
        // Only one window on screen: simulated mouse moves go to the top
        // window under the cursor.
        v->hide();
        EditorSession es;
        es.load(path);
        SchematicView* bv = es.view();
        bv->resize(1000, 700);
        bv->show();
        QVERIFY(QTest::qWaitForWindowExposed(bv));
        bv->resetTransform();
        bv->centerOn(QPointF(240, 170));
        auto at2 = [&](double x, double y) { return bv->mapFromScene(QPointF(x, y)); };
        auto click2 = [&](double x, double y) {
            QTest::mouseMove(bv->viewport(), at2(x, y));
            QTest::mouseClick(bv->viewport(), Qt::LeftButton, {}, at2(x, y));
        };
        auto selectGates = [&] { bv->selectInRect(QRectF(-10, -10, 130, 180), false, false); };
        auto routeOf = [&](std::size_t i) {
            const Wire& w = es.document().wires[i];
            return simplifyPolyline(routePolyline(*pinPosition(es.document(), PartLibrary::builtin(), w.from),
                                                  *pinPosition(es.document(), PartLibrary::builtin(), w.to), w.path));
        };
        auto same = [](const std::vector<Point>& a, const std::vector<Point>& b) {
            if (a.size() != b.size())
                return false;
            for (std::size_t i = 0; i < a.size(); ++i)
                if (std::fabs(a[i].x - b[i].x) > 0.02 || std::fabs(a[i].y - b[i].y) > 0.02)
                    return false;
            return true;
        };
        QStringList hints;
        connect(bv, &SchematicView::hint, this, [&](const QString& h) { hints << h; });

        // Select the gates, click OUT of the bottom one: a bus of three, the
        // bottom gate is wire 1, then upwards.
        selectGates();
        QCOMPARE(es.selectedPartIds().size(), std::size_t(3));
        click2(96, 134.4);
        QVERIFY(bv->drawingWire());
        QCOMPARE(bv->busPhase(), 1);
        QCOMPARE(bv->busRemaining(), 3);
        QVERIFY(es.selectedPartIds().empty());
        QVERIFY(hints.last().contains("3 wires") && hints.last().contains("g2:OUT"));
        // Right to x 240, click (the turn), then down to the bottom flop's D.
        click2(240, 134.4);
        QTest::mouseMove(bv->viewport(), at2(384, 316.8));
        QCOMPARE(bv->targetPinTip(), QStringLiteral("f2:D"));
        QTest::mouseClick(bv->viewport(), Qt::LeftButton, {}, at2(384, 316.8));
        QCOMPARE(es.document().wires.size(), std::size_t(1));
        QCOMPARE(bv->busPhase(), 2); // the rest one by one
        QCOMPARE(bv->busRemaining(), 2);
        QVERIFY(bv->drawingWire());
        QVERIFY(hints.last().contains("wire 2 of 3") && hints.last().contains("g1:OUT"));
        click2(384, 259.2); // wire 2: the middle gate to the middle flop
        QCOMPARE(bv->busRemaining(), 1);
        click2(384, 201.6); // wire 3
        QVERIFY(!bv->drawingWire());
        QCOMPARE(bv->busPhase(), 0);
        QVERIFY(hints.last().isEmpty()); // the hint is cleared
        QCOMPARE(es.document().wires.size(), std::size_t(3));
        const auto& ws = es.document().wires;
        QCOMPARE(ws[0].from.str() + ">" + ws[0].to.str(), std::string("g2:OUT>f2:D"));
        QCOMPARE(ws[1].from.str() + ">" + ws[1].to.str(), std::string("g1:OUT>f1:D"));
        QCOMPARE(ws[2].from.str() + ">" + ws[2].to.str(), std::string("g0:OUT>f0:D"));
        // Vertical tracks one grid apart; the top gate's wire is the outer one.
        QVERIFY(same(routeOf(0), {{96, 134.4}, {240, 134.4}, {240, 316.8}, {384, 316.8}}));
        QVERIFY(same(routeOf(1), {{96, 76.8}, {249.6, 76.8}, {249.6, 259.2}, {384, 259.2}}));
        QVERIFY(same(routeOf(2), {{96, 19.2}, {259.2, 19.2}, {259.2, 201.6}, {384, 201.6}}));
        // Each connection is its own undo step.
        for (int i = 0; i < 3; ++i)
            es.undoStack()->undo();
        QVERIFY(es.document().wires.empty());

        // Esc after the first connection keeps it and drops the rest.
        selectGates();
        click2(96, 19.2); // the top gate first this time: numbered downwards
        QCOMPARE(bv->busPhase(), 1);
        click2(240, 19.2);
        click2(384, 201.6);
        QCOMPARE(bv->busRemaining(), 2);
        QVERIFY(hints.last().contains("g1:OUT")); // next: the gate below
        QTest::keyClick(bv, Qt::Key_Escape);
        QVERIFY(!bv->drawingWire());
        QCOMPARE(bv->busPhase(), 0);
        QCOMPARE(es.document().wires.size(), std::size_t(1));
        es.undoStack()->undo();

        // A colour key recolours every wire of the bus still to connect.
        selectGates();
        click2(96, 134.4);
        QTest::keyClick(bv, Qt::Key_6);
        click2(240, 134.4);
        click2(384, 316.8);
        click2(384, 259.2);
        click2(384, 201.6);
        QCOMPARE(es.document().wires.size(), std::size_t(3));
        for (const Wire& w : es.document().wires)
            QCOMPARE(w.color, std::string("blue"));
        for (int i = 0; i < 3; ++i)
            es.undoStack()->undo();

        // With the preference off, the same click draws a single wire.
        SchematicView::setBusRouting(false);
        selectGates();
        click2(96, 134.4);
        QVERIFY(bv->drawingWire());
        QCOMPARE(bv->busPhase(), 0);
        QTest::keyClick(bv, Qt::Key_Escape);
        SchematicView::setBusRouting(true);
        // One selected part (or a pin the others lack) is a single wire too.
        bv->selectInRect(QRectF(-10, -10, 130, 50), false, false);
        QCOMPARE(es.selectedPartIds().size(), std::size_t(1));
        click2(96, 19.2);
        QCOMPARE(bv->busPhase(), 0);
        QVERIFY(bv->drawingWire());

        // Undo or redo while a wire is being drawn rebuilds the scene; the
        // drawing carries on (this used to leave a dangling preview item).
        QTest::keyClick(bv, Qt::Key_Escape);
        click2(96, 19.2);
        click2(384, 201.6); // g0:OUT -> f0:D
        QCOMPARE(es.document().wires.size(), std::size_t(1));
        bv->clearSelection();
        click2(96, 76.8); // start g1:OUT
        QVERIFY(bv->drawingWire());
        es.undoStack()->undo(); // the first wire goes; the scene is rebuilt
        QVERIFY(es.document().wires.empty());
        QTest::mouseMove(bv->viewport(), at2(300, 150));
        QVERIFY(bv->drawingWire());
        click2(384, 259.2);
        QCOMPARE(es.document().wires.size(), std::size_t(1));
        QCOMPARE(es.document().wires[0].from.str(), std::string("g1:OUT"));
    }
};

QTEST_MAIN(WireDrawTest)
#include "test_gui_wires.moc"
