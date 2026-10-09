#include "MainWindow.h"
#include "Text.h"

#include "EditorSession.h"
#include "PartPalette.h"
#include "Inspector.h"
#include "MiniToolbar.h"
#include "SimRunner.h"
#include "SchematicItems.h"
#include "SchematicView.h"
#include "Theme.h"
#include "WaveformView.h"
#include "ViolationsPane.h"
#include "core/Blocks.h"
#include "core/Sheets.h"
#include "core/Verilog.h"
#include "vl/VerilatorChip.h"
#include "core/WokwiJson.h"

#include <QAction>
#include <QPointer>
#include <QFontMetrics>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QDialog>
#include <QUrl>
#include <QDesktopServices>
#include <atomic>
#include <mutex>
#ifndef __EMSCRIPTEN__
#include <thread>
#endif
#include <QProgressDialog>
#include <QLineEdit>
#include <QPushButton>
#include <QInputDialog>
#include <QScrollArea>
#include <QDir>
#ifndef Q_OS_WASM
#include <QProcess>
#endif
#include <QStandardPaths>
#include <QCheckBox>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QKeySequence>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QStatusBar>
#include <QTabWidget>
#include <QToolBar>
#include <QDockWidget>
#include <QUndoView>
#include <QClipboard>
#include <QCursor>
#include <QTimer>
#include <QPainter>
#include <QPainterPath>
#include <QToolButton>
#include <cmath>
#include <QSettings>
#include <QGraphicsScene>
#include <QUndoGroup>

namespace {
// The folder of the last Open / Save (a preference), so dialogs start there.
QString lastFolder()
{
    const QString d = QSettings().value("files/lastFolder").toString();
    return !d.isEmpty() && QFileInfo(d).isDir() ? d : QDir::homePath();
}
void rememberFolder(const QString& filePath)
{
    QSettings().setValue("files/lastFolder", QFileInfo(filePath).absolutePath());
}
} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    m_undoGroup = new QUndoGroup(this);
    m_tabs = new QTabWidget(this);
    m_tabs->setTabsClosable(true);
    m_tabs->setMovable(true);
    m_tabs->setDocumentMode(true);
    setCentralWidget(m_tabs);
    connect(m_tabs, &QTabWidget::tabCloseRequested, this, [this](int i) { closeTab(i); });
    connect(m_tabs, &QTabWidget::currentChanged, this, [this] {
        if (auto* s = current())
            m_undoGroup->setActiveStack(s->undoStack());
        if (m_inspector)
            m_inspector->setSession(current());
        if (m_waveforms)
            m_waveforms->setSession(current());
        if (m_violations)
            m_violations->setSession(current());
        updateDrcStatus();
        updateSimControls();
        updateStatus();
    });

    m_zoomLabel = new QLabel(this);
    QFont sf = m_zoomLabel->font();
    sf.setPointSize(14); // readable status line
    m_zoomLabel->setFont(sf);
    m_modeLabel = new QLabel(this);
    m_modeLabel->setObjectName("modeStatus");
    m_modeLabel->setFont(sf);
    statusBar()->addPermanentWidget(m_modeLabel);
    m_posLabel = new QLabel(this);
    m_posLabel->setObjectName("cursorPos");
    m_posLabel->setFont(sf);
    m_posLabel->setMinimumWidth(QFontMetrics(sf).horizontalAdvance(QStringLiteral("x -0000.0  y -0000.0   |")));
    m_posLabel->setToolTip(tr("Cursor position in diagram units (px, 9.6 per 0.1 inch grid step)"));
    statusBar()->addPermanentWidget(m_posLabel);
    m_drcLabel = new QLabel(this);
    m_drcLabel->setObjectName("drcStatus");
    m_drcLabel->setFont(sf);
    statusBar()->addPermanentWidget(m_drcLabel);
    statusBar()->addPermanentWidget(m_zoomLabel);

    chiply::scanBlocks({chiply::defaultUserBlocksDir()}); // the user's block library
    chiply::scanSheets({chiply::defaultUserSheetsDir()}); // and sheet library
    buildMenus();
    resize(1400, 900);
    restoreLayout();
    // Save the layout shortly after any change too, not only on quit.
    m_saveLayout.setSingleShot(true);
    m_saveLayout.setInterval(800);
    connect(&m_saveLayout, &QTimer::timeout, this, &MainWindow::saveLayout);
    for (QDockWidget* d : findChildren<QDockWidget*>()) {
        connect(d, &QDockWidget::dockLocationChanged, &m_saveLayout, qOverload<>(&QTimer::start));
        connect(d, &QDockWidget::topLevelChanged, &m_saveLayout, qOverload<>(&QTimer::start));
        connect(d, &QDockWidget::visibilityChanged, &m_saveLayout, qOverload<>(&QTimer::start));
    }
    newFile();
    m_constructed = true;
}

