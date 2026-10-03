#include "MainWindow.h"

#include "EditorSession.h"
#include "SchematicView.h"
#include "core/WokwiJson.h"

#include <QAction>
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

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    m_tabs = new QTabWidget(this);
    m_tabs->setTabsClosable(true);
    m_tabs->setMovable(true);
    m_tabs->setDocumentMode(true);
    setCentralWidget(m_tabs);
    connect(m_tabs, &QTabWidget::tabCloseRequested, this, [this](int i) { closeTab(i); });
    connect(m_tabs, &QTabWidget::currentChanged, this, [this] { updateStatus(); });

    m_zoomLabel = new QLabel(this);
    statusBar()->addPermanentWidget(m_zoomLabel);

    buildMenus();
    resize(1400, 900);
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

    QMenu* view = menuBar()->addMenu(tr("&View"));
    view->addAction(tr("Zoom &In  (+)"), this, [this] { if (auto* s = current()) s->view()->zoomIn(); });
    view->addAction(tr("Zoom &Out  (-)"), this, [this] { if (auto* s = current()) s->view()->zoomOut(); });
    view->addAction(tr("&Fit  (F)"), this, [this] { if (auto* s = current()) s->view()->fitContents(); });
    view->addAction(tr("Toggle &Grid  (G)"), this, [this] { if (auto* s = current()) s->view()->toggleGrid(); });
    view->addSeparator();
    view->addAction(tr("Next Tab"), QKeySequence(Qt::CTRL | Qt::Key_Tab), this, [this] {
        if (m_tabs->count())
            m_tabs->setCurrentIndex((m_tabs->currentIndex() + 1) % m_tabs->count());
    });
    view->addAction(tr("Previous Tab"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Tab), this, [this] {
        if (m_tabs->count())
            m_tabs->setCurrentIndex((m_tabs->currentIndex() + m_tabs->count() - 1) % m_tabs->count());
    });

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
    s->view()->setProperty("session", QVariant::fromValue(s));
    connect(s, &EditorSession::titleChanged, this, &MainWindow::updateTitles);
    connect(s->view(), &SchematicView::zoomChanged, this, [this] { updateStatus(); });
    int i = m_tabs->addTab(s->view(), s->displayName());
    m_tabs->setCurrentIndex(i);
    updateTitles();
    return i;
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

void MainWindow::closeEvent(QCloseEvent* event)
{
    while (m_tabs->count() > 0) {
        if (!closeTab(m_tabs->count() - 1)) {
            event->ignore();
            return;
        }
    }
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
    m_zoomLabel->setText(tr("%1 parts, %2 wires   |   %3%")
                             .arg(s->document().parts.size())
                             .arg(s->document().wires.size())
                             .arg(qRound(s->view()->zoom() * 100)));
    setWindowTitle(tr("%1 - Chiply").arg(s->displayName()));
}
