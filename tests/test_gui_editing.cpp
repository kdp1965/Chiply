// Part editing on the reference design: drag, nudge, rotate, delete,
// duplicate, undo/redo, and the saved file.
#include "EditorSession.h"
#include "SchematicItems.h"
#include "SchematicView.h"
#include "MiniToolbar.h"
#include "core/Geometry.h"
#include "core/JsonFormat.h"
#include "core/WokwiJson.h"

#include <QElapsedTimer>
#include <QScrollBar>
#include <QGraphicsScene>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>
#include <map>

using namespace chiply;

class EditingTest : public QObject {
    Q_OBJECT
    EditorSession* s = nullptr;
    SchematicView* v = nullptr;
    std::string original;

    const Part& part(const char* id) { return *s->document().findPart(id); }
    QRectF outline(const char* id)
    {
        Rect r = partBounds(part(id), *PartLibrary::builtin().find(part(id).type));
        return QRectF(r.x, r.y, r.w, r.h);
    }
    QPoint at(QPointF p) { return v->mapFromScene(p); }
    void click(QPointF p, Qt::KeyboardModifiers m = {}) { QTest::mouseClick(v->viewport(), Qt::LeftButton, m, at(p)); }
    void drag(QPointF a, QPointF b, Qt::KeyboardModifiers m = {})
    {
        QTest::mousePress(v->viewport(), Qt::LeftButton, {}, at(a));
        for (int i = 1; i <= 8; ++i)
            QTest::mouseMove(v->viewport(), at(a + (b - a) * i / 8.0));
        if (m) // modifiers can change mid-drag
            QTest::mouseMove(v->viewport(), at(b));
        QTest::mouseRelease(v->viewport(), Qt::LeftButton, m, at(b));
    }
    WireItem* wireItem(int idx)
    {
        for (QGraphicsItem* it : v->scene()->items())
            if (it->type() == WireItem::Type && static_cast<WireItem*>(it)->index() == idx)
                return static_cast<WireItem*>(it);
        return nullptr;
    }

private slots:
    void initTestCase()
    {
        s = new EditorSession(this);
        s->load(QStringLiteral(CHIPLY_REFERENCE_DIR "/wokwi_414123795172381697.diagram.json"));
        original = saveWokwi(s->document());
        v = s->view();
        v->resize(1400, 900);
        v->show();
        QVERIFY(QTest::qWaitForWindowExposed(v));
        v->centerOn(outline("flop238").center());
    }

    void dragMovesPartOnGridWithWires()
    {
        const Part before = part("flop238");
        const QPointF c = outline("flop238").center();
        drag(c, c + QPointF(3 * 9.6 + 2, 2 * 9.6 - 3)); // a bit off-grid
        const Part& after = part("flop238");
        // Grabbed origin snapped to the grid, moved 3 right and 2 down.
        QCOMPARE(after.left, round2(std::round((before.left + 3 * 9.6 + 2) / 9.6) * 9.6));
        QCOMPARE(after.top, round2(std::round((before.top + 2 * 9.6 - 3) / 9.6) * 9.6));
        QVERIFY(s->isModified());
        // Every wire on the part still ends on its pins.
        for (std::size_t i = 0; i < s->document().wires.size(); ++i) {
            const Wire& w = s->document().wires[i];
            if (w.from.part != "flop238" && w.to.part != "flop238")
                continue;
            auto r = wireItem(int(i))->route();
            auto a = *pinPosition(s->document(), PartLibrary::builtin(), w.from);
            auto b = *pinPosition(s->document(), PartLibrary::builtin(), w.to);
            QVERIFY(std::fabs(r.front().x - round2(a.x)) < 0.02 && std::fabs(r.back().y - round2(b.y)) < 0.02);
        }
        s->undoStack()->undo();
        QCOMPARE(part("flop238").left, before.left);
        QCOMPARE(part("flop238").top, before.top);
    }

    void dragMovesWholeSelection()
    {
        click(outline("flop238").center());
        click(outline("flop239").center(), Qt::ShiftModifier);
        const double l238 = part("flop238").left, l239 = part("flop239").left;
        const QPointF c = outline("flop239").center();
        const int steps = s->undoStack()->index();
        drag(c, c + QPointF(4 * 9.6, 0));
        QCOMPARE(part("flop238").left - l238, part("flop239").left - l239); // rigid
        QVERIFY(part("flop239").left != l239);
        QCOMPARE(s->undoStack()->index(), steps + 1); // one undo step for the whole drag
        s->undoStack()->undo();
    }

