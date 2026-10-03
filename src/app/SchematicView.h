#pragma once
// The canvas. M0 provides Wokwi-style navigation (PLAN.md 4.2):
//   wheel = zoom at cursor, Shift+wheel = horizontal pan, Ctrl/Cmd+wheel =
//   vertical pan, middle-drag, Shift+drag or Space+drag = pan, arrow keys
//   move the diagram in the arrow's direction,
//   left-drag on empty canvas = marquee, +/- = zoom, F = fit, G = grid.
//
// Selection (PLAN.md 4.3): click selects one item, Shift+click (no drag) or
// Ctrl/Cmd+click toggles, click on empty canvas or Esc clears, Ctrl/Cmd+A
// selects all. Marquee selects items fully inside; Alt = anything touched;
// Ctrl/Cmd = add to the current selection. Auto-scrolls at the edges.
#include "core/WirePath.h"

#include <QGraphicsView>
#include <QTimer>

class WireItem;

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

    // Selection operations (each emits selectionEdited once).
    void clearSelection();
    void selectAll();
    void selectOnly(QGraphicsItem* item);
    void toggleSelected(QGraphicsItem* item);
    // Selects by marquee rectangle in scene coordinates.
    void selectInRect(const QRectF& sceneRect, bool crossing, bool add);
    // Selectable item under a viewport position (parts, text, wires).
    QGraphicsItem* selectableAt(QPoint viewPos) const;

signals:
    void zoomChanged(double zoom);
    void selectionEdited();
    // A wire segment drag finished with a new route (index into doc.wires).
    void wireRouteEdited(int wireIndex, std::vector<chiply::Point> route);

protected:
    void drawBackground(QPainter* painter, const QRectF& rect) override;
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void drawForeground(QPainter* painter, const QRectF& rect) override;

private:
    void panBy(QPointF viewDelta);
    void clickAt(QPoint viewPos, Qt::KeyboardModifiers mods);
    void finishMarquee(Qt::KeyboardModifiers mods);
    void autoScrollTick();
    QRectF marqueeSceneRect() const;

    bool m_showGrid = true;
    bool m_spaceHeld = false;
    bool m_panning = false;
    bool m_shiftPending = false; // Shift+press: pan if it moves, click if not
    QPoint m_pressPos;
    QPoint m_lastPanPos;

    void updateHandleDrag(QPoint viewPos, Qt::KeyboardModifiers mods);

    enum class Press { None, Item, Empty, Marquee, Handle };
    WireItem* m_dragWire = nullptr;
    std::size_t m_dragSegment = 0;
    bool m_dragHorizontal = true;
    std::vector<chiply::Point> m_dragRoute; // simplified route at drag start
    std::vector<chiply::Point> m_dragResult;
    Press m_press = Press::None;
    QPointF m_marqueeStart;   // scene
    QPoint m_lastMousePos;    // viewport
    Qt::KeyboardModifiers m_lastMods;
    QTimer m_autoScroll;
};