void MainWindow::buildMenus()
{
    QMenu* file = menuBar()->addMenu(tr("&File"));
    file->addAction(tr("&New"), QKeySequence::New, this, &MainWindow::newFile);
    file->addAction(tr("New from &Template"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_N), this, &MainWindow::newFromTemplate)
        ->setObjectName("newFromTemplateAction");
    file->addAction(tr("&Open..."), QKeySequence::Open, this, &MainWindow::openDialog);
    file->addSeparator();
    file->addAction(tr("&Save"), QKeySequence::Save, this, [this] {
        if (auto* s = current())
            saveSession(s, false);
    });
    file->addAction(tr("Save &As..."), QKeySequence::SaveAs, this, [this] {
        if (auto* s = current())
            saveSession(s, true);
    });
    file->addAction(tr("Save A&ll"), this, [this] {
        for (int i = 0; i < m_tabs->count(); ++i)
            if (sessionAt(i)->isModified() && !saveSession(sessionAt(i), false))
                return;
    });
    file->addSeparator();
    file->addAction(tr("Export &Verilog..."), this, &MainWindow::exportVerilog)->setObjectName("exportVerilogAction");
#ifndef Q_OS_WASM
    file->addAction(tr("Export &Tiny Tapeout Project..."), this, &MainWindow::exportTtProject)
        ->setObjectName("exportTtAction");
#endif
    file->addSeparator();
    file->addAction(tr("&Close Tab"), QKeySequence::Close, this, [this] { closeTab(m_tabs->currentIndex()); });
    file->addAction(tr("&Quit"), QKeySequence::Quit, this, &QWidget::close);

    QToolBar* tb = addToolBar(tr("Main"));
    tb->setObjectName("mainToolbar");
    tb->setMovable(false);
    tb->setToolButtonStyle(Qt::ToolButtonTextOnly);
    QFont tbf = tb->font();
    tbf.setPointSize(16);
    tb->setFont(tbf);
    // Program title C͓̽H͓̽I͓̽P͓̽L͓̽Y͓̽ drawn as a graphic (a small x above and below each
    // letter) so it looks the same in every font.
    m_titleLabel = new QLabel(tb);
    m_titleLabel->setObjectName("programTitle");
    m_titleLabel->setAccessibleName(QStringLiteral("Chiply"));
    m_titleLabel->setContentsMargins(8, 0, 14, 0);
    tb->addWidget(m_titleLabel);
    tb->addSeparator();
    QAction* addPart = tb->addAction(tr("+  Add Part"), this, &MainWindow::addPart);
    m_addPartAction = addPart;
    addPart->setToolTip(tr("Add a part (A)"));
    tb->addSeparator();
    tb->addAction(tr("Fit"), this, [this] { if (auto* s = current()) s->view()->fitContents(); });
    tb->addAction(tr("Zoom +"), this, [this] { if (auto* s = current()) s->view()->zoomIn(); });
    tb->addAction(tr("Zoom \u2212"), this, [this] { if (auto* s = current()) s->view()->zoomOut(); });
    // Simulation controls, right of the zoom buttons: Play/Pause, Step, Stop.
    tb->addSeparator();
    m_playAction = tb->addAction(simIcon(SimIcon::Play), tr("Play"), this, &MainWindow::playPause);
    m_playAction->setObjectName("playAction");
    m_playAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Return));
    m_stepAction = tb->addAction(simIcon(SimIcon::Step), tr("Step"), this, [this] {
        if (auto* s = current(); s && s->sim())
            s->sim()->step();
    });
    m_stepAction->setObjectName("stepAction");
    m_stopAction = tb->addAction(simIcon(SimIcon::Stop), tr("Stop"), this, [this] {
        if (auto* s = current())
            s->stopSimulation();
    });
    m_stopAction->setObjectName("stopAction");
    // LiveWire: colour wires by their simulated value (saved preference).
    auto* liveWire = new QCheckBox(tr("LiveWire"), tb);
    liveWire->setObjectName("liveWireBox");
    liveWire->setToolTip(tr("Show simulated values on the wires"));
    liveWire->setChecked(QSettings().value("sim/liveWire", true).toBool());
    WireItem::setLiveWires(liveWire->isChecked());
    connect(liveWire, &QCheckBox::toggled, this, [this](bool on) {
        QSettings().setValue("sim/liveWire", on);
        WireItem::setLiveWires(on);
        for (int i = 0; i < m_tabs->count(); ++i)
            for (QGraphicsItem* it : sessionAt(i)->view()->scene()->items())
                if (it->type() == WireItem::Type) {
                    static_cast<WireItem*>(it)->restyle();
                    it->update();
                }
    });
    for (QAction* a : {m_playAction, m_stepAction, m_stopAction}) {
        // Icon-only (the toolbar shows text for its other buttons); the name
        // is the tooltip.
        auto* b = qobject_cast<QToolButton*>(tb->widgetForAction(a));
        b->setObjectName(a->objectName() + "Button");
        b->setToolButtonStyle(Qt::ToolButtonIconOnly);
        b->setIconSize(QSize(30, 30));
    }
    tb->addWidget(liveWire);
    // Paste name format (PLAN.md 4.7): "r#_*" steps the number at # on paste.
    tb->addSeparator();
    auto* pasteLabel = new QLabel(tr(" Paste names "), tb);
    tb->addWidget(pasteLabel);
    auto* pasteFormat = new QLineEdit(EditorSession::pasteNameFormat(), tb);
    pasteFormat->setObjectName("pasteNameFormat");
    pasteFormat->setPlaceholderText(tr("e.g. r#_*"));
    pasteFormat->setClearButtonEnabled(true);
    pasteFormat->setFont(tbf); // as large as the rest of the toolbar
    pasteFormat->setFixedWidth(190);
    tb->addWidget(pasteFormat);
    const QString pasteTip = tr("Paste name format: # is the number to step, * is any text.\n"
                                "With r#_*, copying r1_b31 and r1_b31_n1 pastes r2_b31 and r2_b31_n1.\n"
                                "Empty: pasted names get the next free number or _1.");
    pasteFormat->setToolTip(pasteTip);
    pasteLabel->setToolTip(pasteTip);
    auto checkFormat = [pasteFormat, pasteTip](const QString& text) {
        const chiply::NameFormat f(text.trimmed().toStdString());
        pasteFormat->setStyleSheet(f.valid() ? QString() : QStringLiteral("color: #e53935;"));
        pasteFormat->setToolTip(f.valid() ? pasteTip : QString::fromStdString(f.error()) + QStringLiteral("\n\n") + pasteTip);
    };
    checkFormat(pasteFormat->text());
    connect(pasteFormat, &QLineEdit::textChanged, this, [checkFormat](const QString& t) {
        EditorSession::setPasteNameFormat(t.trimmed());
        checkFormat(t);
    });
    // Right end: light/dark switch (sun in light mode, moon in dark mode).
    auto* spacer = new QWidget(tb);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    tb->addWidget(spacer);
    m_themeButton = new QToolButton(tb);
    m_themeButton->setObjectName("themeButton");
    m_themeButton->setIconSize(QSize(28, 28));
    m_themeButton->setAutoRaise(true);
    connect(m_themeButton, &QToolButton::clicked, this, [] {
        Theme::instance().setMode(Theme::instance().isDark() ? Theme::Mode::Light : Theme::Mode::Dark);
    });
    tb->addWidget(m_themeButton);
    updateThemeButton();
    connect(&Theme::instance(), &Theme::changed, this, &MainWindow::updateThemeButton);

    QMenu* edit = menuBar()->addMenu(tr("&Edit"));
    m_editMenu = edit;
    QAction* undo = m_undoGroup->createUndoAction(this, tr("&Undo"));
    undo->setShortcut(QKeySequence::Undo);
    QAction* redo = m_undoGroup->createRedoAction(this, tr("&Redo"));
    redo->setShortcuts({QKeySequence::Redo, QKeySequence(Qt::CTRL | Qt::Key_Y)});
    edit->addAction(undo);
    edit->addAction(redo);
    edit->addSeparator();
    edit->addAction(tr("Cu&t"), QKeySequence::Cut, this, [this] {
        if (auto* s = current()) {
            const QString t = s->copySelection();
            if (!t.isEmpty()) {
                QApplication::clipboard()->setText(t);
                s->cutSelection();
            }
        }
    });
    edit->addAction(tr("&Copy"), QKeySequence::Copy, this, [this] {
        if (auto* s = current()) {
            const QString t = s->copySelection();
            if (!t.isEmpty()) {
                QApplication::clipboard()->setText(t);
                statusBar()->showMessage(tr("Copied %1").arg(countOf(int(s->selectedPartIds().size()), "part", "parts")), 3000);
            }
        }
    });
    edit->addAction(tr("&Paste"), QKeySequence::Paste, this, &MainWindow::paste);
    edit->addSeparator();
    edit->addAction(tr("Add &Part...  (A)"), this, &MainWindow::addPart);
    edit->addAction(tr("&Rotate  (R)"), this, [this] { if (auto* s = current()) s->rotateSelection(); });
    edit->addAction(tr("D&uplicate  (D)"), this, [this] { if (auto* s = current()) s->duplicateSelection(); });
    edit->addAction(tr("&Delete  (Del)"), this, [this] { if (auto* s = current()) s->deleteSelection(); });
    edit->addSeparator();
    edit->addAction(tr("Select &All"), QKeySequence::SelectAll, this, [this] {
        if (auto* s = current())
            s->view()->selectAll();
    });
    edit->addAction(tr("&Deselect  (Esc)"), this, [this] {
        if (auto* s = current())
            s->view()->clearSelection();
    });

    // Wokwi mode / Extended mode (PLAN.md 7.1).
    edit->addSeparator();
    m_extensionsAction = edit->addAction(tr("Chiply E&xtensions (extra cells, not Wokwi-loadable)"));
    m_extensionsAction->setObjectName("extensionsAction");
    m_extensionsAction->setCheckable(true);
    m_extensionsAction->setChecked(EditorSession::extensionsEnabled());
    m_extensionsAction->setToolTip(tr("Offer Chiply's own parts (3/4-input gates, MUX4, AOI/OAI cells...). "
                                      "Designs that use them no longer load in Wokwi."));
    connect(m_extensionsAction, &QAction::toggled, this, &MainWindow::setExtensions);
    // Custom blocks (PLAN.md 7.2): <design>/blocks and the user library.
    QAction* reloadBlocks = edit->addAction(tr("Reload &Blocks and Sheets"), this, [this] { reloadLibraries(true); });
    reloadBlocks->setObjectName("reloadBlocksAction");
    // Bus routing (PLAN.md 4.6).
    QAction* busRoute = edit->addAction(tr("B&us Route from Selected Parts"));
    busRoute->setObjectName("busRouteAction");
    busRoute->setCheckable(true);
    busRoute->setChecked(QSettings().value("edit/busRouting", true).toBool());
    busRoute->setToolTip(tr("With several parts selected, a click on a pin of one of them draws a wire from that pin on "
                            "every selected part, routed together one grid apart"));
    SchematicView::setBusRouting(busRoute->isChecked());
    connect(busRoute, &QAction::toggled, this, [](bool on) {
        QSettings().setValue("edit/busRouting", on);
        SchematicView::setBusRouting(on);
    });
    edit->addSeparator();
    QAction* replaceNames = edit->addAction(tr("Find and Replace in &Names..."), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_F),
                                            this, &MainWindow::replaceInNames);
    replaceNames->setObjectName("replaceNamesAction");
#ifndef Q_OS_WASM
    edit->addAction(tr("Open S&heet Library Folder"), this, [] {
        const QString dir = QString::fromStdString(chiply::defaultUserSheetsDir());
        QDir().mkpath(dir);
        QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
    });
    edit->addAction(tr("Open Block &Library Folder"), this, [] {
        const QString dir = QString::fromStdString(chiply::defaultUserBlocksDir());
        QDir().mkpath(dir);
        QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
    });
