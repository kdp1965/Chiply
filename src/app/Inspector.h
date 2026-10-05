#pragma once
// Right-hand dock showing and editing the selection (PLAN.md 4.5).
#include <QWidget>

class EditorSession;
class QFormLayout;
class QLabel;
class QLineEdit;
class QVBoxLayout;

class Inspector : public QWidget {
    Q_OBJECT
public:
    explicit Inspector(QWidget* parent = nullptr);
    void setSession(EditorSession* s);
    // Puts the cursor in the id field (F2, double-click, mini toolbar Edit).
    void focusName();
    // Constant size hints: the dock must not resize (and shift the canvas)
    // when the selection changes, but the user can drag its width anywhere
    // from kMinWidth up. Contents scroll instead of pushing the dock wider.
    QSize sizeHint() const override { return {kDefaultWidth, 400}; }
    QSize minimumSizeHint() const override { return {kMinWidth, 100}; }
    static constexpr int kDefaultWidth = 380;
    static constexpr int kMinWidth = 220;

signals:
    void openFileRequested(const QString& path); // "Open Sheet" on a sheet instance

private:
    void rebuild();
    void clear();

    EditorSession* m_s = nullptr;
    QVBoxLayout* m_lay;     // inside the scroll area
    class QScrollArea* m_scroll;
    QWidget* m_body = nullptr;
    QLineEdit* m_name = nullptr;
};
