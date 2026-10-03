// Part editing on the reference design: drag, nudge, rotate, delete,
// duplicate, undo/redo, and the saved file.
#include "EditorSession.h"
#include "SchematicItems.h"
#include "SchematicView.h"
#include "core/Geometry.h"
#include "core/JsonFormat.h"
#include "core/WokwiJson.h"

#include <QElapsedTimer>
#include <QScrollBar>
#include <QGraphicsScene>
#include <QTemporaryDir>
#include <QTest>

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

    void undoEverythingRestoresTheFile()
    {
        while (s->undoStack()->canUndo())
            s->undoStack()->undo();
        QCOMPARE(saveWokwi(s->document()), original);
        QVERIFY(!s->isModified());
    }
};

QTEST_MAIN(EditingTest)
#include "test_gui_editing.moc"
