#include "MainWindow.h"

#include "EditorSession.h"
#include "PartPalette.h"
#include "Inspector.h"
#include "MiniToolbar.h"
#include "SchematicItems.h"
#include "SchematicView.h"
#include "Theme.h"
#include "core/WokwiJson.h"

#include <QAction>
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
#include <QTimer>
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
    QAction* addPart = tb->addAction(tr("+  Add Part"), this, &MainWindow::addPart);
    addPart->setToolTip(tr("Add a part (A)"));
    tb->addSeparator();
    tb->addAction(tr("Fit"), this, [this] { if (auto* s = current()) s->view()->fitContents(); });
    tb->addAction(tr("Zoom +"), this, [this] { if (auto* s = current()) s->view()->zoomIn(); });
    tb->addAction(tr("Zoom \u2212"), this, [this] { if (auto* s = current()) s->view()->zoomOut(); });

    QMenu* edit = menuBar()->addMenu(tr("&Edit"));
    QAction* undo = m_undoGroup->createUndoAction(this, tr("&Undo"));
    undo->setShortcut(QKeySequence::Undo);
    QAction* redo = m_undoGroup->createRedoAction(this, tr("&Redo"));
    redo->setShortcuts({QKeySequence::Redo, QKeySequence(Qt::CTRL | Qt::Key_Y)});
    edit->addAction(undo);
    edit->addAction(redo);
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
