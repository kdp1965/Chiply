#include "SchematicView.h"

#include <QContextMenuEvent>
#include <QToolTip>
#include <utility>

#include "SchematicItems.h"
#include "Theme.h"
#include "core/JsonFormat.h"

#include <QCursor>
#include <QGraphicsPathItem>
#include <QKeyEvent>
#include <QSignalBlocker>
#include <QPainter>
#include <QScrollBar>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace {
constexpr double kMinZoom = 0.05;
constexpr double kMaxZoom = 8.0;
} // namespace

SchematicView::SchematicView(QGraphicsScene* scene, QWidget* parent)
    : QGraphicsView(scene, parent)
{
    setRenderHint(QPainter::Antialiasing);
    setDragMode(QGraphicsView::NoDrag); // marquee is implemented here
    // A DRC highlight lasts until the user changes the selection.
    connect(this, &SchematicView::selectionEdited, this, &SchematicView::clearHighlight);
    setMouseTracking(true);
    m_autoScroll.setInterval(16);
    connect(&m_autoScroll, &QTimer::timeout, this, &SchematicView::autoScrollTick);
    connect(this, &SchematicView::selectionEdited, viewport(), qOverload<>(&QWidget::update));
    setTransformationAnchor(QGraphicsView::NoAnchor);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);
    setViewportUpdateMode(QGraphicsView::SmartViewportUpdate);
    setFocusPolicy(Qt::StrongFocus);
    applyTheme();
    connect(&Theme::instance(), &Theme::changed, this, &SchematicView::applyTheme);
}

void SchematicView::drawBackground(QPainter* painter, const QRectF& rect)
{
    painter->fillRect(rect, backgroundBrush());
    if (!m_showGrid)
        return;
    // Skip grid levels that would be denser than ~6 screen pixels.
    double step = kGrid;
    while (step * zoom() < 6.0)
        step *= 5.0;
    const double x0 = std::floor(rect.left() / step) * step;
    const double y0 = std::floor(rect.top() / step) * step;
    QPen pen(Theme::instance().canvas().gridDot);
    pen.setWidthF(std::max(1.5 / zoom(), 0.5));
    pen.setCapStyle(Qt::RoundCap);
    painter->setPen(pen);
    QVector<QPointF> dots;
    for (double x = x0; x <= rect.right(); x += step)
        for (double y = y0; y <= rect.bottom(); y += step)
            dots.append(QPointF(x, y));
    painter->drawPoints(dots.constData(), int(dots.size()));
}

void SchematicView::applyTheme()
{
    const CanvasColors& c = Theme::instance().canvas();
    setBackgroundBrush(c.background);
    // Rubber band follows the theme's selection color.
    QPalette p = palette();
    p.setColor(QPalette::Highlight, c.selection);
    setPalette(p);
    resetCachedContent();
    viewport()->update();
}

void SchematicView::zoomBy(double factor, QPoint anchorViewPos)
{
    const double target = std::clamp(zoom() * factor, kMinZoom, kMaxZoom);
    factor = target / zoom();
    if (factor == 1.0)
        return;
    const QPointF before = mapToScene(anchorViewPos);
    scale(factor, factor);
    const QPointF after = mapToScene(anchorViewPos);
    const QPointF d = after - before;
    translate(d.x(), d.y());
    // Keep the point under the cursor fixed even when scrollbars clamp.
    const QPointF drift = mapFromScene(before) - QPointF(anchorViewPos);
    panBy(drift);
    emit zoomChanged(zoom());
    emit visibleRectChanged(visibleSceneRect());
}

void SchematicView::zoomIn() { zoomBy(1.25, viewport()->rect().center()); }
void SchematicView::zoomOut() { zoomBy(0.8, viewport()->rect().center()); }

void SchematicView::showHighlight(const QRectF& box, const std::vector<QPointF>& pins, const std::vector<int>& wires)
{
    m_hlActive = true;
    m_hlBox = box;
    m_hlPins = pins;
    m_hlWires = wires;
    // Zoom so the box fills the view with a margin, between 100 % and 400 %.
    const QRectF r = box.adjusted(-40, -40, 40, 40);
    const QSize vs = viewport()->size();
    double z = std::min(vs.width() / std::max(1.0, r.width()), vs.height() / std::max(1.0, r.height()));
    z = std::clamp(z, 1.0, 4.0);
    setTransform(QTransform::fromScale(z, z));
    centerOn(box.center());
    emit zoomChanged(zoom());
    emit visibleRectChanged(visibleSceneRect());
    m_hlClock.restart();
    if (!m_hlPulse.isActive()) {
        m_hlPulse.setInterval(40);
        connect(&m_hlPulse, &QTimer::timeout, this, [this] {
            if (m_hlClock.elapsed() > 1200)
                m_hlPulse.stop();
            viewport()->update();
        }, Qt::UniqueConnection);
        m_hlPulse.start();
    }
    viewport()->update();
}

void SchematicView::clearHighlight()
{
    if (!m_hlActive)
        return;
    m_hlActive = false;
    m_hlPins.clear();
    m_hlWires.clear();
    m_hlPulse.stop();
    viewport()->update();
}

void SchematicView::fitContents()
{
    QRectF r = scene()->itemsBoundingRect();
    if (r.isEmpty())
        r = QRectF(-200, -200, 400, 400);
    fitInView(r.adjusted(-20, -20, 20, 20), Qt::KeepAspectRatio);
    emit zoomChanged(zoom());
    emit visibleRectChanged(visibleSceneRect());
}

void SchematicView::toggleGrid()
{
    m_showGrid = !m_showGrid;
    resetCachedContent();
    viewport()->update();
}

void SchematicView::panBy(QPointF viewDelta)
{
    horizontalScrollBar()->setValue(horizontalScrollBar()->value() + int(std::lround(viewDelta.x())));
    verticalScrollBar()->setValue(verticalScrollBar()->value() + int(std::lround(viewDelta.y())));
}

