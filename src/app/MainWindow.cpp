#include "MainWindow.h"

#include "EditorSession.h"
#include "PartPalette.h"
#include "Inspector.h"
#include "MiniToolbar.h"
#include "SimRunner.h"
#include "SchematicItems.h"
#include "SchematicView.h"
#include "Theme.h"
#include "core/WokwiJson.h"

#include <QAction>
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
        updateSimControls();
        updateStatus();
    });

    m_zoomLabel = new QLabel(this);
    QFont sf = m_zoomLabel->font();
    sf.setPointSize(14); // readable status line
    m_zoomLabel->setFont(sf);
    statusBar()->addPermanentWidget(m_zoomLabel);

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
    // Simulation controls, left of the zoom buttons: Play/Pause, Step, Stop.
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
    tb->addSeparator();
    tb->addAction(tr("Zoom +"), this, [this] { if (auto* s = current()) s->view()->zoomIn(); });
    tb->addAction(tr("Zoom \u2212"), this, [this] { if (auto* s = current()) s->view()->zoomOut(); });
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
                statusBar()->showMessage(tr("Copied %n part(s)", nullptr, int(s->selectedPartIds().size())), 3000);
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
        s->startSimulation();
        if (!s->sim())
            return;
        if (!s->sim()->error().isEmpty())
            statusBar()->showMessage(s->sim()->error(), 8000);
    }
    if (s->sim()->running())
        s->sim()->pause();
    else
        s->sim()->play();
    s->view()->setFocus();
    updateSimControls();
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
            if (!a->isSeparator() && !a->text().startsWith(tr("&Copy")) && !a->text().startsWith(tr("Select")))
                a->setEnabled(!active);
    if (m_addPartAction)
        m_addPartAction->setEnabled(!active);
    if (m_undoGroup)
        m_undoGroup->setActiveStack(active ? nullptr : (s ? s->undoStack() : nullptr));
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
    QString msg = tr("Pasted %n part(s)", nullptr, rep.parts);
    if (rep.renamed)
        msg += tr(", %n renamed", nullptr, rep.renamed);
    if (rep.skippedBlocks)
        msg += tr(", %n I/O block(s) skipped", nullptr, rep.skippedBlocks);
    if (rep.droppedWires)
        msg += tr(", %n wire(s) dropped", nullptr, rep.droppedWires);
    statusBar()->showMessage(msg + tr(" - click to drop, Esc to cancel"), 8000);
    v->setFocus();
}

void MainWindow::newFile()
{
    auto* s = new EditorSession;
    s->newDocument(QString());
    addSession(s);
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
    if (!s->loadWarnings().isEmpty())
        statusBar()->showMessage(tr("%n warning(s) while loading: %1", nullptr, int(s->loadWarnings().size()))
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
