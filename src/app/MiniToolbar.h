#pragma once
// Wokwi's small floating toolbar above a single selected part:
// rotate, edit (Inspector), delete.
#include <QWidget>

class EditorSession;

class MiniToolbar : public QWidget {
    Q_OBJECT
public:
    MiniToolbar(EditorSession* s, QWidget* viewport);
    void reposition();

signals:
    void editRequested();

private:
    EditorSession* m_s;
};