void SchematicView::wheelEvent(QWheelEvent* event)
{
    const QPoint pixel = event->pixelDelta();
    const QPoint angle = event->angleDelta();
    const Qt::KeyboardModifiers mods = event->modifiers();

    if (mods & Qt::ShiftModifier) {
        // Some platforms already swap the axis for Shift+wheel.
        int d = angle.x() != 0 ? angle.x() : angle.y();
        if (!pixel.isNull())
            d = pixel.x() != 0 ? pixel.x() : pixel.y();
        else
            d = d / 2;
        panBy(QPointF(-d, 0));
    } else if (mods & Qt::ControlModifier) {
        int d = pixel.isNull() ? angle.y() / 2 : pixel.y();
        panBy(QPointF(0, -d));
    } else {
        const double steps = angle.y() / 120.0;
        zoomBy(std::pow(1.2, steps), event->position().toPoint());
    }
    event->accept();
}

QGraphicsItem* SchematicView::selectableAt(QPoint viewPos) const
{
    for (QGraphicsItem* it : items(viewPos))
        if (it->flags() & QGraphicsItem::ItemIsSelectable)
            return it;
    return nullptr;
}

QString wokwiColorForKey(int key)
{
    switch (key) {
    case Qt::Key_0: return "black";
    case Qt::Key_1: return "brown";
    case Qt::Key_2: return "red";
    case Qt::Key_3: return "orange";
    case Qt::Key_4: return "gold";
    case Qt::Key_5: return "green";
    case Qt::Key_6: return "blue";
    case Qt::Key_7: return "violet";
    case Qt::Key_8: return "gray";
    case Qt::Key_9: return "white";
    case Qt::Key_C: return "cyan";
    case Qt::Key_L: return "limegreen";
    case Qt::Key_M: return "magenta";
    case Qt::Key_P: return "purple";
    case Qt::Key_Y: return "yellow";
    default: return {};
    }
}

std::pair<PartItem*, const chiply::PinDef*> SchematicView::pinAt(QPoint viewPos) const
{
    const QPointF sp = mapToScene(viewPos);
    for (QGraphicsItem* it : items(viewPos)) {
        if (it->type() != PartItem::Type)
            continue;
        auto* p = static_cast<PartItem*>(it);
        if (const chiply::PinDef* pin = p->pinAtScene(sp))
            return {p, pin};
    }
    return {nullptr, nullptr};
}

chiply::Point SchematicView::snapped(QPointF sp, Qt::KeyboardModifiers mods) const
{
    if (mods & Qt::ControlModifier)
        return {chiply::round2(sp.x()), chiply::round2(sp.y())};
    const double g = (mods & Qt::AltModifier) ? kGrid / 2 : kGrid;
    return {chiply::round2(std::round(sp.x() / g) * g), chiply::round2(std::round(sp.y() / g) * g)};
}

std::vector<chiply::Point> SchematicView::legTo(chiply::Point t) const
{
    // L-bend from the last committed point; horizontal first when the
    // target is more to the side than above/below (flips at the diagonal).
    const chiply::Point l = m_drawPts.back();
    if (std::fabs(t.x - l.x) < 0.005 || std::fabs(t.y - l.y) < 0.005)
        return {t};
    if (std::fabs(t.x - l.x) >= std::fabs(t.y - l.y))
        return {{t.x, l.y}, t};
    return {{l.x, t.y}, t};
}

void SchematicView::startWire(PartItem* part, const chiply::PinDef* pin)
{
    cancelWire();
    const QPointF p = part->pinScenePos(*pin);
    m_drawing = true;
    m_drawFrom = QString::fromStdString(part->partId() + ":" + pin->name);
    m_drawColor = m_colorFor ? m_colorFor(m_drawFrom) : QStringLiteral("green");
    m_drawPts = {{chiply::round2(p.x()), chiply::round2(p.y())}};
    m_drawCursor = m_drawPts.back();
    m_drawPreview = new QGraphicsPathItem;
    m_drawPreview->setZValue(30);
    scene()->addItem(m_drawPreview);
    viewport()->setCursor(Qt::CrossCursor);
    if (!scene()->selectedItems().isEmpty())
        clearSelection();
}

void SchematicView::cancelWire()
{
    hideTargetPin();
    if (m_drawPreview) {
        scene()->removeItem(m_drawPreview);
        delete m_drawPreview;
        m_drawPreview = nullptr;
    }
    m_drawing = false;
    m_drawPts.clear();
    viewport()->setCursor(Qt::ArrowCursor);
}

void SchematicView::updateWirePreview(QPoint viewPos, Qt::KeyboardModifiers mods)
{
    if (!m_drawing)
        return;
    // Snap to a pin under the cursor, else to the grid.
    auto [part, pin] = pinAt(viewPos);
    if (part && pin) {
        const QPointF pp = part->pinScenePos(*pin);
        m_drawCursor = {chiply::round2(pp.x()), chiply::round2(pp.y())};
    } else {
        m_drawCursor = snapped(mapToScene(viewPos), mods);
    }
    std::vector<chiply::Point> pts = m_drawPts;
    for (const chiply::Point& q : legTo(m_drawCursor))
        pts.push_back(q);
    QPainterPath path;
    path.moveTo(pts[0].x, pts[0].y);
    for (std::size_t i = 1; i < pts.size(); ++i)
        path.lineTo(pts[i].x, pts[i].y);
    m_drawPreview->setPath(path);
    QColor c(m_drawColor);
    m_drawPreview->setPen(QPen(Theme::instance().canvas().displayWireColor(c.isValid() ? c : QColor("green")), 2.0,
                               Qt::DashLine, Qt::RoundCap, Qt::RoundJoin));
}

void SchematicView::addWirePoint(QPoint viewPos, Qt::KeyboardModifiers mods)
{
    updateWirePreview(viewPos, mods);
    for (const chiply::Point& q : legTo(m_drawCursor))
        m_drawPts.push_back(q);
    m_drawPts = chiply::simplifyPolyline(m_drawPts);
    updateWirePreview(viewPos, mods);
}

