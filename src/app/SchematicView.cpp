#include "SchematicView.h"

#include "SchematicItems.h"
#include "Theme.h"
#include "core/JsonFormat.h"

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
}

void SchematicView::zoomIn() { zoomBy(1.25, viewport()->rect().center()); }
void SchematicView::zoomOut() { zoomBy(0.8, viewport()->rect().center()); }

void SchematicView::fitContents()
{
    QRectF r = scene()->itemsBoundingRect();
    if (r.isEmpty())
        r = QRectF(-200, -200, 400, 400);
    fitInView(r.adjusted(-20, -20, 20, 20), Qt::KeepAspectRatio);
    emit zoomChanged(zoom());
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

void SchematicView::mousePressEvent(QMouseEvent* event)
{
    const QPoint pos = event->position().toPoint();
    if (event->button() == Qt::LeftButton && !m_spaceHeld) {
        for (QGraphicsItem* it : items(pos)) {
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
    if (event->button() == Qt::LeftButton && !m_spaceHeld && (event->modifiers() & Qt::ShiftModifier)) {
        // Shift+drag pans; Shift+click without moving stays a click (Wokwi's
        // add-to-selection). Decide once the mouse moves.
        m_shiftPending = true;
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
    if (event->button() == Qt::LeftButton) {
        m_pressPos = pos;
        m_lastMousePos = pos;
        m_lastMods = event->modifiers();
        m_pressItem = selectableAt(pos);
        m_press = m_pressItem ? Press::Item : Press::Empty;
        m_marqueeStart = mapToScene(pos);
        event->accept();
        return;
    }
    QGraphicsView::mousePressEvent(event);
}

void SchematicView::mouseMoveEvent(QMouseEvent* event)
{
    const QPoint pos = event->position().toPoint();
    if (m_press == Press::Handle) {
        updateHandleDrag(pos, event->modifiers());
        event->accept();
        return;
    }
    if (m_shiftPending && (pos - m_pressPos).manhattanLength() > 4) {
        m_shiftPending = false;
        m_panning = true;
        viewport()->setCursor(Qt::ClosedHandCursor);
    }
    if (m_shiftPending) {
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
        viewport()->setCursor(Qt::SizeAllCursor);
        emit moveStarted(QString::fromStdString(itemPartId(m_pressItem)));
    }
    if (m_press == Press::Moving) {
        const QPointF d = mapToScene(pos) - mapToScene(m_pressPos);
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
    if (m_shiftPending && event->button() == Qt::LeftButton) {
        m_shiftPending = false;
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

void SchematicView::autoScrollTick()
{
    if (m_press != Press::Marquee) {
        m_autoScroll.stop();
        return;
    }
    // Scroll when the cursor is near or past an edge, faster the further out.
    const QRect r = viewport()->rect();
    constexpr int kEdge = 30;
    auto speed = [](int d) { return d >= kEdge ? 0.0 : std::min(40.0, (kEdge - d) * 0.6); };
    const QPoint p = m_lastMousePos;
    QPointF d(0, 0);
    d.rx() -= speed(p.x() - r.left());
    d.rx() += speed(r.right() - p.x());
    d.ry() -= speed(p.y() - r.top());
    d.ry() += speed(r.bottom() - p.y());
    if (!d.isNull()) {
        panBy(d);
        viewport()->update();
    }
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
    emit selectionEdited();
}

void SchematicView::clearSelection()
{
    {
        const QSignalBlocker block(scene());
        scene()->clearSelection();
    }
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
    {
        const QSignalBlocker block(scene());
        scene()->clearSelection();
        item->setSelected(true);
    }
    emit selectionEdited();
}

void SchematicView::toggleSelected(QGraphicsItem* item)
{
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
    case Qt::Key_Escape:
        if (m_press == Press::Moving) {
            m_press = Press::None;
            m_pressItem = nullptr;
            viewport()->setCursor(Qt::ArrowCursor);
            emit moveEnded(false);
        } else if (m_press == Press::Handle) {
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
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
        m_spaceHeld = false;
        if (!m_panning)
            viewport()->setCursor(Qt::ArrowCursor);
        return;
    }
    QGraphicsView::keyReleaseEvent(event);
}
