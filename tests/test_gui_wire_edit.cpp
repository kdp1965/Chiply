// Wire segment handles: show on selection, drag to reroute, undo/redo, save.
#include "EditorSession.h"
#include "SchematicItems.h"
#include "SchematicView.h"
#include "core/Geometry.h"
#include "core/JsonFormat.h"
#include "core/WokwiJson.h"

#include <QGraphicsScene>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>

using namespace chiply;

class WireEditTest : public QObject {
    Q_OBJECT
    EditorSession* s = nullptr;
    SchematicView* v = nullptr;
    int idx = -1;

    WireItem* item()
    {
        for (QGraphicsItem* it : v->scene()->items())
            if (it->type() == WireItem::Type && static_cast<WireItem*>(it)->index() == idx)
                return static_cast<WireItem*>(it);
        return nullptr;
    }
    std::vector<SegmentHandle*> handles()
    {
        std::vector<SegmentHandle*> out;
        for (QGraphicsItem* c : item()->childItems())
            if (c->type() == SegmentHandle::Type)
                out.push_back(static_cast<SegmentHandle*>(c));
        return out;
    }
    std::vector<Point> docRoute()
    {
        const Wire& w = s->document().wires[size_t(idx)];
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
        for (std::size_t i = 0; i < s->document().wires.size(); ++i)
            if (s->document().wires[i].from.str() == "ttin:IN1" && s->document().wires[i].to.str() == "flop238:D")
                idx = int(i);
        QVERIFY(idx >= 0);
    }

    void handlesAppearOnSelection()
    {
        QVERIFY(handles().empty());
        Point a = *pinPosition(s->document(), PartLibrary::builtin(), s->document().wires[size_t(idx)].from);
        v->centerOn(QPointF(a.x, a.y));
        QTest::mouseClick(v->viewport(), Qt::LeftButton, {}, v->mapFromScene(QPointF(a.x + 12, a.y)));
        QCOMPARE(s->selectionSummary().wires, 1);
        const auto r = simplifyPolyline(item()->route());
        QCOMPARE(handles().size(), r.size() - 1); // one per segment
    }

    void draggingAHandleReroutesAndUndoes()
    {
        const std::vector<Point> before = docRoute();
        const std::vector<std::string> beforePath = formatWirePath(s->document().wires[size_t(idx)].path);
        // Pick a middle segment if there is one, else the first.
        auto hs = handles();
        SegmentHandle* h = hs[hs.size() > 2 ? 1 : 0];
        const std::size_t seg = h->segment();
        const bool horiz = h->horizontal();
        const QPointF hp = h->scenePos();
        v->centerOn(hp);
        const QPointF target = hp + (horiz ? QPointF(0, 2 * SchematicView::kGrid) : QPointF(2 * SchematicView::kGrid, 0));
        QTest::mousePress(v->viewport(), Qt::LeftButton, {}, v->mapFromScene(hp));
        for (int i = 1; i <= 6; ++i)
            QTest::mouseMove(v->viewport(), v->mapFromScene(hp + (target - hp) * i / 6.0));
        QTest::mouseRelease(v->viewport(), Qt::LeftButton, {}, v->mapFromScene(target));

        const std::vector<Point> after = docRoute();
        QVERIFY(after != before);
        // The dragged segment now sits on the grid line two steps over.
        const double want = std::round(((horiz ? hp.y() : hp.x()) + 2 * SchematicView::kGrid) / SchematicView::kGrid)
            * SchematicView::kGrid;
        const auto expect = simplifyPolyline(moveSegment(before, seg, round2(want)));
        QCOMPARE(after.size(), expect.size());
        for (std::size_t i = 0; i < after.size(); ++i) {
            QVERIFY(std::fabs(after[i].x - expect[i].x) < 0.02);
            QVERIFY(std::fabs(after[i].y - expect[i].y) < 0.02);
        }
        QVERIFY(s->isModified());
        QCOMPARE(simplifyPolyline(item()->route()), after); // scene matches document

        s->undoStack()->undo();
        QCOMPARE(formatWirePath(s->document().wires[size_t(idx)].path), beforePath);
        QCOMPARE(docRoute(), before);
        QVERIFY(!s->isModified());
        s->undoStack()->redo();
        QCOMPARE(docRoute(), after);
    }