    void escCancelsDrag()
    {
        const double l = part("flop240").left;
        const QPointF c = outline("flop240").center();
        QTest::mousePress(v->viewport(), Qt::LeftButton, {}, at(c));
        for (int i = 1; i <= 5; ++i)
            QTest::mouseMove(v->viewport(), at(c + QPointF(10.0 * i, 0)));
        QVERIFY(part("flop240").left != l); // live preview
        QTest::keyClick(v, Qt::Key_Escape);
        QTest::mouseRelease(v->viewport(), Qt::LeftButton, {}, at(c + QPointF(50, 0)));
        QCOMPARE(part("flop240").left, l);
    }

    void arrowsNudgeSelectionShiftByFive()
    {
        click(outline("flop238").center());
        const double l = part("flop238").left, t = part("flop238").top;
        QTest::keyClick(v, Qt::Key_Right);
        QCOMPARE(part("flop238").left, round2(l + 9.6));
        QTest::keyClick(v, Qt::Key_Down, Qt::ShiftModifier);
        QCOMPARE(part("flop238").top, round2(t + 5 * 9.6));
        QTest::keyClick(v, Qt::Key_Up, Qt::ShiftModifier);
        QTest::keyClick(v, Qt::Key_Left);
        QCOMPARE(part("flop238").left, l);
        QCOMPARE(part("flop238").top, t);
        // Without a selection the arrows pan instead.
        v->clearSelection();
        const int before = v->horizontalScrollBar()->value();
        QTest::keyClick(v, Qt::Key_Right);
        QVERIFY(v->horizontalScrollBar()->value() != before);
        QCOMPARE(part("flop238").left, l);
    }

    void rotateWithR()
    {
        click(outline("flop238").center());
        QTest::keyClick(v, Qt::Key_R);
        QCOMPARE(part("flop238").rotate, 90);
        s->undoStack()->undo();
        QCOMPARE(part("flop238").rotate, 0);
    }

    void deleteAndUndo()
    {
        const std::size_t parts = s->document().parts.size(), wires = s->document().wires.size();
        click(outline("flop238").center());
        QElapsedTimer t;
        t.start();
        QTest::keyClick(v, Qt::Key_Delete);
        qInfo("delete + scene rebuild: %lld ms", t.elapsed());
        QVERIFY(!s->document().findPart("flop238"));
        QCOMPARE(s->document().parts.size(), parts - 1);
        QVERIFY(s->document().wires.size() < wires);
        s->undoStack()->undo();
        QCOMPARE(s->document().parts.size(), parts);
        QCOMPARE(s->document().wires.size(), wires);
        QCOMPARE(s->selectedPartIds(), std::vector<std::string>{"flop238"});
    }

    void duplicateSelectsTheCopy()
    {
        click(outline("flop238").center());
        click(outline("flop239").center(), Qt::ShiftModifier);
        const std::size_t parts = s->document().parts.size();
        QTest::keyClick(v, Qt::Key_D);
        QCOMPARE(s->document().parts.size(), parts + 2);
        const auto sel = s->selectedPartIds();
        QCOMPARE(sel.size(), size_t(2));
        for (const auto& id : sel)
            QVERIFY(id != "flop238" && id != "flop239");
        s->undoStack()->undo();
        QCOMPARE(s->document().parts.size(), parts);
    }

    void placeANewPart()
    {
        v->clearSelection();
        const QPointF where = outline("flop238").center() + QPointF(-200, 0);
        s->startPlacing("wokwi-gate-and-2");
        QVERIFY(s->placing());
        QTest::mouseMove(v->viewport(), at(where));
        QTest::mouseClick(v->viewport(), Qt::LeftButton, {}, at(where));
        QVERIFY(!s->placing());
        const Part* p = s->document().findPart("and326"); // next free AND id
        QVERIFY(p);
        QCOMPARE(p->type, std::string("wokwi-gate-and-2"));
        // Centered under the cursor, origin on the grid.
        QCOMPARE(std::fmod(std::fabs(p->left), 9.6) < 0.01 || std::fabs(std::fmod(std::fabs(p->left), 9.6) - 9.6) < 0.01, true);
        QVERIFY(std::fabs(p->left + 48 - where.x()) <= 4.8 + 1);
        QCOMPARE(s->selectedPartIds(), std::vector<std::string>{"and326"});
        s->undoStack()->undo();
        QVERIFY(!s->document().findPart("and326"));
        // Esc cancels placing without changing anything.
        s->startPlacing("wokwi-mux-2");
        QTest::keyClick(v, Qt::Key_Escape);
        QVERIFY(!s->placing());
        QVERIFY(!s->document().findPart("mux63"));
    }

