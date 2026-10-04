#pragma once
// The canvas. M0 provides Wokwi-style navigation (PLAN.md 4.2):
//   wheel = zoom at cursor, Shift+wheel = horizontal pan, Ctrl/Cmd+wheel =
//   vertical pan, left-drag on empty canvas, middle-drag or Space+drag =
//   pan (as in Wokwi), arrow keys move the diagram in the arrow's direction,
//   Shift+drag = marquee, +/- = zoom, F = fit, G = grid.
//   With parts selected, arrow keys move them one grid step (Shift: five);
//   Ctrl/Cmd+arrows always pan. Dragging a part moves the selection (Alt:
//   half-grid snap, Ctrl/Cmd: no snap). R rotates, D duplicates, Delete
//   deletes.
//
// Selection (PLAN.md 4.3): click selects one item, Shift+click (no drag) or
// Ctrl/Cmd+click toggles, click on empty canvas or Esc clears, Ctrl/Cmd+A
// selects all. Shift+drag marquee selects items fully inside; Alt = anything
// touched; Ctrl/Cmd+drag = marquee that adds to the current selection. Auto-scrolls at the edges.
#include "core/PartLibrary.h"
#include "core/WirePath.h"

#include <QGraphicsView>
#include <QElapsedTimer>
#include <QTimer>

#include <functional>

class PartItem;
class QGraphicsPathItem;
class WireItem;

// Wokwi's wire color keys: 0 black, 1 brown, 2 red, 3 orange, 4 gold,
// 5 green, 6 blue, 7 violet, 8 gray, 9 white, C cyan, L limegreen,
// M magenta, P purple, Y yellow. Empty for any other key.
QString wokwiColorForKey(int key);

class SchematicView : public QGraphicsView {
    Q_OBJECT
public:
    explicit SchematicView(QGraphicsScene* scene, QWidget* parent = nullptr);

    static constexpr double kGrid = 9.6; // 0.1 inch at Wokwi's 96 px/inch
    static constexpr int kHandleMargin = 24; // screen px kept between handles and the edge
    QRectF visibleSceneRect() const;

    void zoomBy(double factor, QPoint anchorViewPos);
    void zoomIn();
    void zoomOut();
    void fitContents();
    void toggleGrid();
    double zoom() const { return transform().m11(); }
    void applyTheme();

    void setPlacing(bool on);
    // Simulation mode: clicks and keys go to parts (buttons, switches), never
    // select or edit; navigation still works.
    void setSimMode(bool on);
    bool simMode() const { return m_simMode; }
    bool drawingWire() const { return m_drawing; }
    void cancelWire();
    void setWireColorProvider(std::function<QString(const QString& pinRef)> f) { m_colorFor = std::move(f); }
    // DRC: centre and zoom on `sceneBox` (100 %..400 %), ring `pins` and
    // outline `wires` (indices), pulsing for about a second; the highlight
    // stays until the selection changes.
    void showHighlight(const QRectF& sceneBox, const std::vector<QPointF>& pins, const std::vector<int>& wires);
    void clearHighlight();
    bool hasHighlight() const { return m_hlActive; }
    // Part + pin under a viewport position (pin hit region), if any.
    std::pair<PartItem*, const chiply::PinDef*> pinAt(QPoint viewPos) const;

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
    // Visible scene rectangle (inset by kHandleMargin) after scroll/zoom/resize.
    void visibleRectChanged(QRectF sceneRect);
    void selectionEdited();
    // A wire segment drag finished with a new route (index into doc.wires).
    void wireRouteEdited(int wireIndex, std::vector<chiply::Point> route);
    // Dragging selected parts. `grid` is the snap step (0 = none).
    void moveStarted(QString grabbedPartId, bool duplicate); // duplicate: Alt/Option held at press
    void moveUpdated(QPointF sceneDelta, double grid);
    void moveEnded(bool commit);
    // Placing a new part.
    void placeMoved(QPointF scenePos);
    void placeClicked(QPointF scenePos);
    void placeCancelled();
    void addPartRequested();
    void editPartRequested(); // F2 or double-click on a part
    // Simulation mode: press/release on a part (local, unrotated coords).
    void simPress(QString partId, QPointF local);
    void simRelease();
    void simKey(QString text, bool pressed);
    void wireColorRequested(QString color);   // color key with wires selected
    // A wire end was dropped on another pin; `route` ends there.
    void wireReanchored(int wireIndex, bool atStart, QString pinRef, std::vector<chiply::Point> route);
    void deleteWireRequested(int wireIndex);  // double-click on a wire
    // A new wire was drawn from pin to pin along `route` (scene points).
    void wireDrawn(QString fromRef, QString toRef, QString color, std::vector<chiply::Point> route);
    // Keyboard edits on the selection.
    void nudgeRequested(int gridX, int gridY, bool autoRepeat);
    void rotateRequested();
    void deleteRequested();
    void duplicateRequested();
    // The cursor's position in scene coordinates (status bar).
    void cursorMoved(QPointF scenePos);
    // Right-click on a pin (pinRef) or a wire (wireIndex): the Probe menu.
    void probeMenuRequested(QString pinRef, int wireIndex, QPoint globalPos);

protected:
    void contextMenuEvent(QContextMenuEvent* event) override;
    void drawBackground(QPainter* painter, const QRectF& rect) override;
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void drawForeground(QPainter* painter, const QRectF& rect) override;
    void scrollContentsBy(int dx, int dy) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void panBy(QPointF viewDelta);
    void clickAt(QPoint viewPos, Qt::KeyboardModifiers mods);
    void finishMarquee(Qt::KeyboardModifiers mods);
    void autoScrollTick();
    QRectF marqueeSceneRect() const;

