#pragma once
#include <QMainWindow>

class EditorSession;
class QLabel;
class QTabWidget;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

    // Opens `path` in a new tab, or activates its tab if already open.
    bool openFile(const QString& path);

protected:
    void closeEvent(QCloseEvent* event) override;

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
    void openDialog();

    QTabWidget* m_tabs = nullptr;
    QLabel* m_zoomLabel = nullptr;
};
