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
#include "core/Verilog.h"
#include "vl/VerilatorChip.h"
#include "core/WokwiJson.h"

#include <QAction>
#include <QUrl>
#include <QDesktopServices>
#include <atomic>
#include <mutex>
#include <thread>
#include <QProgressDialog>
#include <QLineEdit>
#include <QPushButton>
#include <QInputDialog>
#include <QScrollArea>
#include <QDir>
#include <QProcess>
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
    m_drcLabel = new QLabel(this);
    m_drcLabel->setObjectName("drcStatus");
    m_drcLabel->setFont(sf);
    statusBar()->addPermanentWidget(m_drcLabel);
    statusBar()->addPermanentWidget(m_zoomLabel);

    chiply::scanBlocks({chiply::defaultUserBlocksDir()}); // the user's block library
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
    file->addAction(tr("Export &Tiny Tapeout Project..."), this, &MainWindow::exportTtProject)
        ->setObjectName("exportTtAction");
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
    QAction* reloadBlocks = edit->addAction(tr("Reload Custom &Blocks"), this, [this] {
        QStringList warnings;
        QStringList names;
        auto add = [&](const chiply::BlockScan& scan) {
            for (const std::string& w : scan.warnings)
                warnings << QString::fromStdString(w);
            for (const std::string& n : scan.loaded)
                if (!names.contains(QString::fromStdString(n)))
                    names << QString::fromStdString(n);
        };
        add(chiply::scanBlocks({chiply::defaultUserBlocksDir()}));
        for (int i = 0; i < m_tabs->count(); ++i) {
            EditorSession* s = sessionAt(i);
            add(chiply::scanBlocks(chiply::blockRoots(s->filePath().toStdString())));
            s->refreshParts();
        }
        if (!warnings.isEmpty())
            QMessageBox::warning(this, tr("Custom Blocks"), warnings.join(QStringLiteral("\n\n")));
        statusBar()->showMessage(names.isEmpty() ? tr("No custom blocks found") : tr("Custom blocks: %1").arg(names.join(QStringLiteral(", "))), 6000);
    });
    reloadBlocks->setObjectName("reloadBlocksAction");
    edit->addAction(tr("Open Block &Library Folder"), this, [] {
        const QString dir = QString::fromStdString(chiply::defaultUserBlocksDir());
        QDir().mkpath(dir);
        QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
    });

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
    m_saveTraceAction = simMenu->addAction(tr("Save Trace as &VCD..."), this, &MainWindow::saveTrace);
    m_saveTraceAction->setObjectName("saveTraceAction");
    m_gtkwaveAction = simMenu->addAction(tr("Open Trace in &GTKWave"), this, &MainWindow::openInGtkWave);
    m_gtkwaveAction->setObjectName("gtkwaveAction");
    simMenu->addSeparator();
    simMenu->addAction(m_waveDock->toggleViewAction());

    QMenu* help = menuBar()->addMenu(tr("&Help"));
    help->addAction(tr("&About Chiply"), this, [this] {
        QMessageBox::about(this, tr("About Chiply"),
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
    if (!s->sim()) {
        std::shared_ptr<chiply::sim::ChipBackend> chip;
        if (s->usesBlocks() && m_engineVerilator && !m_engineVerilator->isChecked()) {
            const auto answer = QMessageBox::question(
                this, tr("Custom Blocks"),
                tr("This design has custom blocks, which only the Verilator engine can simulate. "
                   "Simulate with Verilator?\n\nNo: simulate with the built-in engine (the blocks' outputs stay floating)."),
                QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, QMessageBox::Yes);
            if (answer == QMessageBox::Cancel)
                return;
            if (answer == QMessageBox::Yes) {
                m_engineVerilator->setChecked(true);
                QSettings().setValue("sim/engine", "verilator");
            }
        }
        if (m_engineVerilator && m_engineVerilator->isChecked()) {
            bool fallBack = false;
            chip = buildVerilatorChip(s, &fallBack);
            if (!chip && !fallBack)
                return;
        }
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
    }
    if (s->sim()->running())
        s->sim()->pause();
    else
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
    const chiply::Document doc = s->document(); // the dialog is modal: no edits meanwhile
    std::atomic<bool> cancel{false}, done{false};
    std::mutex mu;
    std::string status = "Starting";
    std::shared_ptr<chiply::sim::ChipBackend> chip;
    std::string error, log;
    chiply::vl::BuildInfo info;
    std::thread worker([&] {
        chiply::vl::BuildOptions bo;
        bo.cancel = &cancel;
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

bool MainWindow::exportPreflight(EditorSession* s, const QString& what)
{
    s->runDrc(true);
    const int errors = s->unwaivedCount(chiply::drc::Severity::Error);
    const int warnings = s->unwaivedCount(chiply::drc::Severity::Warning);
    if (errors) {
        QMessageBox box(QMessageBox::Critical, what,
                        tr("The design has %1. The exported Verilog would not behave like the schematic.")
                            .arg(countOf(errors, "DRC error", "DRC errors")),
                        QMessageBox::NoButton, this);
        QPushButton* show = box.addButton(tr("Show Violations"), QMessageBox::AcceptRole);
        QPushButton* anyway = box.addButton(tr("Export Anyway"), QMessageBox::DestructiveRole);
        box.addButton(QMessageBox::Cancel);
        box.setDefaultButton(show);
        box.exec();
        if (box.clickedButton() == show) {
            m_violDock->show();
            m_violations->next();
        }
        return box.clickedButton() == anyway;
    }
    if (warnings)
        return QMessageBox::question(this, what,
                                     tr("The design has %1. Export anyway?").arg(countOf(warnings, "DRC warning", "DRC warnings")))
            == QMessageBox::Yes;
    return true;
}

void MainWindow::exportVerilog()
{
    EditorSession* s = current();
    if (!s || !exportPreflight(s, tr("Export Verilog")))
        return;
    const QFileInfo fi(s->filePath().isEmpty() ? QStringLiteral("untitled.json") : s->filePath());
    QString stem = fi.completeBaseName();
    if (stem.endsWith(QStringLiteral(".diagram")))
        stem.chop(8);
    const QString module = QString::fromStdString(chiply::defaultModuleName(stem.toStdString()));
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
        QMessageBox::critical(this, tr("Export Verilog"), QString::fromUtf8(e.what()));
    }
}

void MainWindow::exportTtProject()
{
    EditorSession* s = current();
    if (!s || !exportPreflight(s, tr("Export Tiny Tapeout Project")))
        return;
    const QString start = s->filePath().isEmpty() ? QDir::homePath() : QFileInfo(s->filePath()).absolutePath();
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
        QMessageBox::warning(this, tr("Export Tiny Tapeout Project"),
                             tr("\"%1\" is not a valid top module name: it must start with tt_um_ and be a Verilog identifier.").arg(module));
        return;
    }
    try {
        chiply::VerilogOptions o;
        o.moduleName = module.toStdString();
        o.sourceName = QFileInfo(s->filePath()).fileName().toStdString();
        QString report;
        for (const std::string& line : chiply::exportTtProject(s->document(), chiply::PartLibrary::builtin(), dir.toStdString(), o))
            report += QString::fromStdString(line) + QLatin1Char('\n');
        QMessageBox::information(this, tr("Export Tiny Tapeout Project"), report);
    } catch (const std::exception& e) {
        QMessageBox::critical(this, tr("Export Tiny Tapeout Project"), QString::fromUtf8(e.what()));
    }
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
    const QString path = QFileDialog::getSaveFileName(this, tr("Save Trace"), s->defaultTracePath(),
                                                      tr("Value change dump (*.vcd)"));
    if (path.isEmpty())
        return;
    const QString err = s->writeTraceVcd(path);
    if (!err.isEmpty())
        QMessageBox::warning(this, tr("Save Trace"), err);
    else
        statusBar()->showMessage(tr("Saved trace %1").arg(path), 5000);
}

void MainWindow::openInGtkWave()
{
    EditorSession* s = current();
    const QString gtk = findGtkWave();
    if (!s || gtk.isEmpty())
        return;
    const QString path = QDir(QDir::tempPath()).filePath(QFileInfo(s->defaultTracePath()).fileName());
    const QString err = s->writeTraceVcd(path);
    if (!err.isEmpty()) {
        QMessageBox::warning(this, tr("Open in GTKWave"), err);
        return;
    }
    const bool ok = gtk.endsWith(QStringLiteral(".app"))
        ? QProcess::startDetached(QStringLiteral("open"), {QStringLiteral("-a"), gtk, path})
        : QProcess::startDetached(gtk, {path});
    statusBar()->showMessage(ok ? tr("Opened %1 in GTKWave").arg(path) : tr("Could not start %1").arg(gtk), 5000);
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
                && a != m_extensionsAction)
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

void MainWindow::updateThemeButton()
{
    if (m_titleLabel)
        m_titleLabel->setPixmap(titlePixmap(palette().color(QPalette::WindowText), devicePixelRatioF()));
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
    PartPalette dlg(this);
    if (dlg.exec() == QDialog::Accepted)
        s->startPlacing(dlg.chosenType().toStdString());
    s->view()->setFocus();
}

void MainWindow::paste()
{
    EditorSession* s = current();
    if (!s)
        return;
    const QString text = QApplication::clipboard()->text();
    bool skip = false;
    const QStringList blocks = s->existingTtBlocksIn(text);
    if (!blocks.isEmpty()) {
        auto r = QMessageBox::question(
            this, tr("Paste"),
            tr("The pasted parts include Tiny Tapeout I/O blocks that this design already has (%1).\n\n"
               "Skip them and the wires connected to them?").arg(blocks.join(", ")),
            QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, QMessageBox::Yes);
        if (r == QMessageBox::Cancel)
            return;
        skip = r == QMessageBox::Yes;
    }
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
    const QStringList paths = QFileDialog::getOpenFileNames(
        this, tr("Open diagram"), QString(), tr("Wokwi diagrams (*.json);;All files (*)"));
    for (const QString& p : paths)
        openFile(p);
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
        QMessageBox::critical(this, tr("Open failed"), tr("%1\n\n%2").arg(path, QString::fromUtf8(e.what())));
        return false;
    }
    addReplacingBlank(s);
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
    if (saveAs || path.isEmpty()) {
        path = QFileDialog::getSaveFileName(this, tr("Save diagram"),
            path.isEmpty() ? QStringLiteral("diagram.json") : path, tr("Wokwi diagrams (*.json)"));
        if (path.isEmpty())
            return false;
    }
    try {
        s->save(path);
    } catch (const std::exception& e) {
        QMessageBox::critical(this, tr("Save failed"), QString::fromUtf8(e.what()));
        return false;
    }
    if (s->usesExtensionParts())
        statusBar()->showMessage(tr("Saved %1 (uses Chiply extension parts: Wokwi cannot load it)").arg(path), 8000);
    else
        statusBar()->showMessage(tr("Saved %1").arg(path), 4000);
    return true;
}

bool MainWindow::closeTab(int index)
{
    EditorSession* s = sessionAt(index);
    if (!s)
        return false;
    if (s->isModified()) {
        m_tabs->setCurrentIndex(index);
        auto r = QMessageBox::question(this, tr("Unsaved changes"),
            tr("Save changes to %1?").arg(s->displayName()),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
        if (r == QMessageBox::Cancel || (r == QMessageBox::Save && !saveSession(s, false)))
            return false;
    }
    m_tabs->removeTab(index);
    s->view()->deleteLater();
    s->deleteLater();
    return true;
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