#endif

    QMenu* view = menuBar()->addMenu(tr("&View"));
    view->addAction(tr("Zoom &In  (+)"), this, [this] { if (auto* s = current()) s->view()->zoomIn(); });
    view->addAction(tr("Zoom &Out  (-)"), this, [this] { if (auto* s = current()) s->view()->zoomOut(); });
    view->addAction(tr("&Fit  (F)"), this, [this] { if (auto* s = current()) s->view()->fitContents(); });
    view->addAction(tr("Toggle &Grid  (G)"), this, [this] { if (auto* s = current()) s->view()->toggleGrid(); });
    QMenu* themeMenu = view->addMenu(tr("&Theme"));
    auto* themeGroup = new QActionGroup(this);
    for (Theme::Mode m : {Theme::Mode::System, Theme::Mode::Light, Theme::Mode::Dark}) {
        QAction* a = themeMenu->addAction(Theme::modeName(m));
        a->setCheckable(true);
        a->setChecked(Theme::instance().mode() == m);
        themeGroup->addAction(a);
        connect(a, &QAction::triggered, this, [m] { Theme::instance().setMode(m); });
        // Keep the check mark in step with the toolbar switch.
        connect(&Theme::instance(), &Theme::changed, a, [a, m] { a->setChecked(Theme::instance().mode() == m); });
    }
    QMenu* hoverMenu = view->addMenu(tr("&Hover Text Size"));
    auto* hoverGroup = new QActionGroup(this);
    const QStringList hoverNames{tr("Normal"), tr("Large"), tr("Extra Large"), tr("Huge")};
    for (int i = 0; i < 4; ++i) {
        const int pts = Theme::kHoverSizes[i];
        QAction* a = hoverMenu->addAction(tr("%1 (%2 pt)").arg(hoverNames[i]).arg(pts));
        a->setCheckable(true);
        a->setChecked(Theme::instance().hoverTextSize() == pts);
        hoverGroup->addAction(a);
        connect(a, &QAction::triggered, this, [pts] { Theme::instance().setHoverTextSize(pts); });
    }
    view->addSeparator();
    view->addAction(tr("Next Tab"), QKeySequence(Qt::CTRL | Qt::Key_Tab), this, [this] {
        if (m_tabs->count())
            m_tabs->setCurrentIndex((m_tabs->currentIndex() + 1) % m_tabs->count());
    });
    view->addAction(tr("Previous Tab"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Tab), this, [this] {
        if (m_tabs->count())
            m_tabs->setCurrentIndex((m_tabs->currentIndex() + m_tabs->count() - 1) % m_tabs->count());
    });

    m_inspector = new Inspector(this);
    connect(m_inspector, &Inspector::openFileRequested, this, [this](const QString& path) { openFile(path); });
    auto* inspDock = new QDockWidget(tr("Inspector"), this);
    inspDock->setObjectName("inspector");
    inspDock->setWidget(m_inspector);
    addDockWidget(Qt::RightDockWidgetArea, inspDock);
    view->addAction(inspDock->toggleViewAction());

    // Violations (PLAN.md 5.2): below the Inspector.
    m_violDock = new QDockWidget(tr("Violations"), this);
    m_violDock->setObjectName("violations");
    m_violations = new ViolationsPane(m_violDock);
    m_violDock->setWidget(m_violations);
    addDockWidget(Qt::RightDockWidgetArea, m_violDock);
    splitDockWidget(inspDock, m_violDock, Qt::Vertical);
    // The column starts at the Inspector's width whatever the panes show;
    // a saved layout (restoreLayout) or the user's splitter drag overrides it.
    resizeDocks({inspDock, m_violDock}, {Inspector::kDefaultWidth, Inspector::kDefaultWidth}, Qt::Horizontal);
    view->addAction(m_violDock->toggleViewAction());
    QMenu* check = menuBar()->addMenu(tr("&Check"));
    QAction* full = check->addAction(tr("Run &Full DRC"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_D), this, [this] {
        if (auto* s = current()) {
            s->runDrc(true);
            m_violDock->show();
        }
    });
    full->setObjectName("fullDrcAction");
    QAction* nextV = check->addAction(tr("&Next Violation"), QKeySequence(Qt::Key_F8), this, [this] {
        m_violDock->show();
        m_violations->next();
    });
    nextV->setShortcutContext(Qt::ApplicationShortcut);
    QAction* prevV = check->addAction(tr("&Previous Violation"), QKeySequence(Qt::SHIFT | Qt::Key_F8), this, [this] {
        m_violDock->show();
        m_violations->previous();
    });
    prevV->setShortcutContext(Qt::ApplicationShortcut);
    check->addSeparator();
    check->addAction(m_violDock->toggleViewAction());
    QAction* names = view->addAction(tr("Show Part &Names"));
    names->setCheckable(true);
    names->setChecked(QSettings().value("view/showNames", false).toBool());
    PartItem::setShowNames(names->isChecked());
    connect(names, &QAction::toggled, this, [this](bool on) {
        QSettings().setValue("view/showNames", on);
        PartItem::setShowNames(on);
        for (int i = 0; i < m_tabs->count(); ++i) {
            sessionAt(i)->view()->scene()->update();
            sessionAt(i)->view()->resetCachedContent();
            for (QGraphicsItem* it : sessionAt(i)->view()->scene()->items())
                if (it->type() == PartItem::Type)
                    it->update();
        }
    });

    // Undo history: every step of the active tab; click one to jump there.
    auto* historyDock = new QDockWidget(tr("Undo History"), this);
    historyDock->setObjectName("undoHistory");
    auto* history = new QUndoView(m_undoGroup, historyDock);
    history->setEmptyLabel(tr("<opened file>"));
    QFont hf = history->font();
    hf.setPointSize(15);
    history->setFont(hf);
    historyDock->setWidget(history);
    addDockWidget(Qt::RightDockWidgetArea, historyDock);
    historyDock->hide();
    view->addSeparator();
    view->addAction(historyDock->toggleViewAction());

    // Waveforms (PLAN.md 6.4): logic analyzer channels and probes.
    m_waveDock = new QDockWidget(tr("Waveforms"), this);
    m_waveDock->setObjectName("waveforms");
    auto* waveScroll = new QScrollArea(m_waveDock);
    waveScroll->setWidgetResizable(true);
    waveScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    waveScroll->setFrameShape(QFrame::NoFrame);
    m_waveforms = new WaveformView(waveScroll);
    waveScroll->setWidget(m_waveforms);
    m_waveDock->setWidget(waveScroll);
    addDockWidget(Qt::BottomDockWidgetArea, m_waveDock);
    m_waveDock->hide();
    view->addAction(m_waveDock->toggleViewAction());
    connect(m_waveforms, &WaveformView::saveRequested, this, &MainWindow::saveTrace);

    QMenu* simMenu = menuBar()->addMenu(tr("&Simulation"));
    simMenu->addAction(m_playAction);
    simMenu->addAction(m_stepAction);
    simMenu->addAction(m_stopAction);
    simMenu->addSeparator();
#ifndef Q_OS_WASM
    QMenu* engine = simMenu->addMenu(tr("&Engine"));
    auto* engines = new QActionGroup(this);
    m_engineBuiltin = engine->addAction(tr("&Built-in"));
    m_engineBuiltin->setObjectName("engineBuiltin");
    m_engineVerilator = engine->addAction(tr("&Verilator (chip)"));
    m_engineVerilator->setObjectName("engineVerilator");
    for (QAction* a : {m_engineBuiltin, m_engineVerilator}) {
        a->setCheckable(true);
        engines->addAction(a);
    }
    const bool vlPref = QSettings().value("sim/engine").toString() == QStringLiteral("verilator");
    (vlPref ? m_engineVerilator : m_engineBuiltin)->setChecked(true);
    connect(engines, &QActionGroup::triggered, this, [this](QAction* a) {
        QSettings().setValue("sim/engine", a == m_engineVerilator ? "verilator" : "builtin");
    });
    connect(engine, &QMenu::aboutToShow, this, &MainWindow::updateEngineActions);
    engine->setToolTipsVisible(true);
#endif
    m_saveTraceAction = simMenu->addAction(tr("Save Trace as &VCD..."), this, &MainWindow::saveTrace);
    m_saveTraceAction->setObjectName("saveTraceAction");
    m_gtkwaveAction = simMenu->addAction(tr("Open Trace in &GTKWave"), this, &MainWindow::openInGtkWave);
    m_gtkwaveAction->setObjectName("gtkwaveAction");
    simMenu->addSeparator();
    simMenu->addAction(m_waveDock->toggleViewAction());

    QMenu* help = menuBar()->addMenu(tr("&Help"));
    help->addAction(tr("&About Chiply"), this, [this] {
        notify(QMessageBox::Information, tr("About Chiply"),
            tr("<b>Chiply %1</b><p>Wokwi-compatible logic schematic editor.</p>"
               "<p>BSD 3-Clause License.</p>").arg(QApplication::applicationVersion()));
    });
}

EditorSession* MainWindow::sessionAt(int index) const
{
    QWidget* w = m_tabs->widget(index);
    return w ? w->property("session").value<EditorSession*>() : nullptr;
}

EditorSession* MainWindow::current() const
{
    return sessionAt(m_tabs->currentIndex());
}

int MainWindow::addSession(EditorSession* s)
{
    s->setParent(this);
    m_undoGroup->addStack(s->undoStack());
    s->view()->setProperty("session", QVariant::fromValue(s));
    connect(s, &EditorSession::titleChanged, this, &MainWindow::updateTitles);
    connect(s->view(), &SchematicView::zoomChanged, this, [this] { updateStatus(); });
    connect(s->view(), &SchematicView::hint, this, [this, s](const QString& text) {
        if (s != current())
            return;
        if (text.isEmpty())
            statusBar()->clearMessage();
        else
            statusBar()->showMessage(text); // stays until the bus is done
    });
    connect(s->view(), &SchematicView::cursorMoved, this, [this, s](QPointF p) {
        if (s == current() && m_posLabel)
            m_posLabel->setText(QStringLiteral("x %1  y %2   |").arg(p.x(), 0, 'f', 1).arg(p.y(), 0, 'f', 1));
    });
    connect(s->view(), &SchematicView::addPartRequested, this, &MainWindow::addPart);
    connect(s, &EditorSession::simulationChanged, this, [this, s] {
        if (s == current())
            updateSimControls();
    });
    connect(s, &EditorSession::drcChanged, this, [this, s] {
        if (s == current())
            updateDrcStatus();
    });
    connect(s, &EditorSession::probesChanged, this, [this, s] {
        // A new probe opens the Waveforms pane.
        if (s == current() && !s->probes().isEmpty() && m_waveDock && !m_waveDock->isVisible())
            m_waveDock->show();
        if (s == current())
            updateTraceActions();
    });
    auto edit = [this] {
        if (m_inspector)
            m_inspector->focusName();
    };
    connect(s->view(), &SchematicView::editPartRequested, this, edit);
    connect(s->miniToolbar(), &MiniToolbar::editRequested, this, edit);
    connect(s, &EditorSession::selectionChanged, this, [this, s] {
        if (s == current())
            updateStatus();
    });
    int i = m_tabs->addTab(s->view(), s->displayName());
    m_tabs->setCurrentIndex(i);
    updateTitles();
    return i;
}

