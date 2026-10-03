#include "SchematicView.h"

#include "Theme.h"

#include <QKeyEvent>
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
    setDragMode(QGraphicsView::RubberBandDrag);
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

void SchematicView::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::MiddleButton || (event->button() == Qt::LeftButton && m_spaceHeld)) {
        m_panning = true;
        m_lastPanPos = event->position().toPoint();
        viewport()->setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }
    QGraphicsView::mousePressEvent(event);
}

void SchematicView::mouseMoveEvent(QMouseEvent* event)
{
    if (m_panning) {
        const QPoint p = event->position().toPoint();
        panBy(QPointF(m_lastPanPos - p));
        m_lastPanPos = p;
        event->accept();
        return;
    }
    QGraphicsView::mouseMoveEvent(event);
}

void SchematicView::mouseReleaseEvent(QMouseEvent* event)
{
    if (m_panning && (event->button() == Qt::MiddleButton || event->button() == Qt::LeftButton)) {
        m_panning = false;
        viewport()->setCursor(m_spaceHeld ? Qt::OpenHandCursor : Qt::ArrowCursor);
        event->accept();
        return;
    }
    QGraphicsView::mouseReleaseEvent(event);
}

void SchematicView::keyPressEvent(QKeyEvent* event)
{
    const QSize vp = viewport()->size();
    const bool big = event->modifiers() & Qt::ShiftModifier;
    const double fx = big ? vp.width() : vp.width() / 10.0;
    const double fy = big ? vp.height() : vp.height() / 10.0;
    switch (event->key()) {
    case Qt::Key_Left: panBy(QPointF(-fx, 0)); return;
    case Qt::Key_Right: panBy(QPointF(fx, 0)); return;
    case Qt::Key_Up: panBy(QPointF(0, -fy)); return;
    case Qt::Key_Down: panBy(QPointF(0, fy)); return;
    case Qt::Key_Plus:
    case Qt::Key_Equal: zoomIn(); return;
    case Qt::Key_Minus: zoomOut(); return;
    case Qt::Key_F: fitContents(); return;
    case Qt::Key_G: toggleGrid(); return;
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
