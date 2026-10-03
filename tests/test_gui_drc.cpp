// Design rule checks in the GUI (PLAN.md 5.2): live incremental updates,
// Full DRC, the Violations pane, snapping, F8, check switches, waivers and
// the sidecar file.
#include "EditorSession.h"
#include "MainWindow.h"
#include "SchematicView.h"
#include "ViolationsPane.h"

#include <QCheckBox>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTreeWidget>

using chiply::drc::Severity;

class DrcGuiTest : public QObject {
    Q_OBJECT
    MainWindow* w = nullptr;
    EditorSession* s = nullptr;
    SchematicView* v = nullptr;
    ViolationsPane* pane = nullptr;
    QTemporaryDir tmp;
    QString path;

    int wireIndex(const char* a, const char* b) const
    {
        const auto& ws = s->document().wires;
        for (std::size_t i = 0; i < ws.size(); ++i)
            if ((ws[i].from.str() == a && ws[i].to.str() == b) || (ws[i].from.str() == b && ws[i].to.str() == a))
                return int(i);
        return -1;
    }
    bool has(const std::string& key) const
    {
        for (const auto& x : s->violations())
            if (x.key == key)
                return true;
        return false;
    }

private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName("ChiplyTest");
        QCoreApplication::setApplicationName("ChiplyDrcGuiTest");
        QSettings().clear();
        // A writable copy of the reference design (fixtures are read-only).
        path = tmp.filePath("design.json");
        QVERIFY(QFile::copy(QStringLiteral(CHIPLY_REFERENCE_DIR "/wokwi_414123795172381697.diagram.json"), path));
        QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        w = new MainWindow;
        w->resize(1400, 900);
        w->show();
        QVERIFY(QTest::qWaitForWindowExposed(w));
        QVERIFY(w->openFile(path));
        v = qobject_cast<SchematicView*>(w->findChild<QTabWidget*>()->currentWidget());
        s = qobject_cast<EditorSession*>(v->property("session").value<QObject*>());
        pane = w->findChild<ViolationsPane*>();
        QVERIFY(s && pane);
    }

    void loadRunsAFullCheck()
    {
        QVERIFY(s->drcStats().full);
        QCOMPARE(s->unwaivedCount(Severity::Error), 10);
        QCOMPARE(s->unwaivedCount(Severity::Warning), 3);
        QVERIFY(pane->itemFor("combinational-loop:mux1"));
        QVERIFY(w->findChild<QLabel*>("drcStatus")->text().contains("10 errors"));
        QVERIFY(w->findChild<QDockWidget*>("violations")->isVisible());
    }

    void liveCheckAfterAnEdit()
    {
        // Deleting mux1's self-loop wire clears that loop, checking only
        // the parts around it.
        const int i = wireIndex("mux1:OUT", "mux1:A");
        QVERIFY(i >= 0);
        emit v->deleteWireRequested(i);
        QTRY_VERIFY_WITH_TIMEOUT(!has("combinational-loop:mux1"), 2000);
        QVERIFY(!s->drcStats().full);
        QVERIFY(s->drcStats().partsChecked < 50);
        QVERIFY(has("unconnected-input:mux1:A")); // now nothing drives A
        QVERIFY(!pane->itemFor("combinational-loop:mux1"));
        QVERIFY(pane->itemFor("unconnected-input:mux1:A"));
        // Undo brings it back.
        s->undoStack()->undo();
        QTRY_VERIFY_WITH_TIMEOUT(has("combinational-loop:mux1"), 2000);
        QVERIFY(!has("unconnected-input:mux1:A"));
    }

    void fullDrcButton()
    {
        w->findChild<QPushButton*>("fullDrcButton")->click();
        QVERIFY(s->drcStats().full);
        QCOMPARE(s->unwaivedCount(Severity::Error), 10);
    }

    void clickAndF8SnapToTheViolation()
    {
        pane->tree()->setCurrentItem(nullptr);
        QTest::keyClick(w, Qt::Key_F8);
        QTreeWidgetItem* cur = pane->tree()->currentItem();
        QVERIFY(cur);
        QCOMPARE(cur->data(0, Qt::UserRole + 1).toString(), QStringLiteral("combinational-loop:mux1"));
        QCOMPARE(s->selectedPartIds(), std::vector<std::string>{"mux1"});
        QVERIFY(v->hasHighlight());
        QVERIFY(v->zoom() >= 1.0 && v->zoom() <= 4.0);
        QVERIFY(v->visibleSceneRect().intersects(v->scene()->selectedItems().front()->sceneBoundingRect()));
        QTest::keyClick(w, Qt::Key_F8);
        QCOMPARE(s->selectedPartIds(), std::vector<std::string>{"mux2"});
        QTest::keyClick(w, Qt::Key_F8, Qt::ShiftModifier);
        QCOMPARE(s->selectedPartIds(), std::vector<std::string>{"mux1"});
        // Changing the selection by hand drops the highlight.
        v->clearSelection();
        QVERIFY(!v->hasHighlight());
    }

    void turningAChecksOffAndWaivingAreSavedBesideTheDesign()
    {
        QVERIFY(!s->isModified());
        s->setCheckEnabled("stacked-parts", false);
        QVERIFY(!has("stacked-parts:ttio5,ttio8"));
        QVERIFY(s->isModified()); // saved with the design
        s->waive("combinational-loop:mux9", "known latch, fixed in the copy");
        QCOMPARE(s->unwaivedCount(Severity::Error), 9);
        QTreeWidgetItem* row = pane->itemFor("combinational-loop:mux9");
        QVERIFY(row && row->parent());
        QVERIFY(row->parent()->text(0).startsWith("Waived"));
        QVERIFY(row->text(1).contains("known latch"));

        s->save();
        QVERIFY(!s->isModified());
        const QString side = EditorSession::sidecarPath(path);
        QCOMPARE(side, tmp.filePath("design.chiply.json"));
        QFile f(side);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QByteArray text = f.readAll();
        QVERIFY(text.contains("\"stacked-parts\": false"));
        QVERIFY(text.contains("combinational-loop:mux9"));
        f.close();

        // Reload: both come back.
        s->load(path);
        QVERIFY(!s->drc().enabled("stacked-parts"));
        QVERIFY(s->isWaived("combinational-loop:mux9"));
        QCOMPARE(s->unwaivedCount(Severity::Error), 9);

        // Back to defaults: the sidecar keeps an empty record.
        s->setCheckEnabled("stacked-parts", true);
        s->unwaive("combinational-loop:mux9");
        QVERIFY(has("stacked-parts:ttio5,ttio8"));
        s->save();
        s->load(path);
        QVERIFY(s->drc().enabled("stacked-parts"));
        QVERIFY(!s->isWaived("combinational-loop:mux9"));
    }

    void liveOffWaitsForFullDrc()
    {
        auto* live = w->findChild<QCheckBox*>("liveDrcBox");
        live->setChecked(false);
        QVERIFY(!EditorSession::drcLive());
        const int i = wireIndex("mux2:OUT", "mux2:A");
        QVERIFY(i >= 0);
        emit v->deleteWireRequested(i);
        QTest::qWait(250);
        QVERIFY(has("combinational-loop:mux2")); // not re-checked yet
        live->setChecked(true);                  // catches up
        QVERIFY(!has("combinational-loop:mux2"));
        QSettings().clear();
    }
};

QTEST_MAIN(DrcGuiTest)
#include "test_gui_drc.moc"
