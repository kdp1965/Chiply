#pragma once
#include "sim/ChipBackend.h"

#include <memory>
#include <QMainWindow>
#include <QTimer>

class EditorSession;
class QLabel;
class QTabWidget;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

    // Opens `path` in a new tab, or activates its tab if already open.
    bool openFile(const QString& path);
    // Fits every tab's view to its contents (used after the window is sized).
    void refitAll();

    // Window size/position, toolbar and dock layout (QSettings).
    void saveLayout();
    void restoreLayout();
    static constexpr int kLayoutVersion = 1;
    // Off for headless --screenshot runs: never read or write the user's
    // saved layout. Must be set before constructing the window.
    static void setPersistLayout(bool on) { s_persistLayout = on; }

protected:
    void closeEvent(QCloseEvent* event) override;
    void moveEvent(QMoveEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void buildMenus();
    EditorSession* current() const;
    EditorSession* sessionAt(int index) const;
    int addSession(EditorSession* s);
    bool saveSession(EditorSession* s, bool saveAs);
    // Full DRC before an export: errors stop it unless the user insists,
    // warnings ask. True to go ahead.
    bool exportPreflight(EditorSession* s, const QString& what);
    void exportVerilog();
    // Simulation engine (PLAN.md 6.6): built-in, or the chip in Verilator.
    QAction* m_engineBuiltin = nullptr;
    QAction* m_engineVerilator = nullptr;
    void updateEngineActions();
    // Builds (or loads the cached) Verilator chip for `s`, with a progress
    // dialog. Null if cancelled or failed (after telling the user);
    // *fallBack is set when the user chose the built-in simulator instead.
    std::shared_ptr<chiply::sim::ChipBackend> buildVerilatorChip(EditorSession* s, bool* fallBack);
    // Adds a tab, replacing the initial empty, unmodified Untitled one.
    void addReplacingBlank(EditorSession* s);
    void exportTtProject();
    bool closeTab(int index);
    void updateTitles();
    void updateStatus();

    void newFile();
    void newFromTemplate();
    void addPart();
    void updateThemeButton();
    void playPause();
    void updateSimControls();
    enum class SimIcon { Play, Pause, Stop, Step };
    static QIcon simIcon(SimIcon which);
    static QPixmap titlePixmap(const QColor& ink, qreal dpr);
    void paste();
    void openDialog();

    QTabWidget* m_tabs = nullptr;
    QTimer m_saveLayout;
    class QToolButton* m_themeButton = nullptr;
    class QLabel* m_titleLabel = nullptr;
    QAction* m_playAction = nullptr;
    QAction* m_stepAction = nullptr;
    QAction* m_stopAction = nullptr;
    QAction* m_addPartAction = nullptr;
    QMenu* m_editMenu = nullptr;
    static inline bool s_persistLayout = true;
    class QUndoGroup* m_undoGroup = nullptr;
    class Inspector* m_inspector = nullptr;
    class WaveformView* m_waveforms = nullptr;
    class ViolationsPane* m_violations = nullptr;
    class QDockWidget* m_violDock = nullptr;
    class QLabel* m_drcLabel = nullptr;
    class QLabel* m_modeLabel = nullptr;
    class QLabel* m_posLabel = nullptr;
    void replaceInNames();
    QAction* m_extensionsAction = nullptr;
    void setExtensions(bool on);
    void updateDrcStatus();
    class QDockWidget* m_waveDock = nullptr;
    QAction* m_saveTraceAction = nullptr;
    QAction* m_gtkwaveAction = nullptr;
    void saveTrace();
    void openInGtkWave();
    void updateTraceActions();
    QLabel* m_zoomLabel = nullptr;
};
