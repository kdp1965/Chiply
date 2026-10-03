// Copy / paste / Alt-drag duplicate on the reference design and across tabs.
#include "EditorSession.h"
#include "SchematicItems.h"
#include "SchematicView.h"
#include "core/Geometry.h"
#include "core/WokwiJson.h"

#include <QGraphicsScene>
#include <QTest>

using namespace chiply;

class ClipboardTest : public QObject {
    Q_OBJECT
    EditorSession* s = nullptr;
    SchematicView* v = nullptr;
    std::string original;

    QGraphicsItem* itemOf(const std::string& id)
    {
        for (QGraphicsItem* it : v->scene()->items())
            if (itemPartId(it) == id)
                return it;
        return nullptr;
    }
    QPointF centerOf(const std::string& id)
    {
        const Part* p = s->document().findPart(id);
        Rect r = partBounds(*p, *PartLibrary::builtin().find(p->type));
        return {r.x + r.w / 2, r.y + r.h / 2};
    }
    void select(std::initializer_list<const char*> ids)
    {
        v->clearSelection();
        for (const char* id : ids)
            itemOf(id)->setSelected(true);
        emit v->selectionEdited();
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
        v->centerOn(centerOf("flop238"));
    }

    void copyPasteFloatAndDrop()
    {
        select({"flop238", "flop239"});
        const QString text = s->copySelection();
        QVERIFY(text.contains("flop238") && text.contains("connections"));
        const std::size_t parts = s->document().parts.size(), wires = s->document().wires.size();
        const QPointF anchor = centerOf("flop238") + QPointF(-300, 0);
        auto rep = s->paste(text, anchor, false);
        QVERIFY(rep.error.isEmpty());
        QCOMPARE(rep.parts, 2);
        QCOMPARE(rep.renamed, 2); // flop238/239 are auto ids: always renumbered
        QVERIFY(s->pasteFloating());
        QCOMPARE(s->document().parts.size(), parts + 2);
        QVERIFY(s->document().wires.size() > wires); // the wire between them came along
        const auto ids = s->selectedPartIds();
        QCOMPARE(ids.size(), size_t(2));
        // Move the mouse: the pasted parts follow; click drops them.
        const QPointF drop = anchor + QPointF(5 * 9.6, 3 * 9.6);
        const double before = s->document().findPart(ids[0])->left;
        QTest::mouseMove(v->viewport(), v->mapFromScene(anchor + QPointF(10, 10)));
        QTest::mouseMove(v->viewport(), v->mapFromScene(drop));
        QTest::mouseClick(v->viewport(), Qt::LeftButton, {}, v->mapFromScene(drop));
        QVERIFY(!s->pasteFloating());
        const double after = s->document().findPart(ids[0])->left;
        QVERIFY(std::fabs(after - before - 5 * 9.6) < 9.7);
        const std::string dropped = saveWokwi(s->document());
        // One undo step removes the whole paste; redo puts it back where dropped.
        s->undoStack()->undo();
        QCOMPARE(saveWokwi(s->document()), original);
        s->undoStack()->redo();
        QCOMPARE(saveWokwi(s->document()), dropped);
        s->undoStack()->undo();
    }

    void escCancelsPaste()
    {
        select({"flop238"});
        s->paste(s->copySelection(), centerOf("flop238") + QPointF(0, 200), false);
        QVERIFY(s->pasteFloating());
        QTest::keyClick(v, Qt::Key_Escape);
        QVERIFY(!s->pasteFloating());
        QCOMPARE(saveWokwi(s->document()), original);
    }

    void undoWhileFloatingEndsThePaste()
    {
        select({"flop238"});
        s->paste(s->copySelection(), centerOf("flop238") + QPointF(0, 200), false);
        QVERIFY(s->pasteFloating());
        s->undoStack()->undo();
        QVERIFY(!s->pasteFloating());
        QCOMPARE(saveWokwi(s->document()), original);
        // A click afterwards is an ordinary click again.
        QTest::mouseClick(v->viewport(), Qt::LeftButton, {}, v->mapFromScene(centerOf("flop238")));
        QCOMPARE(s->selectedPartIds(), std::vector<std::string>{"flop238"});
    }

    void pasteIntoAnotherTabSkipsExistingTtBlocks()
    {
        select({"ttin", "flop238", "state_reg_2"});
        const QString text = s->copySelection();
        EditorSession other;
        other.load(QStringLiteral(CHIPLY_REFERENCE_DIR "/tt_template_354858054593504257.diagram.json"));
        other.view()->resize(800, 600);
        QCOMPARE(other.existingTtBlocksIn(text), QStringList{"board-tt-block-input"});
        const std::size_t parts = other.document().parts.size();
        auto rep = other.paste(text, QPointF(0, 0), true);
        QVERIFY(rep.error.isEmpty());
        QCOMPARE(rep.skippedBlocks, 1);
        QCOMPARE(other.document().parts.size(), parts + 2);
        QVERIFY(other.document().findPart("flop1"));       // renumbered for the template
        QVERIFY(other.document().findPart("state_reg_2")); // custom name kept
        // Without skipping, the block comes along under a new id.
        other.cancelPlacing();
        auto rep2 = other.paste(text, QPointF(0, 0), false);
        QCOMPARE(rep2.skippedBlocks, 0);
        QCOMPARE(other.document().parts.size(), parts + 3);
        other.cancelPlacing();
    }

    void altDragDuplicates()
    {
        v->centerOn(centerOf("flop240"));
        select({"flop240"});
        const std::size_t parts = s->document().parts.size();
        const double l = s->document().findPart("flop240")->left;
        const QPointF c = centerOf("flop240");
        QTest::mousePress(v->viewport(), Qt::LeftButton, Qt::AltModifier, v->mapFromScene(c));
        for (int i = 1; i <= 6; ++i)
            QTest::mouseMove(v->viewport(), v->mapFromScene(c + QPointF(8.0 * 9.6 * i / 6, 0)));
        QTest::mouseRelease(v->viewport(), Qt::LeftButton, Qt::AltModifier, v->mapFromScene(c + QPointF(8 * 9.6, 0)));
        QCOMPARE(s->document().parts.size(), parts + 1);
        QCOMPARE(s->document().findPart("flop240")->left, l); // original stays
        const auto sel = s->selectedPartIds();
        QCOMPARE(sel.size(), size_t(1));
        QVERIFY(sel[0] != "flop240");
        QVERIFY(s->document().findPart(sel[0])->left > l + 30); // the copy moved
        s->undoStack()->undo();
        s->undoStack()->undo();
        QCOMPARE(saveWokwi(s->document()), original);
    }
};

QTEST_MAIN(ClipboardTest)
#include "test_gui_clipboard.moc"