namespace {
// Sun or moon, drawn as shapes (not colour alone) so they read for
// colour-blind users too.
QIcon themeIcon(bool dark)
{
    const int s = 64;
    QPixmap pm(s, s);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    const QPointF c(s / 2.0, s / 2.0);
    if (dark) {
        // Crescent moon: a disc with an offset disc cut out.
        QPainterPath moon;
        moon.addEllipse(c, 22, 22);
        QPainterPath bite;
        bite.addEllipse(c + QPointF(12, -9), 19, 19);
        // Pale fill with a dark outline: visible on light and dark toolbars.
        p.setPen(QPen(QColor(0x3a, 0x3a, 0x3a), 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(QColor(0xf5, 0xe6, 0x9a));
        p.drawPath(moon.subtracted(bite));
    } else {
        // Sun: disc with eight rays.
        p.setPen(QPen(QColor(0xf2, 0x9c, 0x00), 5, Qt::SolidLine, Qt::RoundCap));
        for (int i = 0; i < 8; ++i) {
            const double a = i * M_PI / 4;
            p.drawLine(c + QPointF(std::cos(a) * 20, std::sin(a) * 20), c + QPointF(std::cos(a) * 29, std::sin(a) * 29));
        }
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0xff, 0xc1, 0x07));
        p.drawEllipse(c, 14, 14);
    }
    return QIcon(pm);
}
} // namespace

QPixmap MainWindow::titlePixmap(const QColor& ink, qreal dpr)
{
    QFont f(QStringLiteral("Helvetica"));
    f.setPixelSize(24);
    f.setBold(true);
    const QFontMetricsF fm(f);
    const QString word = QStringLiteral("CHIPLY");
    const double gap = 3.5;   // extra space between letters
    const double xs = 3.2;    // half-size of the little x marks
    double width = 0;
    for (QChar c : word)
        width += fm.horizontalAdvance(c) + gap;
    const double h = fm.capHeight() + 2 * (2 * xs + 4) + 2;
    QPixmap pm(QSize(int(std::ceil(width + 2)), int(std::ceil(h))) * dpr);
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setFont(f);
    const double baseline = 2 * xs + 5 + fm.capHeight();
    double x = 1;
    QPen xpen(ink, 1.6, Qt::SolidLine, Qt::RoundCap);
    for (QChar c : word) {
        const double adv = fm.horizontalAdvance(c);
        p.setPen(ink);
        p.drawText(QPointF(x, baseline), QString(c));
        const double cx = x + adv / 2;
        const double above = baseline - fm.capHeight() - xs - 3;
        const double below = baseline + xs + 3;
        p.setPen(xpen);
        for (double cy : {above, below}) {
            p.drawLine(QPointF(cx - xs, cy - xs), QPointF(cx + xs, cy + xs));
            p.drawLine(QPointF(cx - xs, cy + xs), QPointF(cx + xs, cy - xs));
        }
        x += adv + gap;
    }
    return pm;
}

QIcon MainWindow::simIcon(SimIcon which)
{
    // Shapes, not colour, carry the meaning (colour-blind friendly).
    const int s = 64;
    QPixmap pm(s, s);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(QColor(0x2a, 0x2a, 0x2a), 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    switch (which) {
    case SimIcon::Play: {
        p.setBrush(QColor(0x43, 0xa0, 0x47));
        QPolygonF t;
        t << QPointF(18, 10) << QPointF(54, 32) << QPointF(18, 54);
        p.drawPolygon(t);
        break;
    }
    case SimIcon::Pause:
        p.setBrush(QColor(0xff, 0xb3, 0x00));
        p.drawRoundedRect(QRectF(14, 10, 13, 44), 3, 3);
        p.drawRoundedRect(QRectF(37, 10, 13, 44), 3, 3);
        break;
    case SimIcon::Stop:
        p.setBrush(QColor(0xe5, 0x39, 0x35));
        p.drawRoundedRect(QRectF(13, 13, 38, 38), 4, 4);
        break;
    case SimIcon::Step: {
        p.setBrush(QColor(0x1e, 0x88, 0xe5));
        QPolygonF t;
        t << QPointF(12, 12) << QPointF(40, 32) << QPointF(12, 52);
        p.drawPolygon(t);
        p.drawRoundedRect(QRectF(44, 12, 9, 40), 2, 2);
        break;
    }
    }
    return QIcon(pm);
}

void MainWindow::playPause()
{
    EditorSession* s = current();
    if (!s)
        return;
    if (s->sim()) {
        if (s->sim()->running())
            s->sim()->pause();
        else
            s->sim()->play();
        s->view()->setFocus();
        updateSimControls();
        return;
    }
#ifndef Q_OS_WASM
    if (s->usesBlocks() && m_engineVerilator && !m_engineVerilator->isChecked()) {
        QPointer<EditorSession> sp(s);
        ask(tr("Custom Blocks"),
            tr("This design has custom blocks, which only the Verilator engine can simulate. "
               "Simulate with Verilator?\n\nNo: simulate with the built-in engine (the blocks' outputs stay floating)."),
            QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, QMessageBox::Yes, [this, sp](QMessageBox::StandardButton b) {
                if (b == QMessageBox::Cancel || !sp)
                    return;
                if (b == QMessageBox::Yes) {
                    m_engineVerilator->setChecked(true);
                    QSettings().setValue("sim/engine", "verilator");
                }
                startSimulation(sp);
            });
        return;
    }
#endif
    startSimulation(s);
}

void MainWindow::startSimulation(EditorSession* s)
{
    std::shared_ptr<chiply::sim::ChipBackend> chip;
#ifndef Q_OS_WASM
    if (m_engineVerilator && m_engineVerilator->isChecked()) {
        bool fallBack = false;
        chip = buildVerilatorChip(s, &fallBack);
        if (!chip && !fallBack)
            return;
    }
#endif
    s->startSimulation(chip);
    if (!s->sim())
        return;
    if (!s->sim()->error().isEmpty())
        statusBar()->showMessage(s->sim()->error(), 8000);
    // Simulate anyway (Wokwi does), but say so and show where.
    if (const int errs = s->unwaivedCount(chiply::drc::Severity::Error)) {
        statusBar()->showMessage(tr("Simulating with %1: see the Violations pane").arg(countOf(errs, "DRC error", "DRC errors")), 8000);
        m_violDock->show();
    }
    // A logic analyzer in the design opens the Waveforms pane.
    if (s->trace() && s->trace()->hasAnalyzer() && m_waveDock && !m_waveDock->isVisible())
        m_waveDock->show();
    s->sim()->play();
    s->view()->setFocus();
    updateSimControls();
}

namespace {
// Verilator and a compiler, looked up once (two short process runs).
const std::optional<chiply::vl::Tools>& verilatorTools(QString* why = nullptr)
{
    static std::string reason;
    static const std::optional<chiply::vl::Tools> tools = chiply::vl::findTools(&reason);
    if (why)
        *why = QString::fromStdString(reason);
    return tools;
}
} // namespace

void MainWindow::updateEngineActions()
{
    QString why;
    const auto& tools = verilatorTools(&why);
    m_engineVerilator->setEnabled(tools.has_value() || m_engineVerilator->isChecked());
    m_engineVerilator->setToolTip(tools ? tr("Simulate the chip in Verilator %1 (%2); the board stays built in")
                                              .arg(QString::fromStdString(tools->version), QString::fromStdString(tools->verilator))
                                        : why);
    m_engineBuiltin->setToolTip(tr("Chiply's own simulator: no other tools needed"));
}

std::shared_ptr<chiply::sim::ChipBackend> MainWindow::buildVerilatorChip(EditorSession* s, bool* fallBack)
{
    *fallBack = false;
    auto offerBuiltin = [&](const QString& text, const QString& details) {
        QMessageBox box(QMessageBox::Warning, tr("Verilator"), text, QMessageBox::Cancel, this);
        QPushButton* builtin = box.addButton(tr("Use Built-in Simulator"), QMessageBox::AcceptRole);
        box.setDefaultButton(builtin);
        if (!details.isEmpty())
            box.setDetailedText(details);
        box.exec();
        *fallBack = box.clickedButton() == builtin;
    };
    QString why;
    const auto& tools = verilatorTools(&why);
    if (!tools) {
        offerBuiltin(tr("Verilator cannot be used: %1.").arg(why), {});
        return nullptr;
    }
#ifdef __EMSCRIPTEN__
    offerBuiltin(why, {});
    return nullptr;
#else
    const chiply::Document doc = s->document(); // the dialog is modal: no edits meanwhile
    const std::string baseDir = s->baseDir().toStdString();
    std::atomic<bool> cancel{false}, done{false};
    std::mutex mu;
    std::string status = "Starting";
    std::shared_ptr<chiply::sim::ChipBackend> chip;
    std::string error, log;
    chiply::vl::BuildInfo info;
    std::thread worker([&] {
        chiply::vl::BuildOptions bo;
        bo.cancel = &cancel;
        bo.baseDir = baseDir;
        bo.status = [&](const std::string& line) {
            std::lock_guard<std::mutex> g(mu);
            status = line;
        };
        try {
            chip = chiply::vl::buildChip(doc, *tools, bo, &info);
        } catch (const chiply::vl::BuildError& e) {
            error = e.what();
            log = e.log;
        } catch (const std::exception& e) {
            error = e.what();
        }
        done = true;
    });
    QProgressDialog dlg(tr("Building the chip with Verilator..."), tr("Cancel"), 0, 0, this);
    dlg.setWindowTitle(tr("Verilator"));
    dlg.setWindowModality(Qt::WindowModal);
    dlg.setMinimumDuration(400); // a cached chip loads without a flash of dialog
    dlg.setMinimumWidth(420);
    while (!done) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 30);
        if (dlg.wasCanceled())
            cancel = true;
        {
            std::lock_guard<std::mutex> g(mu);
            dlg.setLabelText(tr("Building the chip with Verilator %1...\n%2")
                                 .arg(QString::fromStdString(tools->version), QString::fromStdString(status)));
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
    }
    worker.join();
    dlg.close();
    if (!chip) {
        if (!cancel)
            offerBuiltin(tr("Building the chip with Verilator failed: %1").arg(QString::fromStdString(error)),
                         QString::fromStdString(log));
        return nullptr;
    }
    statusBar()->showMessage(info.fromCache ? tr("%1: chip loaded from the cache").arg(QString::fromStdString(chip->name()))
                                            : tr("%1: chip built in %2 s").arg(QString::fromStdString(chip->name())).arg(info.seconds, 0, 'f', 1),
                             6000);
    return chip;
#endif
}