bool SchematicView::finishWireAt(QPoint viewPos)
{
    auto [part, pin] = pinAt(viewPos);
    if (!part || !pin)
        return false;
    const QString to = QString::fromStdString(part->partId() + ":" + pin->name);
    if (to == m_drawFrom)
        return true; // clicked the start pin again: keep drawing
    const QPointF pp = part->pinScenePos(*pin);
    const chiply::Point end{chiply::round2(pp.x()), chiply::round2(pp.y())};
    std::vector<chiply::Point> pts = m_drawPts;
    for (const chiply::Point& q : legTo(end))
        pts.push_back(q);
    const QString from = m_drawFrom, color = m_drawColor;
    cancelWire();
    emit wireDrawn(from, to, color, chiply::simplifyPolyline(pts));
    return true;
}

QRectF SchematicView::visibleSceneRect() const
{
    const QRect r = viewport()->rect().adjusted(kHandleMargin, kHandleMargin, -kHandleMargin, -kHandleMargin);
    return mapToScene(r).boundingRect();
}

void SchematicView::scrollContentsBy(int dx, int dy)
{
    QGraphicsView::scrollContentsBy(dx, dy);
    emit visibleRectChanged(visibleSceneRect());
}

void SchematicView::resizeEvent(QResizeEvent* event)
{
    QGraphicsView::resizeEvent(event);
    emit visibleRectChanged(visibleSceneRect());
}

void SchematicView::setSimMode(bool on)
{
    m_simMode = on;
    m_press = Press::None;
    m_simPressed = false;
    if (on) {
        cancelWire();
        clearSelection();
    }
    viewport()->setCursor(on ? Qt::PointingHandCursor : Qt::ArrowCursor);
    viewport()->update();
}

void SchematicView::setPlacing(bool on)
{
    m_placing = on;
    viewport()->setCursor(on ? Qt::CrossCursor : Qt::ArrowCursor);
}

void SchematicView::mousePressEvent(QMouseEvent* event)
{
    const QPoint pos = event->position().toPoint();
    if (m_simMode && event->button() == Qt::LeftButton && !m_spaceHeld && !(event->modifiers() & Qt::ShiftModifier)) {
        // Simulation: the click goes to the part under the cursor.
        for (QGraphicsItem* it : items(pos)) {
            if (it->type() == PartItem::Type) {
                auto* p = static_cast<PartItem*>(it);
                m_simPressed = true;
                emit simPress(QString::fromStdString(p->partId()), p->mapFromScene(mapToScene(pos)));
                event->accept();
                return;
            }
        }
        // Not on a part: a drag pans, as in edit mode.
        m_panning = true;
        m_lastPanPos = pos;
        viewport()->setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }
    // Handles on a selected wire win over everything under them (pins).
    if (event->button() == Qt::LeftButton && !m_spaceHeld && !m_drawing) {
        for (QGraphicsItem* it : items(pos)) {
            if (it->type() == EndHandle::Type) {
                auto* h = static_cast<EndHandle*>(it);
                m_dragWire = static_cast<WireItem*>(h->parentItem());
                m_dragAtStart = h->atStart();
                m_dragRoute = chiply::simplifyPolyline(m_dragWire->route());
                m_dragResult = m_dragRoute;
                m_press = Press::End;
                m_endMoved = false;
                m_pressPos = pos;
                viewport()->setCursor(Qt::CrossCursor);
                event->accept();
                return;
            }
            if (it->type() == CornerHandle::Type) {
                auto* h = static_cast<CornerHandle*>(it);
                m_dragWire = static_cast<WireItem*>(h->parentItem());
                m_dragCorner = h->corner();
                m_dragRoute = chiply::simplifyPolyline(m_dragWire->route());
                m_dragResult = m_dragRoute;
                m_press = Press::Corner;
                m_pressPos = pos;
                event->accept();
                return;
            }
            if (it->type() != SegmentHandle::Type)
                continue;
            auto* h = static_cast<SegmentHandle*>(it);
            m_dragWire = static_cast<WireItem*>(h->parentItem());
            m_dragSegment = h->segment();
            m_dragHorizontal = h->horizontal();
            m_dragRoute = chiply::simplifyPolyline(m_dragWire->route());
            m_dragResult = m_dragRoute;
            m_press = Press::Handle;
            m_pressPos = pos;
            event->accept();
            return;
        }
    }
    if (event->button() == Qt::LeftButton && !m_spaceHeld && !m_drawing && (event->modifiers() & Qt::ShiftModifier)) {
        // Wokwi: Shift+drag is a marquee (even when it starts on a part);
        // Shift+click without moving toggles the item under the cursor.
        m_pressPos = pos;
        m_lastMousePos = pos;
        m_lastMods = event->modifiers();
        m_pressItem = selectableAt(pos);
        m_press = Press::Empty;
        m_marqueeStart = mapToScene(pos);
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton && !m_spaceHeld && m_drawing && (event->modifiers() & Qt::ShiftModifier)) {
        // While drawing a wire, Shift+drag pans.
        m_panPending = true;
        m_pressPos = pos;
        m_lastPanPos = pos;
        event->accept();
        return;
    }
    if (event->button() == Qt::MiddleButton || (event->button() == Qt::LeftButton && m_spaceHeld)) {
        m_panning = true;
        m_lastPanPos = pos;
        viewport()->setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }
    if (m_drawing) {
        if (event->button() == Qt::RightButton) {
            cancelWire();
            m_eatContextMenu = true;
        } else if (event->button() == Qt::LeftButton && !(event->modifiers() & Qt::ShiftModifier) && !m_spaceHeld) {
            if (!finishWireAt(pos))
                addWirePoint(pos, event->modifiers());
        } else {
            goto notDrawing; // Shift/Space/middle: pan while drawing
        }
        event->accept();
        return;
    }
notDrawing:
    if (event->button() == Qt::LeftButton && !m_placing && !m_spaceHeld
        && !(event->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier))) {
        // A press in a pin's hit region starts a wire; it never selects the part.
        auto [part, pin] = pinAt(pos);
        if (part && pin) {
            const auto* def = part->def();
            if (def && def->pins.size() == 1 && def->width <= 12 && def->height <= 12) {
                // A junction is all pin: a click starts a wire from it, a
                // drag moves it. Decide on release / first movement.
                m_pressPos = pos;
                m_lastMousePos = pos;
                m_lastMods = event->modifiers();
                m_pressItem = part;
                m_press = Press::Item;
                m_pendingWirePart = part;
                m_pendingWirePin = pin;
                event->accept();
                return;
            }
            startWire(part, pin);
            m_pressPos = pos;
            m_drawPressMoved = false;
            updateWirePreview(pos, event->modifiers());
            event->accept();
            return;
        }
    }
    if (m_placing) {
        if (event->button() == Qt::LeftButton)
            emit placeClicked(mapToScene(pos));
        else if (event->button() == Qt::RightButton) {
            m_eatContextMenu = true;
            emit placeCancelled();
        }
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton && (event->modifiers() & Qt::ControlModifier) && !m_drawing) {
        // Ctrl/Cmd+drag on a wire splits the segment there; a plain
        // Ctrl/Cmd+click (no drag) still toggles selection on release.
        QGraphicsItem* it = selectableAt(pos);
        if (it && it->type() == WireItem::Type) {
            m_dragWire = static_cast<WireItem*>(it);
            m_dragRoute = chiply::simplifyPolyline(m_dragWire->route());
            const QPointF sp = mapToScene(pos);
            m_dragSegment = chiply::nearestSegment(m_dragRoute, {sp.x(), sp.y()}).first;
            m_dragResult = m_dragRoute;
            m_press = Press::Split;
            m_pressPos = pos;
            m_pressItem = it;
            m_lastMods = event->modifiers();
            event->accept();
            return;
        }
    }
    if (event->button() == Qt::LeftButton) {
        m_pressPos = pos;
        m_lastMousePos = pos;
        m_lastMods = event->modifiers();
        m_pressItem = selectableAt(pos);
        if (!m_pressItem && !(event->modifiers() & Qt::ControlModifier)) {
            // Wokwi: dragging the empty canvas pans; a click without moving
            // clears the selection. Ctrl/Cmd+drag is an additive marquee.
            m_panPending = true;
            m_lastPanPos = pos;
            event->accept();
            return;
        }
        m_press = m_pressItem ? Press::Item : Press::Empty;
        m_marqueeStart = mapToScene(pos);
        event->accept();
        return;
    }
    QGraphicsView::mousePressEvent(event);
}

