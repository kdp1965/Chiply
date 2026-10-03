#pragma once
// Waveforms pane (PLAN.md 6.4): the active tab's recording, one row per
// logic analyzer channel and probed net, live while simulating and kept
// after Stop.
//
// Values are told apart by shape, not colour alone: 1 = line at the top with
// a shaded band, 0 = line at the bottom, X = hatched block, Z = dashed
// middle line.
//
// Navigation: wheel or +/- zooms around the cursor, drag or Shift+wheel
// pans, F fits the whole recording, End follows the live edge again.
// Hovering shows a time cursor and every signal's value there.
#include "sim/Trace.h"

#include <QPointer>
#include <QWidget>

#include <memory>

class EditorSession;

class WaveformView : public QWidget {
    Q_OBJECT
public:
    explicit WaveformView(QWidget* parent = nullptr);

    void setSession(EditorSession* s);
    EditorSession* session() const { return m_session; }

    // Visible time window [start, start + span) in ps.
    chiply::sim::Time viewStart() const;
    chiply::sim::Time span() const { return m_span; }
    bool following() const { return m_follow; }
    void zoomBy(double factor, double anchorFraction = 1.0);
    void fit();
    void follow();
    // Row under a widget y coordinate (-1: none); the signal index.
    int rowAt(int y) const;
    int rowHeight() const;
    int labelWidth() const;

    QSize sizeHint() const override { return {800, 220}; }
    QSize minimumSizeHint() const override { return {200, 80}; }

signals:
    void saveRequested(); // context menu "Save Trace as VCD..."

protected:
    void paintEvent(QPaintEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void leaveEvent(QEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    void contextMenuEvent(QContextMenuEvent*) override;

private:
    std::shared_ptr<chiply::sim::Trace> trace() const;
    chiply::sim::Time endTime() const;
    chiply::sim::Time defaultSpan() const;
    double xOf(chiply::sim::Time t, chiply::sim::Time start, int plotWidth) const;
    QString rowLabel(int i) const;
    void syncHeight(); // tall enough for every row (the pane scrolls)

    QPointer<EditorSession> m_session;
    QMetaObject::Connection m_simConn, m_probeConn;
    chiply::sim::Time m_span = 0;   // 0: default for the design
    chiply::sim::Time m_start = 0;  // when not following
    bool m_follow = true;
    int m_hoverX = -1;
    bool m_dragging = false;
    int m_dragX = 0;
    chiply::sim::Time m_dragStart = 0;
};

// "1.25 ms", "40 us", "500 ns", "0".
QString formatTime(chiply::sim::Time ps);
// A time-axis label in the unit of the tick step: "1 500 020 us".
QString formatTick(chiply::sim::Time ps, chiply::sim::Time step);