    void junctionPinLandsOnGrid()
    {
        auto onGrid = [](double v) { const double r = std::fmod(std::fabs(v), 9.6); return r < 0.02 || r > 9.58; };
        v->clearSelection();
        const QPointF where = outline("flop238").center() + QPointF(-157.3, 41.9); // deliberately off-grid
        s->startPlacing("wokwi-junction");
        QTest::mouseMove(v->viewport(), at(where));
        QTest::mouseClick(v->viewport(), Qt::LeftButton, {}, at(where));
        const auto sel = s->selectedPartIds();
        QCOMPARE(sel.size(), size_t(1));
        const std::string id = sel.front();
        auto pin = [&] { return *pinPosition(s->document(), PartLibrary::builtin(), PinRef{id, "J"}); };
        QVERIFY2(onGrid(pin().x) && onGrid(pin().y), "placed junction pin on grid");
        // Dragging keeps it on the grid.
        const QPointF c(pin().x, pin().y);
        QGraphicsItem* item = nullptr;
        for (QGraphicsItem* it : v->scene()->items())
            if (itemPartId(it) == id)
                item = it;
        QVERIFY(item);
        drag(c + QPointF(1, 1), c + QPointF(23.7, -13.1));
        QVERIFY2(onGrid(pin().x) && onGrid(pin().y), "dragged junction pin on grid");
        QVERIFY(std::fabs(pin().x - c.x()) > 1); // it did move
        // A plain click on the junction starts a wire from it.
        QTest::mouseClick(v->viewport(), Qt::LeftButton, {}, at(QPointF(pin().x, pin().y)));
        QVERIFY(v->drawingWire());
        QTest::keyClick(v, Qt::Key_Escape);
        QVERIFY(!v->drawingWire());
        s->undoStack()->undo();
        s->undoStack()->undo();
    }

    void renameValidatesAndRewritesWires()
    {
        QVERIFY(!s->renamePart("flop238", "2bad").isEmpty());
        QVERIFY(!s->renamePart("flop238", "module").isEmpty());
        QVERIFY(!s->renamePart("flop238", "flop239").isEmpty()); // taken
        std::size_t refs = 0;
        for (const Wire& w : s->document().wires)
            refs += (w.from.part == "flop238") + (w.to.part == "flop238");
        QVERIFY(s->renamePart("flop238", "cmp_val_reg").isEmpty());
        QVERIFY(!s->document().findPart("flop238"));
        std::size_t after = 0;
        for (const Wire& w : s->document().wires)
            after += (w.from.part == "cmp_val_reg") + (w.to.part == "cmp_val_reg");
        QCOMPARE(after, refs);
        QCOMPARE(s->selectedPartIds(), std::vector<std::string>{"cmp_val_reg"});
        s->undoStack()->undo();
        QVERIFY(s->document().findPart("flop238"));
    }

    void attributeAndWireColorEdits()
    {
        s->setPartAttr("clock1", "frequency", "20000");
        QCOMPARE(s->document().findPart("clock1")->attrs["frequency"].get<std::string>(), std::string("20000"));
        s->undoStack()->undo();
        QCOMPARE(s->document().findPart("clock1")->attrs["frequency"].get<std::string>(), std::string("10000"));
        s->setWireColor(0, "blue");
        QCOMPARE(s->document().wires[0].color, std::string("blue"));
        s->undoStack()->undo();
        QCOMPARE(s->document().wires[0].color, std::string("red"));
    }

    void miniToolbarOnSingleSelection()
    {
        click(outline("flop238").center());
        QApplication::processEvents();
        QVERIFY(s->miniToolbar()->isVisible());
        click(outline("flop239").center(), Qt::ShiftModifier);
        QVERIFY(!s->miniToolbar()->isVisible());
        v->clearSelection();
        QVERIFY(!s->miniToolbar()->isVisible());
    }