namespace {
QString findGtkWave()
{
    QString exe = QStandardPaths::findExecutable(QStringLiteral("gtkwave"));
    if (exe.isEmpty())
        exe = QStandardPaths::findExecutable(QStringLiteral("gtkwave"),
                                             {QStringLiteral("/opt/homebrew/bin"), QStringLiteral("/usr/local/bin"),
                                              QStringLiteral("/opt/local/bin")});
#ifdef Q_OS_MACOS
    if (exe.isEmpty() && QFileInfo::exists(QStringLiteral("/Applications/gtkwave.app")))
        exe = QStringLiteral("/Applications/gtkwave.app");
#endif
    return exe;
}
} // namespace

void MainWindow::exportPreflight(EditorSession* s, const QString& what, std::function<void()> go)
{
    s->runDrc(true);
    const int errors = s->unwaivedCount(chiply::drc::Severity::Error);
    const int warnings = s->unwaivedCount(chiply::drc::Severity::Warning);
    if (errors) {
        auto* box = new QMessageBox(QMessageBox::Critical, what,
                                    tr("The design has %1. The exported Verilog would not behave like the schematic.")
                                        .arg(countOf(errors, "DRC error", "DRC errors")),
                                    QMessageBox::NoButton, this);
        box->setAttribute(Qt::WA_DeleteOnClose);
        QPushButton* show = box->addButton(tr("Show Violations"), QMessageBox::AcceptRole);
        QPushButton* anyway = box->addButton(tr("Export Anyway"), QMessageBox::DestructiveRole);
        box->addButton(QMessageBox::Cancel);
        box->setDefaultButton(show);
        connect(box, &QMessageBox::finished, this, [this, box, show, anyway, go = std::move(go)](int) {
            if (box->clickedButton() == show) {
                m_violDock->show();
                m_violations->next();
            } else if (box->clickedButton() == anyway) {
                go();
            }
        });
        box->open();
        return;
    }
    if (warnings) {
        ask(what, tr("The design has %1. Export anyway?").arg(countOf(warnings, "DRC warning", "DRC warnings")),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes, [go = std::move(go)](QMessageBox::StandardButton b) {
                if (b == QMessageBox::Yes)
                    go();
            });
        return;
    }
    go();
}

void MainWindow::exportVerilog()
{
    EditorSession* s = current();
    if (!s)
        return;
    QPointer<EditorSession> sp(s);
    exportPreflight(s, tr("Export Verilog"), [this, sp] {
        if (sp)
            exportVerilogNow(sp);
    });
}

void MainWindow::exportVerilogNow(EditorSession* s)
{
    const QFileInfo fi(s->filePath().isEmpty() ? QDir(lastFolder()).filePath(QStringLiteral("untitled.json")) : s->filePath());
    QString stem = fi.completeBaseName();
    if (stem.endsWith(QStringLiteral(".diagram")))
        stem.chop(8);
    const QString module = QString::fromStdString(chiply::defaultModuleName(stem.toStdString()));
#ifdef Q_OS_WASM
    try {
        chiply::VerilogOptions o;
        o.moduleName = module.toStdString();
        o.sourceName = fi.fileName().toStdString();
        o.baseDir = s->baseDir().toStdString();
        const std::string v = chiply::writeVerilog(s->document(), chiply::PartLibrary::builtin(), o);
        QFileDialog::saveFileContent(QByteArray::fromStdString(v), module + ".v");
        if (chiply::usesChiplyCells(s->document()))
            QFileDialog::saveFileContent(QByteArray(chiply::chiplyCellsV()), QStringLiteral("chiply_cells.v"));
        statusBar()->showMessage(tr("Downloading %1.v (needs cells.v)").arg(module), 8000);
    } catch (const std::exception& e) {
        notify(QMessageBox::Critical, tr("Export Verilog"), QString::fromUtf8(e.what()));
    }
    return;
#endif
    const QString path = QFileDialog::getSaveFileName(this, tr("Export Verilog"), QDir(fi.absolutePath()).filePath(module + ".v"),
                                                      tr("Verilog (*.v)"));
    if (path.isEmpty())
        return;
    try {
        chiply::VerilogOptions o;
        o.moduleName = QFileInfo(path).completeBaseName().toStdString();
        if (!chiply::drc::isValidVerilogId(o.moduleName))
            o.moduleName = module.toStdString();
        o.sourceName = fi.fileName().toStdString();
        o.baseDir = s->baseDir().toStdString();
        const std::string v = chiply::writeVerilog(s->document(), chiply::PartLibrary::builtin(), o);
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate) || f.write(v.data(), qint64(v.size())) != qint64(v.size()))
            throw std::runtime_error(tr("Cannot write %1: %2").arg(path, f.errorString()).toStdString());
        QString needs = tr("needs cells.v");
        if (chiply::usesChiplyCells(s->document())) {
            const QString cells = QFileInfo(path).dir().filePath(QStringLiteral("chiply_cells.v"));
            QFile cf(cells);
            const std::string text = chiply::chiplyCellsV();
            if (!cf.open(QIODevice::WriteOnly | QIODevice::Truncate) || cf.write(text.data(), qint64(text.size())) != qint64(text.size()))
                throw std::runtime_error(tr("Cannot write %1: %2").arg(cells, cf.errorString()).toStdString());
            needs = tr("needs cells.v; chiply_cells.v written beside it");
        }
        QStringList copied;
        for (const std::string& f : chiply::blockSources(s->document())) {
            const QString src = QString::fromStdString(f);
            const QString dest = QFileInfo(path).dir().filePath(QFileInfo(src).fileName());
            if (QFileInfo(dest).absoluteFilePath() == QFileInfo(src).absoluteFilePath())
                continue;
            QFile::remove(dest);
            if (!QFile::copy(src, dest))
                throw std::runtime_error(tr("Cannot copy %1 to %2").arg(src, dest).toStdString());
            copied << QFileInfo(src).fileName();
        }
        if (!copied.isEmpty())
            needs += tr("; block sources copied: %1").arg(copied.join(QStringLiteral(", ")));
        statusBar()->showMessage(tr("Exported module %1 to %2 (%3)").arg(QString::fromStdString(o.moduleName), path, needs), 8000);
    } catch (const std::exception& e) {
        notify(QMessageBox::Critical, tr("Export Verilog"), QString::fromUtf8(e.what()));
    }
}

