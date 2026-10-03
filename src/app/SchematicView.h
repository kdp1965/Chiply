#pragma once
// The canvas. M0 provides Wokwi-style navigation (PLAN.md 4.2):
//   wheel = zoom at cursor, Shift+wheel = horizontal pan, Ctrl/Cmd+wheel =
//   vertical pan, middle-drag or Space+drag = pan, arrow keys = pan,
//   left-drag on empty canvas = marquee, +/- = zoom, F = fit, G = grid.
#include <QGraphicsView>

class SchematicView : public QGraphicsView {
    Q_OBJECT
public:
    explicit SchematicView(QGraphicsScene* scene, QWidget* parent = nullptr);

    static constexpr double kGrid = 9.6; // 0.1 inch at Wokwi's 96 px/inch

    void zoomBy(double factor, QPoint anchorViewPos);
    void zoomIn();
    void zoomOut();
    void fitContents();
    void toggleGrid();
    double zoom() const { return transform().m11(); }
    void applyTheme();

signals:
    void zoomChanged(double zoom);

protected:
    void drawBackground(QPainter* painter, const QRectF& rect) override;
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;

private:
    void panBy(QPointF viewDelta);

    bool m_showGrid = true;
    bool m_spaceHeld = false;
    bool m_panning = false;
    QPoint m_lastPanPos;
};
