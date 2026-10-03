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