void MainWindow::exportTtProject()
{
#ifdef Q_OS_WASM
    notify(QMessageBox::Information, tr("Export Tiny Tapeout Project"),
                             tr("The browser version cannot write a project folder. Export the Verilog instead."));
    return;
#endif
    EditorSession* s = current();
    if (!s)
        return;
    QPointer<EditorSession> sp(s);
    exportPreflight(s, tr("Export Tiny Tapeout Project"), [this, sp] {
        if (sp)
            exportTtProjectNow(sp);
    });
}

void MainWindow::exportTtProjectNow(EditorSession* s)
{
    const QString start = s->filePath().isEmpty() ? lastFolder() : QFileInfo(s->filePath()).absolutePath();
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Tiny Tapeout project folder (with info.yaml)"), start);
    if (dir.isEmpty())
        return;
    QString module;
    QFile yaml(QDir(dir).filePath(QStringLiteral("info.yaml")));
    if (yaml.open(QIODevice::ReadOnly))
        module = QString::fromStdString(chiply::infoYamlTopModule(yaml.readAll().toStdString()));
    if (module.isEmpty()) {
        QString stem = QFileInfo(s->filePath()).completeBaseName();
        if (stem.endsWith(QStringLiteral(".diagram")))
            stem.chop(8);
        bool ok = false;
        module = QInputDialog::getText(this, tr("Top Module"),
                                       tr("Top module name (must start with tt_um_; include your GitHub name to make it unique):"),
                                       QLineEdit::Normal, QString::fromStdString(chiply::defaultModuleName(stem.toStdString())), &ok);
        if (!ok)
            return;
    }
    if (!module.startsWith(QStringLiteral("tt_um_")) || !chiply::drc::isValidVerilogId(module.toStdString())) {
        notify(QMessageBox::Warning, tr("Export Tiny Tapeout Project"),
                             tr("\"%1\" is not a valid top module name: it must start with tt_um_ and be a Verilog identifier.").arg(module));
        return;
    }
    try {
        chiply::VerilogOptions o;
        o.moduleName = module.toStdString();
        o.sourceName = QFileInfo(s->filePath()).fileName().toStdString();
        o.baseDir = s->baseDir().toStdString();
        QString report;
        for (const std::string& line : chiply::exportTtProject(s->document(), chiply::PartLibrary::builtin(), dir.toStdString(), o))
            report += QString::fromStdString(line) + QLatin1Char('\n');
        notify(QMessageBox::Information, tr("Export Tiny Tapeout Project"), report);
    } catch (const std::exception& e) {
        notify(QMessageBox::Critical, tr("Export Tiny Tapeout Project"), QString::fromUtf8(e.what()));
    }
}

void MainWindow::reloadLibraries(bool report)
{
    // Custom blocks and sheets: the user libraries, then each open design's
    // own folders (which win for the same name).
    for (int i = 0; i < m_tabs->count(); ++i)
        if (sessionAt(i)->simulating()) { // a running simulation uses the definitions
            if (report)
                statusBar()->showMessage(tr("Stop the simulation before reloading blocks and sheets"), 6000);
            return;
        }
    QStringList warnings, names;
    auto add = [&](const std::vector<std::string>& loaded, const std::vector<std::string>& warn) {
        for (const std::string& w : warn)
            warnings << QString::fromStdString(w);
        for (const std::string& n : loaded)
            if (!names.contains(QString::fromStdString(n)))
                names << QString::fromStdString(n);
    };
    {
        const chiply::BlockScan b = chiply::scanBlocks({chiply::defaultUserBlocksDir()});
        add(b.loaded, b.warnings);
        const chiply::SheetScan sh = chiply::scanSheets({chiply::defaultUserSheetsDir()});
        add(sh.loaded, sh.warnings);
    }
    for (int i = 0; i < m_tabs->count(); ++i) {
        const std::string path = sessionAt(i)->filePath().toStdString();
        const chiply::BlockScan b = chiply::scanBlocks(chiply::blockRoots(path));
        add(b.loaded, b.warnings);
        const chiply::SheetScan sh = chiply::scanSheets(chiply::sheetRoots(path));
        add(sh.loaded, sh.warnings);
    }
    for (int i = 0; i < m_tabs->count(); ++i)
        sessionAt(i)->refreshParts();
    if (m_inspector)
        m_inspector->setSession(current());
    warnings.removeDuplicates();
    if (report && !warnings.isEmpty())
        notify(QMessageBox::Warning, tr("Blocks and Sheets"), warnings.join(QStringLiteral("\n\n")));
    if (report)
        statusBar()->showMessage(names.isEmpty() ? tr("No custom blocks or sheets found")
                                                 : tr("Blocks and sheets: %1").arg(names.join(QStringLiteral(", "))), 6000);
}

void MainWindow::replaceInNames()
{
    EditorSession* s = current();
    if (!s || s->selectedPartIds().empty()) {
        statusBar()->showMessage(tr("Select the parts whose names to change first"), 5000);
        return;
    }
    auto* dlg = new QDialog(this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setWindowTitle(tr("Find and Replace in Names"));
    QFont f = dlg->font();
    f.setPointSize(std::max(f.pointSize(), 15));
    dlg->setFont(f);
    auto* form = new QFormLayout;
    auto* from = new QLineEdit(dlg);
    from->setObjectName("replaceFrom");
    auto* to = new QLineEdit(dlg);
    to->setObjectName("replaceTo");
    form->addRow(tr("Find"), from);
    form->addRow(tr("Replace with"), to);
    auto* preview = new QLabel(dlg);
    preview->setWordWrap(true);
    preview->setTextFormat(Qt::PlainText);
    preview->setMinimumWidth(420);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dlg);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Replace"));
    auto* lay = new QVBoxLayout(dlg);
    lay->addWidget(new QLabel(tr("In the names of the %1 selected parts (wires follow):").arg(s->selectedPartIds().size()), dlg));
    lay->addLayout(form);
    lay->addWidget(preview);
    lay->addWidget(buttons);
    QPointer<EditorSession> sp(s);
    auto update = [sp, from, to, preview, buttons] {
        if (!sp)
            return;
        QString err;
        const auto map = sp->previewReplaceInNames(from->text(), to->text(), &err);
        QString text;
        if (!err.isEmpty()) {
            text = err;
            preview->setStyleSheet(QStringLiteral("color: #e53935;"));
        } else {
            preview->setStyleSheet(QString());
            int n = 0;
            for (const auto& [a, b] : map) {
                if (++n > 8) {
                    text += tr("... and %1 more").arg(map.size() - 8);
                    break;
                }
                text += QString::fromStdString(a) + QStringLiteral("  \u2192  ") + QString::fromStdString(b) + QLatin1Char('\n');
            }
            if (map.empty())
                text = from->text().isEmpty() ? tr("Type the text to find.") : tr("No selected name contains it.");
        }
        preview->setText(text.trimmed());
        buttons->button(QDialogButtonBox::Ok)->setEnabled(err.isEmpty() && !map.empty());
    };
    connect(from, &QLineEdit::textChanged, dlg, update);
    connect(to, &QLineEdit::textChanged, dlg, update);
    connect(buttons, &QDialogButtonBox::accepted, dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, dlg, &QDialog::reject);
    connect(dlg, &QDialog::accepted, this, [this, sp, from, to] {
        if (!sp)
            return;
        int changed = 0;
        const QString err = sp->replaceInNames(from->text(), to->text(), &changed);
        if (!err.isEmpty())
            notify(QMessageBox::Warning, tr("Find and Replace in Names"), err);
        else
            statusBar()->showMessage(tr("Renamed %1").arg(countOf(changed, "part", "parts")), 5000);
    });
    update();
    dlg->open();
}

void MainWindow::setExtensions(bool on)
{
    EditorSession::setExtensionsEnabled(on);
    for (int i = 0; i < m_tabs->count(); ++i)
        sessionAt(i)->setExtensionsAllowed(on);
    if (m_extensionsAction && m_extensionsAction->isChecked() != on)
        m_extensionsAction->setChecked(on);
    updateDrcStatus();
    statusBar()->showMessage(on ? tr("Extended mode: Chiply's own parts are in Add Part. Designs using them do not load in Wokwi.")
                                : tr("Wokwi mode: only Wokwi's parts are offered."),
                             6000);
}

void MainWindow::notify(QMessageBox::Icon icon, const QString& title, const QString& text, const QString& details)
{
    auto* box = new QMessageBox(icon, title, text, QMessageBox::Ok, this);
    box->setAttribute(Qt::WA_DeleteOnClose);
    if (!details.isEmpty())
        box->setDetailedText(details);
    box->open();
}

void MainWindow::ask(const QString& title, const QString& text, QMessageBox::StandardButtons buttons,
                     QMessageBox::StandardButton def, std::function<void(QMessageBox::StandardButton)> then)
{
    auto* box = new QMessageBox(QMessageBox::Question, title, text, buttons, this);
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->setDefaultButton(def);
    connect(box, &QMessageBox::finished, this, [box, then = std::move(then)](int) {
        QMessageBox::StandardButton b = box->standardButton(box->clickedButton());
        if (b == QMessageBox::NoButton)
            b = QMessageBox::Cancel; // closed without a choice
        then(b);
    });
    box->open();
}