    void cornerHandleMovesCorner()
    {
        v->clearSelection();
        Point a = *pinPosition(s->document(), PartLibrary::builtin(), s->document().wires[size_t(idx)].from);
        v->centerOn(QPointF(a.x, a.y));
        QTest::mouseClick(v->viewport(), Qt::LeftButton, {}, v->mapFromScene(QPointF(a.x + 12, a.y)));
        CornerHandle* corner = nullptr;
        for (QGraphicsItem* c : item()->childItems())
            if (c->type() == CornerHandle::Type && !corner)
                corner = static_cast<CornerHandle*>(c);
        QVERIFY(corner);
        const std::vector<Point> before = docRoute();
        const QPointF cp = corner->scenePos();
        const QPointF target = cp + QPointF(2 * 9.6, 9.6);
        v->centerOn(cp);
        QTest::mousePress(v->viewport(), Qt::LeftButton, {}, v->mapFromScene(cp));
        for (int i = 1; i <= 5; ++i)
            QTest::mouseMove(v->viewport(), v->mapFromScene(cp + (target - cp) * i / 5.0));
        QTest::mouseRelease(v->viewport(), Qt::LeftButton, {}, v->mapFromScene(target));
        const std::vector<Point> after = docRoute();
        QVERIFY(after != before);
        const double tx = round2(std::round(target.x() / 9.6) * 9.6), ty = round2(std::round(target.y() / 9.6) * 9.6);
        QVERIFY(std::any_of(after.begin(), after.end(),
                            [&](const Point& p) { return std::fabs(p.x - tx) < 0.02 && std::fabs(p.y - ty) < 0.02; }));
        QCOMPARE(after.front(), before.front()); // pins untouched
        QCOMPARE(after.back(), before.back());
        s->undoStack()->undo();
        QCOMPARE(docRoute(), before);
    }

    void ctrlDragSplitsSegment()
    {
        const std::vector<Point> before = docRoute();
        // Longest segment.
        std::size_t seg = 0;
        double best = 0;
        for (std::size_t i = 0; i + 1 < before.size(); ++i) {
            const double l = std::fabs(before[i + 1].x - before[i].x) + std::fabs(before[i + 1].y - before[i].y);
            if (l > best) {
                best = l;
                seg = i;
            }
        }
        // A quarter of the way along (the middle is the segment handle).
        v->clearSelection();
        const QPointF mid(before[seg].x + (before[seg + 1].x - before[seg].x) / 4,
                          before[seg].y + (before[seg + 1].y - before[seg].y) / 4);
        const bool horiz = std::fabs(before[seg].y - before[seg + 1].y) < 0.01;
        const QPointF target = mid + (horiz ? QPointF(0, 3 * 9.6) : QPointF(3 * 9.6, 0));
        v->centerOn(mid);
        QTest::mousePress(v->viewport(), Qt::LeftButton, Qt::ControlModifier, v->mapFromScene(mid));
        for (int i = 1; i <= 5; ++i)
            QTest::mouseMove(v->viewport(), v->mapFromScene(mid + (target - mid) * i / 5.0));
        QTest::mouseRelease(v->viewport(), Qt::LeftButton, Qt::ControlModifier, v->mapFromScene(target));
        const std::vector<Point> after = docRoute();
        QCOMPARE(after.size(), before.size() + 2); // one step: two new corners
        QCOMPARE(after.front(), before.front());
        QCOMPARE(after.back(), before.back());
        s->undoStack()->undo();
        QCOMPARE(docRoute(), before);
        // Ctrl/Cmd+click without dragging only toggles selection.
        const bool sel = item()->isSelected();
        QTest::mouseClick(v->viewport(), Qt::LeftButton, Qt::ControlModifier, v->mapFromScene(mid));
        QCOMPARE(item()->isSelected(), !sel);
        QCOMPARE(docRoute(), before);
    }