void SchematicView::showTargetPin(QPoint viewPos)
{
    auto [part, pin] = pinAt(viewPos);
    const QString tip = part && pin ? QString::fromStdString(part->partId() + ":" + pin->name) : QString();
    if (tip == m_targetTip)
        return;
    m_targetTip = tip;
    if (tip.isEmpty())
        QToolTip::hideText();
    else
        QToolTip::showText(viewport()->mapToGlobal(viewPos) + QPoint(16, 16), QStringLiteral("\u2192 ") + tip, viewport());
}

void SchematicView::hideTargetPin()
{
    if (!m_targetTip.isEmpty())
        QToolTip::hideText();
    m_targetTip.clear();
}

void SchematicView::mouseMoveEvent(QMouseEvent* event)
{
    const QPoint pos = event->position().toPoint();
    emit cursorMoved(mapToScene(pos));
    m_autoPos = pos;
    if (autoScrollActive() && !m_autoScroll.isActive())
        m_autoScroll.start();
    if (m_drawing || (m_press == Press::End && m_dragWire && m_endMoved))
        showTargetPin(pos);
    if (m_drawing && !m_panning && !m_panPending) {
        if ((pos - m_pressPos).manhattanLength() > 6)
            m_drawPressMoved = true;
        updateWirePreview(pos, event->modifiers());
        QGraphicsView::mouseMoveEvent(event); // pin hover markers
        event->accept();
        return;
    }
    if (m_placing && !m_panning) {
        emit placeMoved(mapToScene(pos));
        if (!m_panPending) {
            event->accept();
            return;
        }
    }
    if (m_press == Press::Handle) {
        updateHandleDrag(pos, event->modifiers());
        event->accept();
        return;
    }
    if (m_press == Press::End && m_dragWire) {
        // A click without a drag starts a new wire instead (on release).
        if (!m_endMoved && (pos - m_pressPos).manhattanLength() <= 4) {
            event->accept();
            return;
        }
        m_endMoved = true;
        auto [part, pin] = pinAt(pos);
        chiply::Point q;
        if (part && pin) {
            const QPointF pp = part->pinScenePos(*pin);
            q = {chiply::round2(pp.x()), chiply::round2(pp.y())};
        } else {
            q = snapped(mapToScene(pos), event->modifiers());
        }
        m_dragResult = chiply::stretchEnd(m_dragRoute, m_dragAtStart, q);
        m_dragWire->showPreview(m_dragResult);
        QGraphicsView::mouseMoveEvent(event); // pin hover markers
        event->accept();
        return;
    }
    if ((m_press == Press::Corner || m_press == Press::Split) && m_dragWire) {
        if (m_press == Press::Split && (pos - m_pressPos).manhattanLength() <= 4) {
            event->accept();
            return;
        }
        // Snap like everything else; Ctrl/Cmd is the split modifier here,
        // so only Alt changes the step.
        auto mods = event->modifiers() & ~Qt::KeyboardModifiers(Qt::ControlModifier);
        const chiply::Point q = snapped(mapToScene(pos), mods);
        m_dragResult = (m_press == Press::Corner) ? chiply::moveCorner(m_dragRoute, m_dragCorner, q)
                                                  : chiply::splitSegment(m_dragRoute, m_dragSegment, q);
        m_dragWire->showPreview(m_dragResult);
        event->accept();
        return;
    }
    if (m_panPending && (pos - m_pressPos).manhattanLength() > 4) {
        m_panPending = false;
        m_panning = true;
        viewport()->setCursor(Qt::ClosedHandCursor);
    }
    if (m_panPending) {
        event->accept();
        return;
    }
    if (m_panning) {
        panBy(QPointF(m_lastPanPos - pos));
        m_lastPanPos = pos;
        event->accept();
        return;
    }
    if (m_press == Press::Item && (pos - m_pressPos).manhattanLength() > 4 && m_pressItem
        && m_pressItem->type() != WireItem::Type) {
        // Dragging a part moves the selection; an unselected part becomes
        // the selection first (or joins it with Ctrl/Cmd).
        if (!m_pressItem->isSelected()) {
            if (m_lastMods & Qt::ControlModifier)
                toggleSelected(m_pressItem);
            else
                selectOnly(m_pressItem);
        }
        m_press = Press::Moving;
        m_moveStartScene = mapToScene(m_pressPos); // in scene units: auto-scroll does not shift it
        m_pendingWirePart = nullptr; // a junction drag is a move, not a wire
        m_pendingWirePin = nullptr;
        viewport()->setCursor(Qt::SizeAllCursor);
        emit moveStarted(QString::fromStdString(itemPartId(m_pressItem)), m_lastMods & Qt::AltModifier);
    }
    if (m_press == Press::Moving) {
        const QPointF d = mapToScene(pos) - m_moveStartScene;
        const auto mods = event->modifiers();
        const double grid = (mods & Qt::ControlModifier) ? 0.0 : (mods & Qt::AltModifier) ? kGrid / 2 : kGrid;
        emit moveUpdated(d, grid);
        event->accept();
        return;
    }
    if (m_press == Press::Empty && (pos - m_pressPos).manhattanLength() > 3) {
        m_press = Press::Marquee;
        m_autoScroll.start();
    }
    if (m_press == Press::Marquee) {
        m_lastMousePos = pos;
        m_lastMods = event->modifiers();
        viewport()->update();
        event->accept();
        return;
    }
    // Item drags become moves in M4; until then a drag on an item does nothing.
    QGraphicsView::mouseMoveEvent(event); // hover
}