    // flop54 moved right one step: its wires stretch at the segment nearest
    // the flop; long runs elsewhere (e.g. the vertical into the mux) stay put.
    void elasticWiresOnNudge()
    {
        v->clearSelection();
        QGraphicsItem* item = nullptr;
        for (QGraphicsItem* it : v->scene()->items())
            if (itemPartId(it) == "flop54")
                item = it;
        QVERIFY(item);
        v->centerOn(item);
        v->selectOnly(item);
        std::map<int, std::vector<Point>> before;
        for (std::size_t i = 0; i < s->document().wires.size(); ++i) {
            const Wire& w = s->document().wires[i];
            if ((w.from.part == "flop54") != (w.to.part == "flop54"))
                before[int(i)] = simplifyPolyline(wireItem(int(i))->route());
        }
        QVERIFY(!before.empty());
        QTest::keyClick(v, Qt::Key_Right);
        for (const auto& [i, old] : before) {
            const auto now = simplifyPolyline(wireItem(i)->route());
            const Wire& w = s->document().wires[size_t(i)];
            const bool atStart = w.from.part == "flop54";
            // Every corner of the old route except those before the first
            // horizontal segment from flop54 is still there, unchanged.
            std::vector<Point> o = old, n = now;
            if (!atStart) {
                std::reverse(o.begin(), o.end());
                std::reverse(n.begin(), n.end());
            }
            std::size_t k = 0;
            while (k + 1 < o.size() && std::fabs(o[k].y - o[k + 1].y) > 0.005)
                ++k;
            for (std::size_t j = k + 1; j < o.size(); ++j)
                QVERIFY2(std::find_if(n.begin(), n.end(), [&](const Point& p) {
                             return std::fabs(p.x - o[j].x) < 0.02 && std::fabs(p.y - o[j].y) < 0.02;
                         }) != n.end(),
                         qPrintable(QString::fromStdString(w.from.str() + " -> " + w.to.str())));
            // The flop end moved by one grid step.
            QVERIFY(std::fabs(n.front().x - (o.front().x + 9.6)) < 0.02);
        }
        s->undoStack()->undo();
        for (const auto& [i, old] : before)
            QCOMPARE(simplifyPolyline(wireItem(i)->route()), old);
    }

    void undoEverythingRestoresTheFile()
    {
        while (s->undoStack()->canUndo())
            s->undoStack()->undo();
        QCOMPARE(saveWokwi(s->document()), original);
        QVERIFY(!s->isModified());
    }

    void dragPastTheEdgeScrollsTheCanvas()
    {
        // Moving a part to the right edge of the view and holding it there
        // scrolls the canvas, and the part keeps following the cursor.
        v->resetTransform();
        v->scale(1.5, 1.5);
        const QPointF c = outline("flop238").center();
        v->centerOn(c);
        const double left0 = s->document().findPart("flop238")->left;
        const int h0 = v->horizontalScrollBar()->value();
        const QPoint start = v->mapFromScene(c);
        const QPoint edge(v->viewport()->width() - 3, start.y());
        QTest::mousePress(v->viewport(), Qt::LeftButton, {}, start);
        for (int i = 1; i <= 10; ++i)
            QTest::mouseMove(v->viewport(), start + (edge - start) * i / 10);
        QTest::qWait(400); // the cursor rests at the edge
        QVERIFY(v->horizontalScrollBar()->value() > h0 + 50);
        QTest::mouseRelease(v->viewport(), Qt::LeftButton, {}, edge);
        const double moved = s->document().findPart("flop238")->left - left0;
        // Farther than the cursor moved on screen: the scroll was added.
        QVERIFY(moved > (edge.x() - start.x()) / 1.5 + 40);
        s->undoStack()->undo();
        QCOMPARE(s->document().findPart("flop238")->left, left0);

        // Drawing a wire towards the bottom edge scrolls too.
        v->centerOn(c);
        const int vv0 = v->verticalScrollBar()->value();
        const chiply::Point q = *chiply::pinPosition(s->document(), chiply::PartLibrary::builtin(), *chiply::PinRef::parse("flop238:Q"));
        const QPoint pinAt = v->mapFromScene(QPointF(q.x, q.y));
        QTest::mouseClick(v->viewport(), Qt::LeftButton, {}, pinAt); // start drawing
        QVERIFY(v->drawingWire());
        const QPoint bottom(pinAt.x(), v->viewport()->height() - 3);
        for (int i = 1; i <= 10; ++i)
            QTest::mouseMove(v->viewport(), pinAt + (bottom - pinAt) * i / 10);
        QTest::qWait(400);
        QVERIFY(v->verticalScrollBar()->value() > vv0 + 50);
        v->cancelWire();
    }
};

QTEST_MAIN(EditingTest)
#include "test_gui_editing.moc"
