// Selection behaviour on the reference design, driven with real mouse events.
#include "EditorSession.h"
#include "SchematicItems.h"
#include "SchematicView.h"
#include "Theme.h"
#include "core/Geometry.h"

#include <QGraphicsScene>
#include <QTest>

#include <algorithm>
#include <set>

using chiply::PartLibrary;

class SelectionTest : public QObject {
    Q_OBJECT
    EditorSession* s = nullptr;
    SchematicView* v = nullptr;

    QRectF outline(const char* id)
    {
        const chiply::Part* p = s->document().findPart(id);
        chiply::Rect r = chiply::partBounds(*p, *PartLibrary::builtin().find(p->type));
        return QRectF(r.x, r.y, r.w, r.h);
    }
    QPoint at(QPointF scene) { return v->mapFromScene(scene); }
    std::set<std::string> selected()
    {
        auto ids = s->selectedPartIds();
        return {ids.begin(), ids.end()};
    }
    void click(QPointF scene, Qt::KeyboardModifiers m = {})
    {
        QTest::mouseClick(v->viewport(), Qt::LeftButton, m, at(scene));
    }
    void drag(QPointF a, QPointF b, Qt::KeyboardModifiers m = {})
    {
        QTest::mousePress(v->viewport(), Qt::LeftButton, m, at(a));
        const QPoint pa = at(a), pb = at(b);
        for (int i = 1; i <= 8; ++i)
            QTest::mouseMove(v->viewport(), pa + (pb - pa) * i / 8);
        QTest::mouseRelease(v->viewport(), Qt::LeftButton, m, pb);
    }
    // A scene point near `near` with nothing selectable under it.
    QPointF emptyNear(QPointF near)
    {
        for (int r = 0; r < 400; r += 6)
            for (int dx : {-r, r})
                for (int dy : {-r, r}) {
                    QPointF p = near + QPointF(dx, dy);
                    if (!v->selectableAt(at(p)))
                        return p;
                }
        return near;
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
        v->centerOn(outline("flop238").center());
    }

    void clickSelectsOnlyThatPart()
    {
        click(outline("flop238").center());
        QCOMPARE(selected(), std::set<std::string>{"flop238"});
        QCOMPARE(s->selectionSummary().parts, 1);
        QVERIFY(s->selectionSummary().stretchWires > 0); // its wires will stretch
    }

    void shiftClickAddsAndToggles()
    {
        click(outline("flop239").center(), Qt::ShiftModifier);
        QCOMPARE(selected(), (std::set<std::string>{"flop238", "flop239"}));
        click(outline("flop239").center(), Qt::ShiftModifier);
        QCOMPARE(selected(), std::set<std::string>{"flop238"});
    }

    void ctrlClickToggles()
    {
        click(outline("flop239").center(), Qt::ControlModifier);
        QCOMPARE(selected().size(), size_t(2));
        click(outline("flop238").center(), Qt::ControlModifier);
        QCOMPARE(selected(), std::set<std::string>{"flop239"});
    }

    void plainClickReplaces()
    {
        click(outline("flop238").center());
        QCOMPARE(selected(), std::set<std::string>{"flop238"});
    }

    void clickOnEmptyCanvasClears()
    {
        click(emptyNear(outline("flop238").topLeft() - QPointF(30, 30)));
        QVERIFY(selected().empty());
        QVERIFY(s->selectionSummary().empty());
    }

    void escClears()
    {
        click(outline("flop238").center());
        QVERIFY(!selected().empty());
        QTest::keyClick(v, Qt::Key_Escape);
        QVERIFY(selected().empty());
    }

    void marqueeSelectsEnclosedOnly()
    {
        const QRectF a = outline("flop238"), b = outline("flop239");
        QRectF box = a.united(b).adjusted(-8, -8, 8, 8);
        drag(emptyNear(box.topLeft()), box.bottomRight());
        const auto sel = selected();
        QVERIFY(sel.count("flop238"));
        QVERIFY(sel.count("flop239"));
        // Everything selected lies inside the dragged rectangle.
        const QRectF dragged(emptyNear(box.topLeft()), box.bottomRight());
        for (const auto& id : sel) {
            const chiply::Part* p = s->document().findPart(id);
            if (p->type == "wokwi-text")
                continue;
            QVERIFY2(dragged.normalized().adjusted(-1, -1, 1, 1).contains(outline(id.c_str())), id.c_str());
        }
    }