    bool m_showGrid = true;
    bool m_spaceHeld = false;
    bool m_panning = false;
    bool m_panPending = false; // press on empty canvas: pan if it moves, click if not
    QPoint m_pressPos;
    QPoint m_lastPanPos;

    void updateHandleDrag(QPoint viewPos, Qt::KeyboardModifiers mods);

    enum class Press { None, Item, Empty, Marquee, Handle, Moving, Corner, Split, End };
    bool m_dragAtStart = true;
    std::size_t m_dragCorner = 0;
    QGraphicsItem* m_pressItem = nullptr;
    PartItem* m_pendingWirePart = nullptr; // junction pressed: wire on click, move on drag
    const chiply::PinDef* m_pendingWirePin = nullptr;
    bool m_placing = false;
    bool m_simMode = false;
    bool m_simPressed = false;

    // Wire drawing (PLAN.md 4.6).
    void startWire(PartItem* part, const chiply::PinDef* pin);
    void updateWirePreview(QPoint viewPos, Qt::KeyboardModifiers mods);
    void addWirePoint(QPoint viewPos, Qt::KeyboardModifiers mods);
    bool finishWireAt(QPoint viewPos); // true if it ended on a pin
    chiply::Point snapped(QPointF scene, Qt::KeyboardModifiers mods) const;
    std::vector<chiply::Point> legTo(chiply::Point target) const; // L-bend from the last point
    bool m_drawing = false;
    bool m_eatContextMenu = false;
    bool m_endMoved = false; // an end-handle press became a drag
    // While dragging a wire end or drawing a wire: the pin under the cursor
    // is named in a tooltip, as confirmation it is targeted.
    void showTargetPin(QPoint viewPos);
    void hideTargetPin();
    QString m_targetTip;
public:
    QString targetPinTip() const { return m_targetTip; } // for tests
private:
    bool m_hlActive = false;
    QRectF m_hlBox;
    std::vector<QPointF> m_hlPins;
    std::vector<int> m_hlWires;
    QElapsedTimer m_hlClock;
    QTimer m_hlPulse; // the right press already cancelled something
    QString m_drawFrom;              // "part:PIN"
    QString m_drawColor;
    std::vector<chiply::Point> m_drawPts; // committed points, first = source pin
    chiply::Point m_drawCursor;
    QGraphicsPathItem* m_drawPreview = nullptr;
    bool m_drawPressMoved = false;   // press-drag-release from the source pin
    std::function<QString(const QString&)> m_colorFor;
    bool hasSelectedParts() const;
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
