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
    // when the selection changes. Contents scroll vertically instead.
    QSize sizeHint() const override { return {kWidth, 400}; }
    QSize minimumSizeHint() const override { return {kWidth, 100}; }
    static constexpr int kWidth = 380;

private:
    void rebuild();
    void clear();

    EditorSession* m_s = nullptr;
    QVBoxLayout* m_lay;     // inside the scroll area
    class QScrollArea* m_scroll;
    QWidget* m_body = nullptr;
    QLineEdit* m_name = nullptr;
};
