// Drawing new wires from pins on the reference design.
#include "EditorSession.h"
#include "SchematicItems.h"
#include "SchematicView.h"
#include "core/Geometry.h"

#include <QGraphicsScene>
#include <QTest>

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
};

QTEST_MAIN(WireDrawTest)
#include "test_gui_wires.moc"
