#pragma once
// Violations pane (PLAN.md 5.2): the active tab's DRC results.
//
// Header: counters per severity (shape + word, not colour alone), Full DRC
// button, Live checkbox, a Checks menu that turns checks on or off for this
// document, and a text filter. Rows are grouped by check; waived rows sit in
// a collapsed group at the end. Clicking a row snaps the canvas to it;
// F8 / Shift+F8 step through the rows. Right-click: waive / unwaive,
// disable the check, copy.
#include <QPointer>
#include <QSet>
#include <QWidget>

#include <string>

class EditorSession;
class QLabel;
class QLineEdit;
class QTreeWidget;
class QTreeWidgetItem;
class QCheckBox;
class QToolButton;
class QPushButton;

class ViolationsPane : public QWidget {
    Q_OBJECT
public:
    explicit ViolationsPane(QWidget* parent = nullptr);
    void setSession(EditorSession* s);

    // Steps to the next / previous violation and snaps to it.
    void next();
    void previous();
    // The row of a violation key, for tests.
    QTreeWidgetItem* itemFor(const std::string& key) const;
    QTreeWidget* tree() const { return m_tree; }
    QString countsText() const;

    // The pane never asks the dock column for room: its width is the
    // user's (or the default) and never follows the text it shows, so the
    // canvas does not jump while an edit re-runs the live check.
    QSize sizeHint() const override { return {kDefaultWidth, 300}; }
    QSize minimumSizeHint() const override { return {kMinWidth, 120}; }
    static constexpr int kDefaultWidth = 380;
    static constexpr int kMinWidth = 220;

private:
    void rebuild();
    void activate(QTreeWidgetItem* item);
    void step(int dir);
    void showContextMenu(const QPoint& pos);
    void updateChecksMenu();

    QPointer<EditorSession> m_session;
    QMetaObject::Connection m_conn;
    QLabel* m_counts = nullptr;
    QLabel* m_stats = nullptr;
    QPushButton* m_full = nullptr;
    QCheckBox* m_live = nullptr;
    QToolButton* m_checks = nullptr;
    QLineEdit* m_filter = nullptr;
    QTreeWidget* m_tree = nullptr;
    QSet<QString> m_collapsed; // group titles the user collapsed
    bool m_rebuilding = false;
};
