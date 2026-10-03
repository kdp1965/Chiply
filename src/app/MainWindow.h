#pragma once
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
    bool closeTab(int index);
    void updateTitles();
    void updateStatus();

    void newFile();
    void addPart();
    void paste();
    void openDialog();

    QTabWidget* m_tabs = nullptr;
    QTimer m_saveLayout;
    static inline bool s_persistLayout = true;
    class QUndoGroup* m_undoGroup = nullptr;
    class Inspector* m_inspector = nullptr;
    QLabel* m_zoomLabel = nullptr;
};