    void ctrlMarqueeAdds()
    {
        click(outline("flop238").center());
        const QRectF b = outline("flop240").adjusted(-8, -8, 8, 8);
        drag(emptyNear(b.topLeft()), b.bottomRight(), Qt::ControlModifier);
        const auto sel = selected();
        QVERIFY(sel.count("flop238"));
        QVERIFY(sel.count("flop240"));
    }

    void altMarqueeSelectsTouched()
    {
        // Cover only the left half of the part: enclosed mode misses it,
        // crossing mode (Alt) catches it.
        const QRectF o = outline("flop240");
        const QPointF start = emptyNear(o.topLeft() - QPointF(20, 20));
        const QPointF end(o.center().x(), o.bottom() + 4);
        drag(start, end);
        QVERIFY(!selected().count("flop240"));
        drag(start, end, Qt::AltModifier);
        QVERIFY(selected().count("flop240"));
    }

    void implicitAndStretchWires()
    {
        // Select both ends of one wire: it becomes implicit.
        const chiply::Wire* w = nullptr;
        for (const chiply::Wire& x : s->document().wires)
            if (x.from.part == "flop238" || x.to.part == "flop238") {
                w = &x;
                break;
            }
        QVERIFY(w);
        v->clearSelection();
        const QRectF a = outline(w->from.part.c_str()), b = outline(w->to.part.c_str());
        v->centerOn(a.united(b).center());
        click(a.center());
        click(b.center(), Qt::ShiftModifier);
        const int idx = int(w - s->document().wires.data());
        WireItem* item = nullptr;
        for (QGraphicsItem* it : v->scene()->items())
            if (it->type() == WireItem::Type && static_cast<WireItem*>(it)->index() == idx)
                item = static_cast<WireItem*>(it);
        QVERIFY(item);
        QCOMPARE(int(item->link()), int(WireItem::Link::Implicit));
        QVERIFY(s->selectionSummary().implicitWires >= 1);
    }

    void selectAllAndSurviveThemeChange()
    {
        v->selectAll();
        const auto sum = s->selectionSummary();
        QCOMPARE(sum.parts, int(s->document().parts.size()));
        QCOMPARE(sum.wires, int(s->document().wires.size()));
        v->clearSelection();
        click(outline("flop238").center());
        Theme::instance().setModeForSession(Theme::instance().isDark() ? Theme::Mode::Light : Theme::Mode::Dark);
        QCOMPARE(selected(), std::set<std::string>{"flop238"});
    }

    void clickOnWireSelectsIt()
    {
        v->clearSelection();
        const chiply::Wire* w = nullptr;
        for (const chiply::Wire& x : s->document().wires)
            if (x.from.str() == "ttin:IN1" && x.to.str() == "flop238:D")
                w = &x;
        QVERIFY(w);
        chiply::Point a = *chiply::pinPosition(s->document(), PartLibrary::builtin(), w->from);
        v->centerOn(QPointF(a.x, a.y));
        click(QPointF(a.x + 12, a.y));
        QCOMPARE(s->selectionSummary().wires, 1);
        QCOMPARE(s->selectionSummary().parts, 0);
        const QString shot = qEnvironmentVariable("CHIPLY_SELECT_SHOT");
        if (!shot.isEmpty()) {
            v->clearSelection();
            v->centerOn(outline("flop238").center());
            const QRectF box = outline("flop238").united(outline("flop239")).adjusted(-8, -8, 8, 8);
            drag(emptyNear(box.topLeft()), box.bottomRight());
            QApplication::processEvents();
            v->grab().save(shot);
        }
    }
};

QTEST_MAIN(SelectionTest)
#include "test_gui_selection.moc"
