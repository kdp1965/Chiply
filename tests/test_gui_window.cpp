// Main window layout: selecting must not resize docks or shift the canvas.
#include "EditorSession.h"
#include "Inspector.h"
#include "MainWindow.h"
#include "PartPalette.h"
#include "SchematicItems.h"
#include "SchematicView.h"
#include <QGraphicsScene>
#include "core/Geometry.h"

#include "Theme.h"

#include <QApplication>
#include <QComboBox>
#include <QDockWidget>
#include <QLabel>
#include <QListWidget>
#include <QToolButton>
#include <QSettings>
#include <QTabWidget>
#include <QTest>

class WindowTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase()
    {
        // Keep the tests away from the user's real preferences.
        QCoreApplication::setOrganizationName("ChiplyTest");
        QCoreApplication::setApplicationName("ChiplyWindowTest");
        QSettings().clear();
    }

    void layoutIsRestored()
    {
        {
            MainWindow w;
            w.show();
            QVERIFY(QTest::qWaitForWindowExposed(&w));
            auto* insp = w.findChild<QDockWidget*>("inspector");
            QVERIFY(insp);
            QCOMPARE(w.dockWidgetArea(insp), Qt::RightDockWidgetArea);
            w.addDockWidget(Qt::LeftDockWidgetArea, insp);
            w.resize(700, 590); // fits the 800x800 offscreen test screen
            QApplication::processEvents();
            QVERIFY(w.close());
        }
        MainWindow w2;
        w2.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w2));
        auto* insp2 = w2.findChild<QDockWidget*>("inspector");
        QCOMPARE(w2.dockWidgetArea(insp2), Qt::LeftDockWidgetArea);
        QCOMPARE(w2.size(), QSize(700, 590));
        w2.close();
        QSettings().clear();
    }

    void inspectorIsResizableAndSticky()
    {
        int chosen = 0;
        {
            MainWindow w;
            w.resize(780, 600);
            w.show();
            QVERIFY(QTest::qWaitForWindowExposed(&w));
            QVERIFY(w.openFile(QStringLiteral(CHIPLY_REFERENCE_DIR "/wokwi_414123795172381697.diagram.json")));
            QApplication::processEvents();
            auto* dock = w.findChild<QDockWidget*>("inspector");
            w.resizeDocks({dock}, {240}, Qt::Horizontal); // what dragging the splitter does
            QApplication::processEvents();
            chosen = dock->width();
            QVERIFY2(chosen < 300, qPrintable(QString::number(chosen)));
            // Selecting parts with wide forms must not change it.
            auto* v = qobject_cast<SchematicView*>(w.findChild<QTabWidget*>()->currentWidget());
            for (QGraphicsItem* it : v->scene()->items()) {
                if (itemPartId(it) == "ttin" || itemPartId(it) == "clock1") {
                    v->selectOnly(it);
                    QApplication::processEvents();
                    QCOMPARE(dock->width(), chosen);
                }
            }
            QVERIFY(w.close());
        }
        MainWindow w2;
        w2.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w2));
        QApplication::processEvents();
        QCOMPARE(w2.findChild<QDockWidget*>("inspector")->width(), chosen);
        w2.close();
        QSettings().clear();
    }

    void themeButtonTogglesAndPersists()
    {
        Theme::instance().setMode(Theme::Mode::Light);
        MainWindow w;
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        auto* b = w.findChild<QToolButton*>("themeButton");
        QVERIFY(b);
        QCOMPARE(b->property("dark").toBool(), false); // sun
        b->click();
        QVERIFY(Theme::instance().isDark());
        QCOMPARE(b->property("dark").toBool(), true); // moon
        QCOMPARE(QSettings().value("appearance/theme").toString(), QStringLiteral("dark"));
        b->click();
        QVERIFY(!Theme::instance().isDark());
        QCOMPARE(QSettings().value("appearance/theme").toString(), QStringLiteral("light"));
        w.close();
    }

    void newFromTemplateIsAnUntitledCopy()
    {
        MainWindow w;
        w.show();
        auto* tabs = w.findChild<QTabWidget*>();
        QCOMPARE(tabs->count(), 1); // the blank Untitled tab
        w.findChild<QAction*>("newFromTemplateAction")->trigger();
        QCOMPARE(tabs->count(), 1); // replaced, not added beside it
        auto* s = qobject_cast<EditorSession*>(tabs->currentWidget()->property("session").value<QObject*>());
        QVERIFY(s);
        QVERIFY(s->filePath().isEmpty());
        QVERIFY(!s->isModified());
        QCOMPARE(s->displayName(), QStringLiteral("Untitled"));
        QVERIFY(s->document().findPart("ttin") && s->document().findPart("ttout") && s->document().findPart("sw1"));
        for (int b = 0; b < 8; ++b) // template.json adds the bidirectional I/O blocks
            QVERIFY(s->document().findPart("ttio" + std::to_string(b)));
        QCOMPARE(s->document().parts.size(), std::size_t(34));
        QCOMPARE(s->document().author(), std::string());
        QVERIFY(s->violations().empty()); // the template is DRC clean
        // A second one opens in its own tab.
        w.findChild<QAction*>("newFromTemplateAction")->trigger();
        QCOMPARE(tabs->count(), 2);
    }

    void extensionsModeSwitch()
    {
        QSettings().remove("extensions/enabled");
        MainWindow w;
        w.show();
        auto paletteTypes = [] {
            PartPalette pal;
            QStringList types;
            auto* list = pal.findChild<QListWidget*>();
            for (int i = 0; i < list->count(); ++i)
                types << list->item(i)->data(Qt::UserRole).toString();
            return types;
        };
        QVERIFY(!EditorSession::extensionsEnabled()); // Wokwi mode by default
        QVERIFY(w.findChild<QLabel*>("modeStatus")->text().startsWith("WOKWI MODE"));
        QVERIFY(paletteTypes().contains("wokwi-gate-and-2"));
        QVERIFY(!paletteTypes().contains("chiply-a21oi"));

        // A design with an extension part: DRC flags it in Wokwi mode only.
        auto* s = qobject_cast<EditorSession*>(w.findChild<QTabWidget*>()->currentWidget()->property("session").value<QObject*>());
        chiply::Document d = s->document();
        chiply::Part p;
        p.type = "chiply-a21oi";
        p.id = "aoi1";
        d.parts.push_back(p);
        s->replaceDocument(d, {});
        s->runDrc(false);
        auto hasExt = [&] {
            for (const auto& v : s->violations())
                if (v.check == "extension-part")
                    return true;
            return false;
        };
        QVERIFY(hasExt());
        QVERIFY(s->usesExtensionParts());

        w.findChild<QAction*>("extensionsAction")->trigger();
        QVERIFY(EditorSession::extensionsEnabled());
        QVERIFY(w.findChild<QLabel*>("modeStatus")->text().startsWith("EXTENDED MODE"));
        QVERIFY(paletteTypes().contains("chiply-a21oi"));
        QVERIFY(paletteTypes().contains("chiply-mux-4"));
        QVERIFY(!hasExt());

        w.findChild<QAction*>("extensionsAction")->trigger();
        QVERIFY(!EditorSession::extensionsEnabled());
        QVERIFY(hasExt());
        QSettings().remove("extensions/enabled");
    }

    void customBlocksFromTheDesignFolder()
    {
        MainWindow w;
        w.show();
        QVERIFY(w.openFile(QStringLiteral(CHIPLY_TEST_DATA_DIR "/blocks_demo/design.json")));
        auto* s = qobject_cast<EditorSession*>(w.findChild<QTabWidget*>()->currentWidget()->property("session").value<QObject*>());
        QVERIFY(s->usesBlocks());
        PartItem* add = nullptr;
        for (QGraphicsItem* it : s->view()->scene()->items())
            if (it->type() == PartItem::Type && static_cast<PartItem*>(it)->partId() == "add1")
                add = static_cast<PartItem*>(it);
        QVERIFY(add && add->def() && add->def()->block);
        QCOMPARE(add->def()->block->module, std::string("adder4"));
        QVERIFY(s->violations().size() == 2); // Wokwi mode: two extension parts
        QSettings().setValue("extensions/enabled", true);
        {
            PartPalette pal;
            auto* list = pal.findChild<QListWidget*>();
            QStringList types;
            for (int i = 0; i < list->count(); ++i)
                types << list->item(i)->data(Qt::UserRole).toString();
            QVERIFY(types.contains("chiply-block-adder4"));
            QVERIFY(types.contains("chiply-block-counter4"));
        }
        QSettings().remove("extensions/enabled");
        w.findChild<QAction*>("reloadBlocksAction")->trigger(); // rebuilds every tab
        QVERIFY(s->usesBlocks());
    }

    void memorySizeFromTheInspector()
    {
        MainWindow w;
        w.show();
        auto* s = qobject_cast<EditorSession*>(w.findChild<QTabWidget*>()->currentWidget()->property("session").value<QObject*>());
        chiply::Document d = s->document();
        chiply::Part ram;
        ram.type = "chiply-ram-16x8";
        ram.id = "ram1";
        d.parts.push_back(ram);
        s->replaceDocument(d, {"ram1"});
        QApplication::processEvents();
        auto* depth = w.findChild<QComboBox*>("memoryDepth");
        auto* width = w.findChild<QComboBox*>("memoryWidth");
        QVERIFY(depth && width);
        QCOMPARE(depth->currentText(), QStringLiteral("16"));
        QCOMPARE(width->currentText(), QStringLiteral("8"));
        depth->setCurrentText(QStringLiteral("64"));
        emit depth->activated(depth->currentIndex());
        QCOMPARE(s->document().findPart("ram1")->type, std::string("chiply-ram-64x8"));
        QApplication::processEvents();
        width = w.findChild<QComboBox*>("memoryWidth"); // the Inspector was rebuilt
        width->setCurrentText(QStringLiteral("4"));
        emit width->activated(width->currentIndex());
        QCOMPARE(s->document().findPart("ram1")->type, std::string("chiply-ram-64x4"));
        s->undoStack()->undo();
        QCOMPARE(s->document().findPart("ram1")->type, std::string("chiply-ram-64x8"));
        s->undoStack()->undo();
        QCOMPARE(s->document().findPart("ram1")->type, std::string("chiply-ram-16x8"));
    }

    void cursorPositionInTheStatusBar()
    {
        MainWindow w;
        w.resize(1200, 800);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        auto* v = qobject_cast<SchematicView*>(w.findChild<QTabWidget*>()->currentWidget());
        v->resetTransform();
        v->centerOn(QPointF(96, 48));
        const QPoint at = v->mapFromScene(QPointF(96, 48));
        QTest::mouseMove(v->viewport(), at);
        const QString t = w.findChild<QLabel*>("cursorPos")->text();
        QVERIFY2(t.startsWith("x 96.") || t.startsWith("x 95.") || t.startsWith("x 97."), qPrintable(t));
        QVERIFY2(t.contains("y 48.") || t.contains("y 47.") || t.contains("y 49."), qPrintable(t));
    }

    void selectionDoesNotShiftCanvas()
    {
        MainWindow w;
        w.resize(1500, 950);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        QVERIFY(w.openFile(QStringLiteral(CHIPLY_REFERENCE_DIR "/wokwi_414123795172381697.diagram.json")));
        QApplication::processEvents(); // let the replaced Untitled tab go away
        auto* v = qobject_cast<SchematicView*>(w.findChild<QTabWidget*>()->currentWidget());
        auto* s = qobject_cast<EditorSession*>(v->property("session").value<QObject*>());
        QVERIFY(v && s);
        QApplication::processEvents();
        const QRect before = v->geometry();
        const QRect inspBefore = w.findChild<Inspector*>()->geometry();

        const std::vector<std::string> ids{"flop238", "clock1", "ttin", "sevseg1", "text5"};
        for (const std::string& id : ids) {
            QGraphicsItem* item = nullptr;
            for (QGraphicsItem* it : v->scene()->items())
                if (itemPartId(it) == id)
                    item = it;
            QVERIFY2(item, id.c_str());
            v->centerOn(item);
            v->selectOnly(item);
            QApplication::processEvents();
            QCOMPARE(s->selectedPartIds(), std::vector<std::string>{id});
            QCOMPARE(v->geometry(), before);
            QCOMPARE(w.findChild<Inspector*>()->geometry().width(), inspBefore.width());
        }
        v->clearSelection();
        QApplication::processEvents();
        QCOMPARE(v->geometry(), before);
    }
};

QTEST_MAIN(WindowTest)
#include "test_gui_window.moc"
