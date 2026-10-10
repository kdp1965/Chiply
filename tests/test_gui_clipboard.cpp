// Copy / paste / Alt-drag duplicate on the reference design and across tabs.
#include "EditorSession.h"
#include "SchematicItems.h"
#include "SchematicView.h"
#include "core/Edit.h"
#include "core/Geometry.h"
#include "core/WokwiJson.h"

#include <QGraphicsScene>
#include <QSettings>
#include <QSignalSpy>
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
        QCoreApplication::setOrganizationName("ChiplyTest");
        QCoreApplication::setApplicationName("ChiplyClipboardTest");
        QSettings().clear();
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

    void selectedWiresToOtherPartsComeAlong()
    {
        // flop238 plus one of its wires to a part that is not selected: the
        // wire pastes too, its far end connecting to the pin it lands on.
        std::size_t wi = s->document().wires.size();
        for (std::size_t i = 0; i < s->document().wires.size() && wi == s->document().wires.size(); ++i) {
            const Wire& w = s->document().wires[i];
            if ((w.from.part == "flop238") != (w.to.part == "flop238"))
                wi = i;
        }
        QVERIFY(wi < s->document().wires.size());
        const Wire orig = s->document().wires[wi];
        const PinRef far = orig.from.part == "flop238" ? orig.to : orig.from;
        v->clearSelection();
        itemOf("flop238")->setSelected(true);
        for (QGraphicsItem* it : v->scene()->items())
            if (it->type() == WireItem::Type && static_cast<WireItem*>(it)->index() == int(wi))
                it->setSelected(true);
        emit v->selectionEdited();
        const QString text = s->copySelection();
        QVERIFY(text.contains("\"end\""));
        const std::size_t parts = s->document().parts.size(), wires = s->document().wires.size();
        // Pasted exactly over the original, the end lands on the far pin.
        const auto frag = fragmentFromText(text.toStdString());
        QVERIFY(frag);
        const Point o = fragmentOrigin(*frag);
        const QPointF anchor(o.x, o.y);
        auto rep = s->paste(text, anchor, false);
        QVERIFY(rep.error.isEmpty());
        QCOMPARE(rep.parts, 1);
        QCOMPARE(rep.ends, 1);
        QCOMPARE(s->document().parts.size(), parts + 2); // the flop and its placeholder float
        QSignalSpy resolved(s, &EditorSession::pasteEndsResolved);
        QTest::mouseClick(v->viewport(), Qt::LeftButton, {}, v->mapFromScene(anchor));
        QVERIFY(!s->pasteFloating());
        QCOMPARE(resolved.count(), 1);
        QCOMPARE(resolved.first().at(0).toInt(), 1);
        QCOMPARE(resolved.first().at(1).toInt(), 0);
        QCOMPARE(s->document().parts.size(), parts + 1); // placeholder gone
        QCOMPARE(s->document().wires.size(), wires + 1);
        const Wire& w = s->document().wires.back();
        QVERIFY((w.from.str() == far.str()) != (w.to.str() == far.str()));
        QVERIFY(w.from.part != "flop238" && w.to.part != "flop238"); // the copy, not the original
        for (const Part& p : s->document().parts)
            QVERIFY(!isEndPlaceholder(p));
        s->undoStack()->undo();
        QCOMPARE(saveWokwi(s->document()), original);
        // Dropped in empty space, the end stays as a junction to wire up.
        rep = s->paste(text, anchor + QPointF(4000, 4000), false);
        QVERIFY(rep.error.isEmpty());
        QTest::mouseClick(v->viewport(), Qt::LeftButton, {}, v->mapFromScene(anchor + QPointF(4000, 4000)));
        QVERIFY(!s->pasteFloating());
        QCOMPARE(resolved.count(), 2);
        QCOMPARE(resolved.last().at(1).toInt(), 1);
        QCOMPARE(s->document().parts.size(), parts + 2);
        QVERIFY(s->document().parts.back().type == "wokwi-junction");
        QVERIFY(!isEndPlaceholder(s->document().parts.back()));
        s->undoStack()->undo();
        QCOMPARE(saveWokwi(s->document()), original);
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

    void pasteNameFormatStepsTheNumber()
    {
        // state_reg_0..2 exist: with "state_reg_#", a copy of state_reg_0
        // pastes as state_reg_3 (not state_reg_0_1).
        EditorSession::setPasteNameFormat(QStringLiteral("state_reg_#"));
        select({"state_reg_0"});
        auto rep = s->paste(s->copySelection(), centerOf("flop238") + QPointF(0, 300), false);
        QVERIFY(rep.error.isEmpty());
        QCOMPARE(s->selectedPartIds(), std::vector<std::string>{"state_reg_3"});
        QTest::keyClick(v, Qt::Key_Escape); // cancel the floating paste
        QVERIFY(!s->document().findPart("state_reg_3"));
        // An invalid format (no #) is ignored: the usual _1.
        EditorSession::setPasteNameFormat(QStringLiteral("state_reg_*"));
        select({"state_reg_0"});
        s->paste(s->copySelection(), centerOf("flop238") + QPointF(0, 300), false);
        QCOMPARE(s->selectedPartIds(), std::vector<std::string>{"state_reg_0_1"});
        QTest::keyClick(v, Qt::Key_Escape);
        EditorSession::setPasteNameFormat(QString());
    }

    void findAndReplaceInSelectedNames()
    {
        select({"loop_reg_0", "loop_reg_1", "loop_reg_2"});
        std::size_t wiresOn = 0;
        for (const Wire& w : s->document().wires)
            wiresOn += w.from.part.rfind("loop_reg_", 0) == 0 || w.to.part.rfind("loop_reg_", 0) == 0;
        QVERIFY(wiresOn > 0);
        QString err;
        QCOMPARE(s->previewReplaceInNames("loop", "iter", &err).size(), std::size_t(3));
        QVERIFY(s->previewReplaceInNames("loop_reg_0", "state_reg_0", &err).empty()); // taken
        QVERIFY(err.contains("already used"));
        QVERIFY(s->previewReplaceInNames("loop", "2x", &err).empty()); // not a Verilog name
        int changed = 0;
        QCOMPARE(s->replaceInNames("loop", "iter", &changed), QString());
        QCOMPARE(changed, 3);
        QVERIFY(s->document().findPart("iter_reg_1") && !s->document().findPart("loop_reg_1"));
        std::size_t wiresNow = 0;
        for (const Wire& w : s->document().wires) {
            QVERIFY(w.from.part.rfind("loop_reg_", 0) != 0 && w.to.part.rfind("loop_reg_", 0) != 0);
            wiresNow += w.from.part.rfind("iter_reg_", 0) == 0 || w.to.part.rfind("iter_reg_", 0) == 0;
        }
        QCOMPARE(wiresNow, wiresOn); // the wires followed
        auto sel = s->selectedPartIds();
        std::sort(sel.begin(), sel.end());
        QCOMPARE(sel, (std::vector<std::string>{"iter_reg_0", "iter_reg_1", "iter_reg_2"}));
        s->undoStack()->undo(); // one step
        QVERIFY(s->document().findPart("loop_reg_1"));
        QCOMPARE(saveWokwi(s->document()), original);
        QSettings().clear();
    }
};

QTEST_MAIN(ClipboardTest)
#include "test_gui_clipboard.moc"
