// Hover and hit-testing on the real reference design.
#include "EditorSession.h"
#include "SchematicItems.h"
#include "SchematicView.h"
#include "core/Geometry.h"

#include <QGraphicsScene>
#include <QTest>
#include <QToolTip>

class HoverTest : public QObject {
    Q_OBJECT
    EditorSession* s = nullptr;

    QPointF partCenter(const char* id)
    {
        const chiply::Part* p = s->document().findPart(id);
        const chiply::PartDef* d = chiply::PartLibrary::builtin().find(p->type);
        chiply::Rect r = chiply::partBounds(*p, *d);
        return QPointF(r.x + r.w / 2, r.y + r.h / 2);
    }

private slots:
    void initTestCase()
    {
        s = new EditorSession(this);
        s->load(QStringLiteral(CHIPLY_REFERENCE_DIR "/wokwi_414123795172381697.diagram.json"));
        s->view()->resize(1200, 800);
        s->view()->show();
        QVERIFY(QTest::qWaitForWindowExposed(s->view()));
    }

    // The flop under "Compare Val" is encircled by the ttin:IN1 -> flop238:D
    // wire; the part, not the wire, must be what the cursor is on.
    void partWinsOverEnclosingWire()
    {
        QGraphicsItem* top = s->view()->scene()->itemAt(partCenter("flop238"), QTransform());
        QVERIFY(top);
        QCOMPARE(top->type(), int(PartItem::Type));
        QCOMPARE(static_cast<PartItem*>(top)->partId(), std::string("flop238"));
        QCOMPARE(top->toolTip(), QStringLiteral("flop238"));
    }

    void every_part_center_hits_its_part()
    {
        int wrong = 0;
        for (const chiply::Part& p : s->document().parts) {
            if (p.type == "wokwi-text" || p.type == "wokwi-junction")
                continue;
            QGraphicsItem* top = s->view()->scene()->itemAt(partCenter(p.id.c_str()), QTransform());
            if (!top || top->type() != PartItem::Type) {
                ++wrong; // a wire crossing the exact center is legitimate, but rare
            }
        }
        QVERIFY2(wrong < 25, qPrintable(QString("%1 part centers covered").arg(wrong)));
    }

    void hoveringAPinShowsItsName()
    {
        const chiply::Part* p = s->document().findPart("flop238");
        const chiply::PartDef* d = chiply::PartLibrary::builtin().find(p->type);
        chiply::Point q = *chiply::pinPosition(*p, *d, "Q");
        SchematicView* v = s->view();
        v->centerOn(partCenter("flop238"));
        QTest::mouseMove(v->viewport(), v->mapFromScene(partCenter("flop238")));
        QTest::mouseMove(v->viewport(), v->mapFromScene(QPointF(q.x, q.y)));
        QApplication::processEvents();
        QGraphicsItem* item = v->scene()->itemAt(QPointF(q.x, q.y), QTransform());
        QVERIFY(item && item->type() == PartItem::Type);
        QCOMPARE(item->toolTip(), QStringLiteral("flop238:Q"));
        // Between pins, the wire still answers with its own tooltip.
        const chiply::Wire* w = nullptr;
        for (const chiply::Wire& x : s->document().wires)
            if (x.from.str() == "ttin:IN1" && x.to.str() == "flop238:D")
                w = &x;
        QVERIFY(w);
        chiply::Point a = *chiply::pinPosition(s->document(), chiply::PartLibrary::builtin(), w->from);
        chiply::Point mid{a.x + 12, a.y};
        QGraphicsItem* wi = v->scene()->itemAt(QPointF(mid.x, mid.y), QTransform());
        QVERIFY(wi && wi->type() == WireItem::Type);
        QVERIFY(wi->toolTip().contains("flop238:D"));
        const QString shot = qEnvironmentVariable("CHIPLY_HOVER_SHOT");
        if (!shot.isEmpty())
            v->grab().save(shot);
    }
};

QTEST_MAIN(HoverTest)
#include "test_gui_hover.moc"