void SchematicView::mouseReleaseEvent(QMouseEvent* event)
{
    const QPoint pos = event->position().toPoint();
    if (m_simMode && event->button() == Qt::LeftButton && m_simPressed) {
        m_simPressed = false;
        emit simRelease();
        event->accept();
        return;
    }
    if (m_drawing && event->button() == Qt::LeftButton && !m_panning && !m_panPending) {
        // Press on a pin, drag, release on another pin: finish there too.
        if (m_drawPressMoved && m_drawPts.size() == 1)
            finishWireAt(pos);
        m_drawPressMoved = false;
        event->accept();
        return;
    }
    if (m_panPending && event->button() == Qt::LeftButton) {
        m_panPending = false;
        clickAt(m_pressPos, event->modifiers());
        event->accept();
        return;
    }
    if (m_panning && (event->button() == Qt::MiddleButton || event->button() == Qt::LeftButton)) {
        m_panning = false;
        viewport()->setCursor(m_spaceHeld ? Qt::OpenHandCursor : Qt::ArrowCursor);
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton && m_press == Press::End) {
        hideTargetPin();
        m_press = Press::None;
        viewport()->setCursor(Qt::ArrowCursor);
        WireItem* w = m_dragWire;
        m_dragWire = nullptr;
        if (w && !m_endMoved) {
            // Click on an end handle without dragging: deselect the wire and
            // start a new wire from the pin at that end (e.g. a second
            // connection right after making the first).
            const std::vector<chiply::Point>& route = w->route();
            const chiply::Point endPt = m_dragAtStart ? route.front() : route.back();
            auto [endPart, endPin] = pinAt(mapFromScene(QPointF(endPt.x, endPt.y)));
            clearSelection();
            if (endPart && endPin) {
                startWire(endPart, endPin);
                m_pressPos = pos;
                m_drawPressMoved = false;
                updateWirePreview(pos, event->modifiers());
            }
            event->accept();
            return;
        }
        auto [part, pin] = pinAt(pos);
        if (w && part && pin) {
            const QString ref = QString::fromStdString(part->partId() + ":" + pin->name);
            const QPointF pp = part->pinScenePos(*pin);
            const chiply::Point q{chiply::round2(pp.x()), chiply::round2(pp.y())};
            const auto r = chiply::stretchEnd(m_dragRoute, m_dragAtStart, q);
            emit wireReanchored(w->index(), m_dragAtStart, ref, r);
        } else if (w) {
            w->showPreview(w->route()); // dropped on nothing: back to where it was
        }
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton && (m_press == Press::Corner || m_press == Press::Split)) {
        const Press p = m_press;
        m_press = Press::None;
        WireItem* w = m_dragWire;
        m_dragWire = nullptr;
        if (w && m_dragResult != m_dragRoute) {
            emit wireRouteEdited(w->index(), m_dragResult);
        } else if (w) {
            w->showPreview(w->route());
            if (p == Press::Split)
                toggleSelected(w); // it was just a Ctrl/Cmd+click
        }
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton && m_press == Press::Handle) {
        m_press = Press::None;
        WireItem* w = m_dragWire;
        m_dragWire = nullptr;
        if (w && m_dragResult != m_dragRoute)
            emit wireRouteEdited(w->index(), m_dragResult);
        else if (w)
            w->showPreview(w->route());
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton && m_press == Press::Moving) {
        m_press = Press::None;
        m_pressItem = nullptr;
        viewport()->setCursor(Qt::ArrowCursor);
        emit moveEnded(true);
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton && m_press == Press::Item && m_pendingWirePart) {
        // Junction clicked without dragging: start a wire from it.
        PartItem* part = m_pendingWirePart;
        const chiply::PinDef* pin = m_pendingWirePin;
        m_pendingWirePart = nullptr;
        m_pendingWirePin = nullptr;
        m_press = Press::None;
        m_pressItem = nullptr;
        startWire(part, pin);
        m_drawPressMoved = false;
        updateWirePreview(pos, event->modifiers());
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton && m_press != Press::None) {
        const Press p = m_press;
        m_press = Press::None;
        if (p == Press::Marquee) {
            m_autoScroll.stop();
            m_lastMousePos = pos;
            finishMarquee(event->modifiers());
        } else {
            clickAt(m_pressPos, event->modifiers());
        }
        viewport()->update();
        event->accept();
        return;
    }
    QGraphicsView::mouseReleaseEvent(event);
}