    void dragEndOntoAnotherPinReconnects()
    {
        // Select the wire, drag its target end (flop238:D) to flop238:CLK.
        v->clearSelection();
        Point a = *pinPosition(s->document(), PartLibrary::builtin(), s->document().wires[size_t(idx)].from);
        v->centerOn(QPointF(a.x, a.y));
        QTest::mouseClick(v->viewport(), Qt::LeftButton, {}, v->mapFromScene(QPointF(a.x + 12, a.y)));
        QVERIFY(item()->isSelected());
        const std::vector<Point> before = docRoute();
        const Point d = before.back();
        const Point clk = *pinPosition(s->document(), PartLibrary::builtin(), *PinRef::parse("flop238:CLK"));
        v->centerOn(QPointF(d.x, d.y));
        const QPointF from(d.x, d.y), to(clk.x, clk.y);
        const std::size_t wireCount = s->document().wires.size();
        QTest::mousePress(v->viewport(), Qt::LeftButton, {}, v->mapFromScene(from));
        for (int i = 1; i <= 5; ++i)
            QTest::mouseMove(v->viewport(), v->mapFromScene(from + (to - from) * i / 5.0));
        QTest::mouseRelease(v->viewport(), Qt::LeftButton, {}, v->mapFromScene(to));
        QCOMPARE(s->document().wires.size(), wireCount); // reconnected, not a new wire
        const Wire& w = s->document().wires[size_t(idx)];
        QCOMPARE(w.to.str(), std::string("flop238:CLK"));
        QCOMPARE(w.from.str(), std::string("ttin:IN1"));
        const std::vector<Point> after = docRoute();
        QVERIFY(std::fabs(after.back().x - round2(clk.x)) < 0.02 && std::fabs(after.back().y - round2(clk.y)) < 0.02);
        QCOMPARE(after.front(), before.front());
        s->undoStack()->undo();
        QCOMPARE(s->document().wires[size_t(idx)].to.str(), std::string("flop238:D"));
        QCOMPARE(docRoute(), before);

        // Dropping on empty canvas changes nothing.
        QTest::mouseClick(v->viewport(), Qt::LeftButton, {}, v->mapFromScene(QPointF(a.x + 12, a.y)));
        const int steps = s->undoStack()->index();
        QTest::mousePress(v->viewport(), Qt::LeftButton, {}, v->mapFromScene(from));
        QTest::mouseMove(v->viewport(), v->mapFromScene(from + QPointF(-60, 70)));
        QTest::mouseRelease(v->viewport(), Qt::LeftButton, {}, v->mapFromScene(from + QPointF(-60, 70)));
        QCOMPARE(s->undoStack()->index(), steps);
        QCOMPARE(docRoute(), before);
    }

    void clickOnEndHandleStartsANewWire()
    {
        // Select the wire, then click (no drag) its target end at flop238:D:
        // the wire is deselected and a new wire starts from flop238:D.
        v->clearSelection();
        Point a = *pinPosition(s->document(), PartLibrary::builtin(), s->document().wires[size_t(idx)].from);
        v->centerOn(QPointF(a.x, a.y));
        QTest::mouseClick(v->viewport(), Qt::LeftButton, {}, v->mapFromScene(QPointF(a.x + 12, a.y)));
        QVERIFY(item()->isSelected());
        const std::vector<Point> before = docRoute();
        const Point d = before.back();
        v->centerOn(QPointF(d.x, d.y));
        const std::size_t wireCount = s->document().wires.size();
        const int steps = s->undoStack()->index();
        QTest::mousePress(v->viewport(), Qt::LeftButton, {}, v->mapFromScene(QPointF(d.x, d.y)));
        QTest::mouseMove(v->viewport(), v->mapFromScene(QPointF(d.x + 1, d.y + 1))); // a jitter, not a drag
        QTest::mouseRelease(v->viewport(), Qt::LeftButton, {}, v->mapFromScene(QPointF(d.x + 1, d.y + 1)));
        QVERIFY(!item()->isSelected());
        QVERIFY(v->drawingWire());
        QCOMPARE(s->undoStack()->index(), steps); // nothing moved
        QCOMPARE(docRoute(), before);
        // Finish on flop238:CLK: a second wire from flop238:D.
        const Point clk = *pinPosition(s->document(), PartLibrary::builtin(), *PinRef::parse("flop238:CLK"));
        QTest::mouseMove(v->viewport(), v->mapFromScene(QPointF(clk.x, clk.y)));
        QTest::mouseClick(v->viewport(), Qt::LeftButton, {}, v->mapFromScene(QPointF(clk.x, clk.y)));
        QVERIFY(!v->drawingWire());
        QCOMPARE(s->document().wires.size(), wireCount + 1);
        const Wire& nw = s->document().wires.back();
        QCOMPARE(nw.from.str(), std::string("flop238:D"));
        QCOMPARE(nw.to.str(), std::string("flop238:CLK"));
        QCOMPARE(s->document().wires[size_t(idx)].to.str(), std::string("flop238:D")); // the first wire is untouched
        s->undoStack()->undo();
        QCOMPARE(s->document().wires.size(), wireCount);
    }