void MainWindow::updateDrcStatus()
{
    if (m_modeLabel) {
        const bool ext = EditorSession::extensionsEnabled();
        m_modeLabel->setText(ext ? tr("EXTENDED MODE   |") : tr("WOKWI MODE   |"));
        m_modeLabel->setToolTip(ext ? tr("Chiply Extensions are on (Edit menu): extra cells are offered")
                                    : tr("Only Wokwi's parts are offered (turn on Edit > Chiply Extensions for more)"));
    }
    EditorSession* s = current();
    if (!m_drcLabel)
        return;
    if (!s) {
        m_drcLabel->clear();
        return;
    }
    const int e = s->unwaivedCount(chiply::drc::Severity::Error);
    const int w = s->unwaivedCount(chiply::drc::Severity::Warning);
    m_drcLabel->setText((e + w == 0 ? tr("DRC clean") : QStringLiteral("\u2716 ") + countOf(e, "error", "errors") + QStringLiteral("  \u25B2 ") + countOf(w, "warning", "warnings")) + QStringLiteral("   |"));
    m_drcLabel->setToolTip(tr("Design rule checks: F8 steps through the violations"));
}

void MainWindow::updateTraceActions()
{
    EditorSession* s = current();
    const bool has = s && s->trace() && !s->trace()->channels().empty();
    if (m_saveTraceAction)
        m_saveTraceAction->setEnabled(has);
    if (m_gtkwaveAction) {
        static const QString gtk = findGtkWave();
        m_gtkwaveAction->setVisible(!gtk.isEmpty());
        m_gtkwaveAction->setEnabled(has);
    }
}

void MainWindow::saveTrace()
{
    EditorSession* s = current();
    if (!s)
        return;
#ifdef Q_OS_WASM
    const QString path = QDir(QDir::tempPath()).filePath(QFileInfo(s->defaultTracePath()).fileName());
#else
    const QString path = QFileDialog::getSaveFileName(this, tr("Save Trace"), s->defaultTracePath(),
                                                      tr("Value change dump (*.vcd)"));
    if (path.isEmpty())
        return;
#endif
    const QString err = s->writeTraceVcd(path);
#ifdef Q_OS_WASM
    if (err.isEmpty()) {
        QFile f(path);
        if (f.open(QIODevice::ReadOnly))
            QFileDialog::saveFileContent(f.readAll(), QFileInfo(path).fileName());
    }
#endif
    if (!err.isEmpty())
        notify(QMessageBox::Warning, tr("Save Trace"), err);
    else
        statusBar()->showMessage(tr("Saved trace %1").arg(path), 5000);
}

void MainWindow::openInGtkWave()
{
#ifdef Q_OS_WASM
    return; // no processes in the browser (the action is hidden)
#else
    EditorSession* s = current();
    const QString gtk = findGtkWave();
    if (!s || gtk.isEmpty())
        return;
    const QString path = QDir(QDir::tempPath()).filePath(QFileInfo(s->defaultTracePath()).fileName());
    const QString err = s->writeTraceVcd(path);
    if (!err.isEmpty()) {
        notify(QMessageBox::Warning, tr("Open in GTKWave"), err);
        return;
    }
    const bool ok = gtk.endsWith(QStringLiteral(".app"))
        ? QProcess::startDetached(QStringLiteral("open"), {QStringLiteral("-a"), gtk, path})
        : QProcess::startDetached(gtk, {path});
    statusBar()->showMessage(ok ? tr("Opened %1 in GTKWave").arg(path) : tr("Could not start %1").arg(gtk), 5000);
#endif
}

void MainWindow::updateSimControls()
{
    EditorSession* s = current();
    const bool active = s && s->sim();
    const bool running = active && s->sim()->running();
    if (m_playAction) {
        m_playAction->setIcon(simIcon(running ? SimIcon::Pause : SimIcon::Play));
        m_playAction->setText(running ? tr("Pause") : tr("Play"));
        m_playAction->setToolTip(running ? tr("Pause the simulation") : active ? tr("Resume the simulation")
                                                                               : tr("Start simulating (edit mode is locked while simulating)"));
        m_playAction->setProperty("running", running);
        m_stepAction->setEnabled(active && !running);
        m_stepAction->setToolTip(tr("Advance one clock period"));
        m_stopAction->setEnabled(active);
        m_stopAction->setToolTip(tr("Stop the simulation and return to editing"));
    }
    // No editing while simulating.
    if (m_editMenu)
        for (QAction* a : m_editMenu->actions())
            if (!a->isSeparator() && !a->text().startsWith(tr("&Copy")) && !a->text().startsWith(tr("Select"))
                && a != m_extensionsAction && a->objectName() != QLatin1String("busRouteAction"))
                a->setEnabled(!active);
    if (m_addPartAction)
        m_addPartAction->setEnabled(!active);
    if (m_undoGroup)
        m_undoGroup->setActiveStack(active ? nullptr : (s ? s->undoStack() : nullptr));
    updateTraceActions();
    if (m_waveforms)
        m_waveforms->update();
    updateStatus();
}

void MainWindow::changeEvent(QEvent* e)
{
    QMainWindow::changeEvent(e);
    // Not while the window is being built: the theme may still be setting
    // itself up (and sends these events synchronously on macOS).
    if (m_constructed && (e->type() == QEvent::PaletteChange || e->type() == QEvent::ThemeChange))
        updateThemeButton();
}

void MainWindow::updateThemeButton()
{
    if (m_updatingTheme)
        return; // redrawing can itself raise palette events
    m_updatingTheme = true;
    struct Reset {
        bool& flag;
        ~Reset() { flag = false; }
    } reset{m_updatingTheme};
    // The ink follows the theme setting itself: when the theme changes, the
    // widgets' palettes catch up only a moment later (changeEvent redraws).
    if (m_titleLabel)
        m_titleLabel->setPixmap(titlePixmap(Theme::instance().isDark() ? QColor(0xf0, 0xf0, 0xf0) : QColor(0x10, 0x10, 0x10),
                                            devicePixelRatioF()));
    if (!m_themeButton)
        return;
    const bool dark = Theme::instance().isDark();
    m_themeButton->setIcon(themeIcon(dark));
    m_themeButton->setProperty("dark", dark);
    m_themeButton->setToolTip(dark ? tr("Dark mode - click for light mode") : tr("Light mode - click for dark mode"));
}

void MainWindow::addPart()
{
    EditorSession* s = current();
    if (!s)
        return;
    auto* dlg = new PartPalette(this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    QPointer<EditorSession> sp(s);
    connect(dlg, &QDialog::accepted, this, [sp, dlg] {
        if (sp)
            sp->startPlacing(dlg->chosenType().toStdString());
    });
    connect(dlg, &QDialog::finished, this, [sp](int) {
        if (sp)
            sp->view()->setFocus();
    });
    dlg->open();
}

void MainWindow::paste()
{
    EditorSession* s = current();
    if (!s)
        return;
    const QString text = QApplication::clipboard()->text();
    const QStringList blocks = s->existingTtBlocksIn(text);
    if (!blocks.isEmpty()) {
        QPointer<EditorSession> sp(s);
        ask(tr("Paste"),
            tr("The pasted parts include Tiny Tapeout I/O blocks that this design already has (%1).\n\n"
               "Skip them and the wires connected to them?").arg(blocks.join(", ")),
            QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, QMessageBox::Yes, [this, sp, text](QMessageBox::StandardButton b) {
                if (b != QMessageBox::Cancel && sp)
                    pasteText(sp, text, b == QMessageBox::Yes);
            });
        return;
    }
    pasteText(s, text, false);
}

void MainWindow::pasteText(EditorSession* s, const QString& text, bool skip)
{
    SchematicView* v = s->view();
    const QPoint cur = v->viewport()->mapFromGlobal(QCursor::pos());
    const QPointF anchor = v->mapToScene(v->viewport()->rect().contains(cur) ? cur : v->viewport()->rect().center());
    const EditorSession::PasteReport rep = s->paste(text, anchor, skip);
    if (!rep.error.isEmpty()) {
        statusBar()->showMessage(rep.error, 5000);
        return;
    }
    QString msg = tr("Pasted %1").arg(countOf(rep.parts, "part", "parts"));
    if (rep.renamed)
        msg += tr(", %1 renamed").arg(rep.renamed);
    if (rep.skippedBlocks)
        msg += tr(", %1 skipped").arg(countOf(rep.skippedBlocks, "I/O block", "I/O blocks"));
    if (rep.droppedWires)
        msg += tr(", %1 dropped").arg(countOf(rep.droppedWires, "wire", "wires"));
    statusBar()->showMessage(msg + tr(" - click to drop, Esc to cancel"), 8000);
    v->setFocus();
}

void MainWindow::newFile()
{
    auto* s = new EditorSession;
    s->newDocument(QString());
    addSession(s);
}

void MainWindow::newFromTemplate()
{
    auto* s = new EditorSession;
    s->newFromTemplate();
    addReplacingBlank(s);
    statusBar()->showMessage(tr("New design from the Tiny Tapeout template: Save As to name it"), 6000);
}

void MainWindow::addReplacingBlank(EditorSession* s)
{
    // Replace the initial empty, unmodified Untitled tab.
    if (m_tabs->count() == 1 && sessionAt(0)->filePath().isEmpty() && !sessionAt(0)->isModified()
        && sessionAt(0)->document().parts.empty()) {
        EditorSession* old = sessionAt(0);
        m_tabs->removeTab(0);
        old->view()->deleteLater();
        old->deleteLater();
    }
    addSession(s);
    s->view()->fitContents();
}

void MainWindow::openDialog()
{
#ifdef Q_OS_WASM
    // The browser's file picker; the file lands in the in-memory file system.
    QFileDialog::getOpenFileContent(tr("Wokwi diagrams (*.json)"), [this](const QString& name, const QByteArray& data) {
        if (name.isEmpty())
            return;
        const QString path = QDir(QDir::tempPath()).filePath(QFileInfo(name).fileName());
        QFile f(path);
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate) && f.write(data) == data.size()) {
            f.close();
            openFile(path);
        } else {
            notify(QMessageBox::Critical, tr("Open failed"), tr("Cannot keep %1 in memory").arg(name));
        }
    });