void SchematicView::updateHandleDrag(QPoint viewPos, Qt::KeyboardModifiers mods)
{
    if (!m_dragWire)
        return;
    const QPointF sp = mapToScene(viewPos);
    double coord = m_dragHorizontal ? sp.y() : sp.x();
    // Snap the segment to the 0.1 inch grid; Alt = half grid; Ctrl/Cmd = free.
    if (!(mods & Qt::ControlModifier)) {
        const double g = (mods & Qt::AltModifier) ? kGrid / 2 : kGrid;
        coord = std::round(coord / g) * g;
    }
    coord = chiply::round2(coord);
    m_dragResult = chiply::moveSegment(m_dragRoute, m_dragSegment, coord);
    m_dragWire->showPreview(m_dragResult);
}

void SchematicView::contextMenuEvent(QContextMenuEvent* event)
{
    if (std::exchange(m_eatContextMenu, false) || m_drawing || m_placing) {
        event->accept();
        return;
    }
    const QPoint pos = event->pos();
    auto [part, pin] = pinAt(pos);
    if (part && pin && !(part->def() && part->def()->pins.size() == 1 && part->def()->width <= 12)) {
        emit probeMenuRequested(QString::fromStdString(part->partId() + ":" + pin->name), -1, event->globalPos());
    } else if (QGraphicsItem* it = selectableAt(pos); it && it->type() == WireItem::Type) {
        emit probeMenuRequested({}, static_cast<WireItem*>(it)->index(), event->globalPos());
    } else if (part && pin) {
        // A junction: probe the wires meeting there through its pin.
        emit probeMenuRequested(QString::fromStdString(part->partId() + ":" + pin->name), -1, event->globalPos());
    }
    event->accept();
}

void SchematicView::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (m_simMode) {
        mousePressEvent(event); // a quick second click is just another click
        return;
    }
    QGraphicsItem* it = selectableAt(event->position().toPoint());
    if (event->button() == Qt::LeftButton && it && it->type() == WireItem::Type && !m_drawing) {
        // Wokwi: double-click deletes a wire.
        emit deleteWireRequested(static_cast<WireItem*>(it)->index());
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton && it && !itemPartId(it).empty()) {
        selectOnly(it);
        emit editPartRequested();
        event->accept();
        return;
    }
    event->accept();
}

void SchematicView::clickAt(QPoint viewPos, Qt::KeyboardModifiers mods)
{
    QGraphicsItem* item = selectableAt(viewPos);
    const bool toggle = mods & (Qt::ShiftModifier | Qt::ControlModifier);
    if (!item) {
        if (!toggle)
            clearSelection();
        return;
    }
    if (toggle)
        toggleSelected(item);
    else
        selectOnly(item);
}

QRectF SchematicView::marqueeSceneRect() const
{
    return QRectF(m_marqueeStart, mapToScene(m_lastMousePos)).normalized();
}

void SchematicView::finishMarquee(Qt::KeyboardModifiers mods)
{
    selectInRect(marqueeSceneRect(), mods & Qt::AltModifier, mods & Qt::ControlModifier);
}

bool SchematicView::autoScrollActive() const
{
    // Carrying something across the canvas: the view follows the cursor
    // past the edges.
    return m_press == Press::Marquee || m_press == Press::Moving || (m_press == Press::End && m_endMoved)
        || m_press == Press::Handle || m_press == Press::Corner || m_press == Press::Split || m_drawing || m_placing;
}

void SchematicView::autoScrollTick()
{
    if (!autoScrollActive()) {
        m_autoScroll.stop();
        return;
    }
    // Scroll when the cursor is near or past an edge, faster the further out.
    const QRect r = viewport()->rect();
    constexpr int kEdge = 30;
    auto speed = [](int d) { return d >= kEdge ? 0.0 : std::min(40.0, (kEdge - d) * 0.6); };
    const QPoint p = m_autoPos;
    QPointF d(0, 0);
    d.rx() -= speed(p.x() - r.left());
    d.rx() += speed(r.right() - p.x());
    d.ry() -= speed(p.y() - r.top());
    d.ry() += speed(r.bottom() - p.y());
    if (d.isNull())
        return;
    panBy(d);
    // Replay the cursor at its place: what is being moved, drawn or
    // stretched follows the scroll.
    QMouseEvent move(QEvent::MouseMove, QPointF(p), QPointF(viewport()->mapToGlobal(p)), Qt::NoButton,
                     QGuiApplication::mouseButtons(), QGuiApplication::keyboardModifiers());
    mouseMoveEvent(&move);
    viewport()->update();
}

namespace {
QRectF selectionRect(const QGraphicsItem* it)
{
    if (it->type() == PartItem::Type)
        return static_cast<const PartItem*>(it)->outlineSceneRect();
    if (it->type() == WireItem::Type)
        return static_cast<const WireItem*>(it)->path().boundingRect();
    return it->sceneBoundingRect();
}
} // namespace

void SchematicView::selectInRect(const QRectF& r, bool crossing, bool add)
{
    {
        const QSignalBlocker block(scene());
        if (!add)
            scene()->clearSelection();
        const QList<QGraphicsItem*> hits = scene()->items(r, Qt::IntersectsItemShape);
        for (QGraphicsItem* it : hits) {
            if (!(it->flags() & QGraphicsItem::ItemIsSelectable))
                continue;
            if (crossing || r.contains(selectionRect(it)))
                it->setSelected(true);
        }
    }
    // Wires not selected as a whole: the segments the rectangle fully
    // encloses move with the selection (their corners; ends stay on pins).
    std::map<int, std::vector<chiply::Point>> corners;
    const QRectF rr = r.adjusted(-0.01, -0.01, 0.01, 0.01);
    for (QGraphicsItem* it : scene()->items(r, Qt::IntersectsItemShape)) {
        if (it->type() != WireItem::Type || it->isSelected())
            continue;
        auto* w = static_cast<WireItem*>(it);
        const std::vector<chiply::Point>& route = w->route();
        std::vector<chiply::Point> pts;
        for (std::size_t k = 0; k + 1 < route.size(); ++k) {
            const QPointF a(route[k].x, route[k].y), b(route[k + 1].x, route[k + 1].y);
            if (!rr.contains(a) || !rr.contains(b))
                continue;
            for (std::size_t m : {k, k + 1})
                if (m != 0 && m + 1 != route.size())
                    pts.push_back(route[m]);
        }
        if (!pts.empty())
            corners[w->index()] = pts;
    }
    emit segmentsSelected(corners, add);
    emit selectionEdited();
}