    void handlesStayOnScreenWhenZoomed()
    {
        v->clearSelection();
        const std::vector<Point> r = docRoute();
        // Longest segment and its midpoint.
        std::size_t seg = 0;
        double best = 0;
        for (std::size_t i = 0; i + 1 < r.size(); ++i) {
            const double l = std::fabs(r[i + 1].x - r[i].x) + std::fabs(r[i + 1].y - r[i].y);
            if (l > best) {
                best = l;
                seg = i;
            }
        }
        QVERIFY(best > 1000);
        // Zoom in on a point a quarter of the way along that segment, so the
        // segment crosses the view but its midpoint is far off-screen.
        const QPointF q(r[seg].x + (r[seg + 1].x - r[seg].x) / 4, r[seg].y + (r[seg + 1].y - r[seg].y) / 4);
        v->resetTransform();
        v->scale(3, 3);
        v->centerOn(q);
        QTest::mouseClick(v->viewport(), Qt::LeftButton, {}, v->mapFromScene(q));
        QVERIFY(item()->isSelected());
        const QPointF mid((r[seg].x + r[seg + 1].x) / 2, (r[seg].y + r[seg + 1].y) / 2);
        QVERIFY(!v->viewport()->rect().contains(v->mapFromScene(mid)));
        SegmentHandle* h = nullptr;
        for (SegmentHandle* x : handles())
            if (x->segment() == seg)
                h = x;
        QVERIFY(h);
        const QRect inner = v->viewport()->rect().adjusted(SchematicView::kHandleMargin - 1, SchematicView::kHandleMargin - 1,
                                                           -SchematicView::kHandleMargin + 1, -SchematicView::kHandleMargin + 1);
        QVERIFY2(inner.contains(v->mapFromScene(h->scenePos())), "handle of the long segment is on screen");
        // It follows scrolling.
        v->centerOn(q + QPointF((r[seg + 1].x - r[seg].x) / 8, (r[seg + 1].y - r[seg].y) / 8));
        QApplication::processEvents();
        QVERIFY(inner.contains(v->mapFromScene(h->scenePos())));
        // Dragging it still moves that segment.
        const QPointF hp = h->scenePos();
        const bool horiz = h->horizontal();
        const QPointF target = hp + (horiz ? QPointF(0, 9.6) : QPointF(9.6, 0));
        QTest::mousePress(v->viewport(), Qt::LeftButton, {}, v->mapFromScene(hp));
        for (int i = 1; i <= 4; ++i)
            QTest::mouseMove(v->viewport(), v->mapFromScene(hp + (target - hp) * i / 4.0));
        QTest::mouseRelease(v->viewport(), Qt::LeftButton, {}, v->mapFromScene(target));
        const std::vector<Point> after = docRoute();
        QVERIFY(after != r);
        s->undoStack()->undo();
        QCOMPARE(docRoute(), r);
        v->resetTransform();
    }

    void savedFileRoundTrips()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("edited.json");
        s->save(path);
        LoadResult r = loadWokwiFile(path.toStdString());
        const Wire& w = r.doc.wires[size_t(idx)];
        QCOMPARE(formatWirePath(w.path), formatWirePath(s->document().wires[size_t(idx)].path));
        const QString shot = qEnvironmentVariable("CHIPLY_WIRE_SHOT");
        if (!shot.isEmpty())
            v->grab().save(shot);
    }
};

QTEST_MAIN(WireEditTest)
#include "test_gui_wire_edit.moc"