#else
    const QStringList paths = QFileDialog::getOpenFileNames(
        this, tr("Open diagram"), lastFolder(), tr("Wokwi diagrams (*.json);;All files (*)"));
    for (const QString& p : paths)
        openFile(p);
#endif
}

bool MainWindow::openFile(const QString& path)
{
    const QString canonical = QFileInfo(path).canonicalFilePath();
    for (int i = 0; i < m_tabs->count(); ++i) {
        if (QFileInfo(sessionAt(i)->filePath()).canonicalFilePath() == canonical && !canonical.isEmpty()) {
            m_tabs->setCurrentIndex(i);
            return true;
        }
    }
    auto* s = new EditorSession;
    try {
        s->load(path);
    } catch (const std::exception& e) {
        delete s;
        notify(QMessageBox::Critical, tr("Open failed"), tr("%1\n\n%2").arg(path, QString::fromUtf8(e.what())));
        return false;
    }
    addReplacingBlank(s);
    rememberFolder(path);
    if (s->usesExtensionParts() && !EditorSession::extensionsEnabled())
        statusBar()->showMessage(tr("This design uses Chiply extension parts. Turn on Edit > Chiply Extensions to add more."), 10000);
    if (!s->loadWarnings().isEmpty())
        statusBar()->showMessage(tr("%1 while loading: %2").arg(countOf(int(s->loadWarnings().size()), "warning", "warnings"))
                                     .arg(s->loadWarnings().first()), 10000);
    return true;
}

void MainWindow::refitAll()
{
    for (int i = 0; i < m_tabs->count(); ++i)
        sessionAt(i)->view()->fitContents();
}

bool MainWindow::saveSession(EditorSession* s, bool saveAs)
{
    QString path = s->filePath();
#ifdef Q_OS_WASM
    // In the browser a save is a download: the design is kept in memory
    // (so it counts as saved) and offered as a file.
    (void)saveAs;
    const QString name = path.isEmpty() ? QStringLiteral("diagram.json") : QFileInfo(path).fileName();
    path = QDir(QDir::tempPath()).filePath(name);
    try {
        s->save(path);
    } catch (const std::exception& e) {
        notify(QMessageBox::Critical, tr("Save failed"), QString::fromUtf8(e.what()));
        return false;
    }
    QFile f(path);
    if (f.open(QIODevice::ReadOnly))
        QFileDialog::saveFileContent(f.readAll(), name);
    statusBar()->showMessage(tr("Downloading %1").arg(name), 4000);
    return true;
#else
    if (saveAs || path.isEmpty()) {
        path = QFileDialog::getSaveFileName(this, tr("Save diagram"),
            path.isEmpty() ? QDir(lastFolder()).filePath(QStringLiteral("diagram.json")) : path, tr("Wokwi diagrams (*.json)"));
        if (path.isEmpty())
            return false;
    }
    try {
        s->save(path);
    } catch (const std::exception& e) {
        notify(QMessageBox::Critical, tr("Save failed"), QString::fromUtf8(e.what()));
        return false;
    }
    rememberFolder(path);
    if (QFileInfo(path).dir().dirName() == QLatin1String("sheets"))
        reloadLibraries(false); // a sheet was saved: designs that use it follow
    if (s->usesExtensionParts())
        statusBar()->showMessage(tr("Saved %1 (uses Chiply extension parts: Wokwi cannot load it)").arg(path), 8000);
    else
        statusBar()->showMessage(tr("Saved %1").arg(path), 4000);
    return true;
#endif
}

bool MainWindow::closeTab(int index)
{
    EditorSession* s = sessionAt(index);
    if (!s)
        return false;
    if (s->isModified()) {
        m_tabs->setCurrentIndex(index);
#ifdef Q_OS_WASM
        // No nested event loops in the browser: the tab goes when the
        // question is answered.
        QPointer<EditorSession> sp(s);
        ask(tr("Unsaved changes"), tr("Save changes to %1?").arg(s->displayName()),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save, [this, sp](QMessageBox::StandardButton b) {
                if (!sp || b == QMessageBox::Cancel || (b == QMessageBox::Save && !saveSession(sp, false)))
                    return;
                removeSession(sp);
            });
        return false;
#else
        auto r = QMessageBox::question(this, tr("Unsaved changes"),
            tr("Save changes to %1?").arg(s->displayName()),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
        if (r == QMessageBox::Cancel || (r == QMessageBox::Save && !saveSession(s, false)))
            return false;
#endif
    }
    removeSession(s);
    return true;
}

void MainWindow::removeSession(EditorSession* s)
{
    for (int i = 0; i < m_tabs->count(); ++i)
        if (sessionAt(i) == s) {
            m_tabs->removeTab(i);
            break;
        }
    s->view()->deleteLater();
    s->deleteLater();
}

void MainWindow::saveLayout()
{
    if (!s_persistLayout)
        return;
    QSettings s;
    s.setValue("window/geometry", saveGeometry());
    s.setValue("window/state", saveState(kLayoutVersion));
}

void MainWindow::restoreLayout()
{
    if (!s_persistLayout)
        return;
    QSettings s;
    restoreGeometry(s.value("window/geometry").toByteArray());
    restoreState(s.value("window/state").toByteArray(), kLayoutVersion);
}

void MainWindow::moveEvent(QMoveEvent* event)
{
    QMainWindow::moveEvent(event);
    m_saveLayout.start();
}

void MainWindow::resizeEvent(QResizeEvent* event)
{
    QMainWindow::resizeEvent(event);
    m_saveLayout.start();
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    while (m_tabs->count() > 0) {
        if (!closeTab(m_tabs->count() - 1)) {
            event->ignore();
            return;
        }
    }
    saveLayout();
    event->accept();
}

void MainWindow::updateTitles()
{
    for (int i = 0; i < m_tabs->count(); ++i) {
        EditorSession* s = sessionAt(i);
        m_tabs->setTabText(i, s->displayName() + (s->isModified() ? QStringLiteral(" •") : QString()));
        m_tabs->setTabToolTip(i, s->filePath());
    }
    EditorSession* s = current();
    setWindowTitle(s ? tr("%1 - Chiply").arg(s->displayName()) : tr("Chiply"));
    updateStatus();
}

void MainWindow::updateStatus()
{
    EditorSession* s = current();
    if (!s) {
        m_zoomLabel->clear();
        return;
    }
    QString sel;
    if (SimRunner* r = s->sim()) {
        const double t = double(r->now()) / 1e12; // seconds
        QString time = t < 1e-3 ? tr("%1 us").arg(t * 1e6, 0, 'f', 1)
            : t < 1 ? tr("%1 ms").arg(t * 1e3, 0, 'f', 2) : tr("%1 s").arg(t, 0, 'f', 3);
        sel = r->running() ? tr("SIMULATING  %1  (%2x real time)").arg(time).arg(r->speed(), 0, 'f', 2)
                           : tr("SIMULATION PAUSED  %1").arg(time);
        if (r->engineName() != tr("built-in"))
            sel += QStringLiteral("  [") + r->engineName() + QLatin1Char(']');
        sel += QStringLiteral("   |   ");
    }
    const auto sum = s->selectionSummary();
    if (!sum.empty()) {
        sel = tr("Selected: %1 parts, %2 wires").arg(sum.parts).arg(sum.wires);
        if (sum.implicitWires)
            sel += tr(" (+%1 wires move with them)").arg(sum.implicitWires);
        if (sum.stretchWires)
            sel += tr(", %1 stretch").arg(sum.stretchWires);
        sel += QStringLiteral("   |   ");
    }
    m_zoomLabel->setText(sel + tr("%1 parts, %2 wires   |   %3%")
                                   .arg(s->document().parts.size())
                                   .arg(s->document().wires.size())
                                   .arg(qRound(s->view()->zoom() * 100)));
    setWindowTitle(tr("%1 - Chiply").arg(s->displayName()));
}