void SchematicView::clearSelection()
{
    {
        const QSignalBlocker block(scene());
        scene()->clearSelection();
    }
    emit segmentsCleared();
    emit selectionEdited();
}

void SchematicView::selectAll()
{
    {
        const QSignalBlocker block(scene());
        for (QGraphicsItem* it : scene()->items())
            if (it->flags() & QGraphicsItem::ItemIsSelectable)
                it->setSelected(true);
    }
    emit selectionEdited();
}

void SchematicView::selectOnly(QGraphicsItem* item)
{
    if (!item) {
        clearSelection();
        return;
    }
    {
        const QSignalBlocker block(scene());
        scene()->clearSelection();
        item->setSelected(true);
    }
    emit segmentsCleared();
    emit selectionEdited();
}

void SchematicView::toggleSelected(QGraphicsItem* item)
{
    if (!item)
        return;
    {
        const QSignalBlocker block(scene());
        item->setSelected(!item->isSelected());
    }
    emit selectionEdited();
}

void SchematicView::drawForeground(QPainter* painter, const QRectF&)
{
    const CanvasColors& c = Theme::instance().canvas();
    painter->save();
    painter->resetTransform(); // draw in viewport pixels
    if (m_press == Press::Marquee) {
        const QRect r = QRect(mapFromScene(m_marqueeStart), m_lastMousePos).normalized();
        QColor fill = c.selection;
        fill.setAlpha(40);
        QPen pen(c.selection, 1, (m_lastMods & Qt::AltModifier) ? Qt::DashLine : Qt::SolidLine);
        painter->setPen(pen);
        painter->setBrush(fill);
        painter->drawRect(r);
    }
    if (m_hlActive) {
        // DRC highlight: wires of the net, rings on the pins; pulses for a
        // second, then stays.
        const double t = m_hlClock.elapsed() / 1000.0;
        const double pulse = t < 1.2 ? 0.5 + 0.5 * std::cos(t * 2 * 3.14159265358979 * 2.5) : 1.0;
        QColor ring(0xff, 0x6d, 0x00); // orange: stands out from every wire colour
        QColor halo = ring;
        halo.setAlphaF(0.35 + 0.4 * pulse);
        for (QGraphicsItem* it : scene()->items())
            if (it->type() == WireItem::Type) {
                auto* w = static_cast<WireItem*>(it);
                if (std::find(m_hlWires.begin(), m_hlWires.end(), w->index()) == m_hlWires.end())
                    continue;
                QPolygonF poly;
                for (const chiply::Point& p : w->route())
                    poly << mapFromScene(QPointF(p.x, p.y));
                painter->setPen(QPen(halo, 9, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                painter->setBrush(Qt::NoBrush);
                painter->drawPolyline(poly);
            }
        const double rr = 9 + 5 * pulse;
        painter->setBrush(Qt::NoBrush);
        for (const QPointF& p : m_hlPins) {
            const QPointF vp = mapFromScene(p);
            painter->setPen(QPen(Qt::black, 5));
            painter->drawEllipse(vp, rr, rr);
            painter->setPen(QPen(ring, 3));
            painter->drawEllipse(vp, rr, rr);
        }
        if (m_hlPins.empty()) {
            const QRectF vb = QRectF(mapFromScene(m_hlBox).boundingRect()).adjusted(-8 - 4 * pulse, -8 - 4 * pulse, 8 + 4 * pulse, 8 + 4 * pulse);
            painter->setPen(QPen(ring, 3, Qt::DashLine));
            painter->drawRoundedRect(vb, 6, 6);
        }
    }
    // Group box and count for multi-selections.
    const QList<QGraphicsItem*> sel = scene()->selectedItems();
    if (sel.size() > 1) {
        QRectF box;
        for (const QGraphicsItem* it : sel)
            box = box.united(selectionRect(it));
        const QRect vb = mapFromScene(box).boundingRect().adjusted(-6, -6, 6, 6);
        QPen pen(c.selection, 1, Qt::DotLine);
        painter->setPen(pen);
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(vb);
        QFont f = font();
        f.setPointSize(Theme::instance().hoverTextSize());
        painter->setFont(f);
        const QString label = tr("%1 selected").arg(sel.size());
        const QRect tr_ = painter->fontMetrics().boundingRect(label).adjusted(-6, -3, 6, 3);
        QRect lr(vb.left(), vb.top() - tr_.height() - 2, tr_.width(), tr_.height());
        QColor bg = c.selection;
        painter->setPen(Qt::NoPen);
        painter->setBrush(bg);
        painter->drawRoundedRect(lr, 3, 3);
        painter->setPen(Qt::white);
        painter->drawText(lr, Qt::AlignCenter, label);
    }
    painter->restore();
}

bool SchematicView::hasSelectedParts() const
{
    for (const QGraphicsItem* it : scene()->selectedItems())
        if (!itemPartId(it).empty())
            return true;
    return false;
}

void SchematicView::keyPressEvent(QKeyEvent* event)
{
    if (m_simMode) {
        // Letters press pushbuttons by their "key"; navigation keys still work.
        const bool nav = event->key() == Qt::Key_Left || event->key() == Qt::Key_Right || event->key() == Qt::Key_Up
            || event->key() == Qt::Key_Down || event->key() == Qt::Key_Plus || event->key() == Qt::Key_Equal
            || event->key() == Qt::Key_Minus || event->key() == Qt::Key_F || event->key() == Qt::Key_G
            || event->key() == Qt::Key_Space;
        if (!event->isAutoRepeat() && !(event->modifiers() & (Qt::ControlModifier | Qt::MetaModifier))
            && !event->text().isEmpty()) {
            emit simKey(event->text(), true);
        }
        if (!nav) {
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_Left || event->key() == Qt::Key_Right || event->key() == Qt::Key_Up
            || event->key() == Qt::Key_Down) {
            const QSize vp = viewport()->size();
            const bool big = event->modifiers() & Qt::ShiftModifier;
            const double fx = big ? vp.width() : vp.width() / 10.0, fy = big ? vp.height() : vp.height() / 10.0;
            if (event->key() == Qt::Key_Left) panBy(QPointF(fx, 0));
            if (event->key() == Qt::Key_Right) panBy(QPointF(-fx, 0));
            if (event->key() == Qt::Key_Up) panBy(QPointF(0, fy));
            if (event->key() == Qt::Key_Down) panBy(QPointF(0, -fy));
            return;
        }
    }
    if (m_drawing) {
        const QString c = wokwiColorForKey(event->key());
        if (!c.isEmpty() && !(event->modifiers() & (Qt::ControlModifier | Qt::AltModifier))) {
            m_drawColor = c;
            updateWirePreview(viewport()->mapFromGlobal(QCursor::pos()), event->modifiers());
            return;
        }
        if (event->key() == Qt::Key_Escape) {
            cancelWire();
            return;
        }
        if (event->key() == Qt::Key_Backspace || event->key() == Qt::Key_Delete) {
            if (m_drawPts.size() > 1)
                m_drawPts.pop_back();
            else
                cancelWire();
            if (m_drawing)
                updateWirePreview(viewport()->mapFromGlobal(QCursor::pos()), event->modifiers());
            return;
        }
    }
    // Wokwi color keys recolor the selected wires.
    if (!(event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))) {
        const QString color = wokwiColorForKey(event->key());
        if (!color.isEmpty()) {
            bool wires = false;
            for (const QGraphicsItem* it : scene()->selectedItems())
                wires |= it->type() == WireItem::Type;
            if (wires) {
                emit wireColorRequested(color);
                return;
            }
        }
    }
    // Editing keys act on the selected parts.
    const bool editMods = !(event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier));
    if (hasSelectedParts() && editMods && m_press != Press::Moving) {
        const int step = (event->modifiers() & Qt::ShiftModifier) ? 5 : 1;
        switch (event->key()) {
        case Qt::Key_Left: emit nudgeRequested(-step, 0, event->isAutoRepeat()); return;
        case Qt::Key_Right: emit nudgeRequested(step, 0, event->isAutoRepeat()); return;
        case Qt::Key_Up: emit nudgeRequested(0, -step, event->isAutoRepeat()); return;
        case Qt::Key_Down: emit nudgeRequested(0, step, event->isAutoRepeat()); return;
        case Qt::Key_R:
            if (!event->isAutoRepeat())
                emit rotateRequested();
            return;
        case Qt::Key_D:
            if (!event->isAutoRepeat())
                emit duplicateRequested();
            return;
        default: break;
        }
    }
    if ((event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) && !scene()->selectedItems().isEmpty()) {
        emit deleteRequested();
        return;
    }
    const QSize vp = viewport()->size();
    const bool big = event->modifiers() & Qt::ShiftModifier;
    const double fx = big ? vp.width() : vp.width() / 10.0;
    const double fy = big ? vp.height() : vp.height() / 10.0;
    switch (event->key()) {
    // As in Wokwi, the arrow moves the diagram: Up moves it up (the view
    // travels down), Right moves it right (the view travels left).
    case Qt::Key_Left: panBy(QPointF(fx, 0)); return;
    case Qt::Key_Right: panBy(QPointF(-fx, 0)); return;
    case Qt::Key_Up: panBy(QPointF(0, fy)); return;
    case Qt::Key_Down: panBy(QPointF(0, -fy)); return;
    case Qt::Key_Plus:
    case Qt::Key_Equal: zoomIn(); return;
    case Qt::Key_Minus: zoomOut(); return;
    case Qt::Key_F: fitContents(); return;
    case Qt::Key_G: toggleGrid(); return;
    case Qt::Key_F2:
        if (hasSelectedParts())
            emit editPartRequested();
        return;
    case Qt::Key_A:
        if (!event->isAutoRepeat() && !(event->modifiers() & (Qt::ControlModifier | Qt::AltModifier)))
            emit addPartRequested();
        return;
    case Qt::Key_Escape:
        if (m_placing) {
            emit placeCancelled();
        } else if (m_press == Press::Moving) {
            m_press = Press::None;
            m_pressItem = nullptr;
            viewport()->setCursor(Qt::ArrowCursor);
            emit moveEnded(false);
        } else if (m_press == Press::Handle || m_press == Press::Corner || m_press == Press::Split
                   || m_press == Press::End) {
            if (m_dragWire)
                m_dragWire->showPreview(m_dragWire->route());
            m_dragWire = nullptr;
            m_press = Press::None;
        } else if (m_press == Press::Marquee) {
            m_press = Press::None;
            m_autoScroll.stop();
            viewport()->update();
        } else {
            clearSelection();
        }
        return;
    case Qt::Key_Space:
        if (!event->isAutoRepeat()) {
            m_spaceHeld = true;
            viewport()->setCursor(Qt::OpenHandCursor);
        }
        return;
    default: break;
    }
    QGraphicsView::keyPressEvent(event);
}

void SchematicView::keyReleaseEvent(QKeyEvent* event)
{
    if (m_simMode && !event->isAutoRepeat() && !event->text().isEmpty())
        emit simKey(event->text(), false);
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
        m_spaceHeld = false;
        if (!m_panning)
            viewport()->setCursor(Qt::ArrowCursor);
        return;
    }
    QGraphicsView::keyReleaseEvent(event);
}
