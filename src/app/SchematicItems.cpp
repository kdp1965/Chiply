#include "SchematicItems.h"

#include "SymbolPainter.h"
#include "Theme.h"

#include <QPainter>
#include <QPainterPath>
#include <QGraphicsSceneHoverEvent>
#include <QPainterPathStroker>
#include <QPen>
#include <QStyleOptionGraphicsItem>
#include <QToolTip>

#include <algorithm>
#include <cmath>

namespace {
constexpr double kUnknownSize = 38.4;
constexpr double kWireWidth = 2.0; // scene px, Wokwi's stroke width
constexpr double kCornerRadius = 4.0;
constexpr double kPinHitRadius = 4.5; // px around a pin that counts as "on the pin"
}

PartItem::PartItem(const chiply::Part& part, const chiply::PartDef* def)
    : m_part(part)
    , m_def(def)
    , m_w(def ? def->width : kUnknownSize)
    , m_h(def ? def->height : kUnknownSize)
{
    setPos(part.left, part.top);
    // Same pivot as chiply::partToDiagram: the whole-pixel layout box center.
    setTransformOriginPoint(std::round(m_w) / 2, std::round(m_h) / 2);
    setRotation(part.rotate);
    setCacheMode(QGraphicsItem::DeviceCoordinateCache);
    setToolTip(QString::fromStdString(part.id));
    setAcceptHoverEvents(true);
    setFlag(QGraphicsItem::ItemIsSelectable);
    setZValue(0);
}

QPainterPath PartItem::shape() const
{
    // The outline plus the pin hit radius, so pins on the edge are hoverable.
    QPainterPath p;
    p.addRect(QRectF(0, 0, m_w, m_h).adjusted(-kPinHitRadius, -kPinHitRadius, kPinHitRadius, kPinHitRadius));
    return p;
}

QRectF PartItem::outlineSceneRect() const
{
    return mapRectToScene(QRectF(0, 0, m_w, m_h));
}

const chiply::PinDef* PartItem::pinAt(QPointF local) const
{
    if (!m_def)
        return nullptr;
    const chiply::PinDef* best = nullptr;
    double bestD = kPinHitRadius * kPinHitRadius;
    for (const chiply::PinDef& pin : m_def->pins) {
        const double dx = local.x() - pin.x, dy = local.y() - pin.y;
        const double d = dx * dx + dy * dy;
        if (d <= bestD) {
            bestD = d;
            best = &pin;
        }
    }
    return best;
}

void PartItem::hoverEnterEvent(QGraphicsSceneHoverEvent* event)
{
    m_hovered = true;
    hoverMoveEvent(event);
    update();
}

void PartItem::hoverMoveEvent(QGraphicsSceneHoverEvent* event)
{
    const chiply::PinDef* pin = pinAt(event->pos());
    if (pin != m_hoverPin) {
        m_hoverPin = pin;
        if (pin) {
            // Pin names show immediately, like Wokwi's pin overlay.
            const QString label = QString::fromStdString(m_part.id + ":" + pin->name);
            QToolTip::showText(event->screenPos() + QPoint(12, 12), label);
            setToolTip(label);
        } else {
            QToolTip::hideText();
            setToolTip(QString::fromStdString(m_part.id));
        }
        update();
    }
}

void PartItem::hoverLeaveEvent(QGraphicsSceneHoverEvent*)
{
    m_hovered = false;
    if (m_hoverPin)
        QToolTip::hideText();
    m_hoverPin = nullptr;
    setToolTip(QString::fromStdString(m_part.id));
    update();
}

QRectF PartItem::boundingRect() const
{
    // Text and leads may extend slightly past the outline.
    return QRectF(0, 0, m_w, m_h).adjusted(-4, -4, 4, 14);
}

void PartItem::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*)
{
    const CanvasColors& c = Theme::instance().canvas();
    if (m_def)
        SymbolPainter::paint(painter, *m_def, m_part, c);
    else
        SymbolPainter::paintUnknown(painter, m_w, m_h, QString::fromStdString(m_part.type), c);

    if (isSelected()) {
        QPen pen(c.selection, 2);
        pen.setCosmetic(true);
        painter->setPen(pen);
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(QRectF(0, 0, m_w, m_h).adjusted(-2, -2, 2, 2));
    } else if (m_hovered) {
        // Wokwi's dotted "you are on this part" outline.
        QPen pen(c.partText, 0, Qt::DotLine);
        pen.setCosmetic(true);
        painter->setPen(pen);
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(QRectF(0, 0, m_w, m_h).adjusted(-1, -1, 1, 1));
    }
    if (m_hoverPin) {
        QColor fill = c.selection;
        fill.setAlpha(110);
        painter->setPen(QPen(c.selection, 0));
        painter->setBrush(fill);
        painter->drawEllipse(QPointF(m_hoverPin->x, m_hoverPin->y), 3.5, 3.5);
    }
}

WireItem::WireItem(const chiply::Wire& wire, const std::vector<chiply::Point>& route, int index)
    : m_index(index)
    , m_fromPart(wire.from.part)
    , m_toPart(wire.to.part)
{
    setFlag(QGraphicsItem::ItemIsSelectable);
    // Corners rounded with a 4 px radius, as Wokwi draws them; a corner on a
    // short segment uses at most half of that segment.
    QPainterPath path;
    if (!route.empty()) {
        auto P = [&](std::size_t i) { return QPointF(route[i].x, route[i].y); };
        path.moveTo(P(0));
        for (std::size_t i = 1; i + 1 < route.size(); ++i) {
            const QPointF a = P(i - 1), c = P(i), b = P(i + 1);
            const QLineF in(c, a), out(c, b);
            const double r = std::min({kCornerRadius, in.length() / 2, out.length() / 2});
            if (r < 0.05) {
                path.lineTo(c);
                continue;
            }
            const QPointF p1 = c + (a - c) * (r / in.length());
            const QPointF p2 = c + (b - c) * (r / out.length());
            path.lineTo(p1);
            path.quadTo(c, p2);
        }
        if (route.size() > 1)
            path.lineTo(P(route.size() - 1));
    }
    setPath(path);
    m_fileColor = QColor(QString::fromStdString(wire.color));
    if (wire.color.empty())
        setVisible(false); // Wokwi hides wires with an empty color
    if (!m_fileColor.isValid())
        m_fileColor = QColor("green");
    setToolTip(QString::fromStdString(wire.from.str() + "  →  " + wire.to.str()));
    setZValue(1); // wires draw above parts, as in Wokwi
    restyle();
}

QPainterPath WireItem::shape() const
{
    QPainterPathStroker stroker;
    stroker.setWidth(kWireWidth + 4);
    stroker.setCapStyle(Qt::RoundCap);
    stroker.setJoinStyle(Qt::RoundJoin);
    QPainterPath hit = stroker.createStroke(path());
    // Leave the pins at both ends to the part underneath, so hovering a pin
    // shows "part:PIN" even though the wire draws on top of it.
    QPainterPath ends;
    const QPainterPath& p = path();
    if (p.elementCount() > 1) {
        ends.addEllipse(p.pointAtPercent(0), kPinHitRadius, kPinHitRadius);
        ends.addEllipse(p.pointAtPercent(1), kPinHitRadius, kPinHitRadius);
        hit = hit.subtracted(ends);
    }
    return hit;
}

void WireItem::setLink(Link l)
{
    if (l == m_link)
        return;
    m_link = l;
    restyle();
    update();
}

QRectF WireItem::boundingRect() const
{
    return QGraphicsPathItem::boundingRect().adjusted(-4, -4, 4, 4);
}

void WireItem::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*)
{
    const CanvasColors& c = Theme::instance().canvas();
    if (isSelected() || m_link == Link::Implicit) {
        // Selection halo under the wire; explicit selection is stronger.
        QColor halo = c.selection;
        halo.setAlpha(isSelected() ? 170 : 110);
        painter->setPen(QPen(halo, kWireWidth + 5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter->setBrush(Qt::NoBrush);
        painter->drawPath(path());
    }
    painter->setPen(pen());
    painter->setBrush(Qt::NoBrush);
    painter->drawPath(path());
}

void WireItem::restyle()
{
    const CanvasColors& c = Theme::instance().canvas();
    // A wire that will stretch when the selection moves is drawn dashed.
    QPen p(c.displayWireColor(m_fileColor), kWireWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    if (m_link == Link::Stretch)
        p.setDashPattern({3, 2});
    setPen(p);
}

TextItem::TextItem(const chiply::Part& part)
    : m_id(part.id)
{
    setFlag(QGraphicsItem::ItemIsSelectable);
    std::string text = part.attrs.value("text", std::string());
    setText(QString::fromStdString(text));
    QFont f("Helvetica");
    f.setPixelSize(16); // browser default size Wokwi's text part inherits
    setFont(f);
    setPos(part.left, part.top);
    setTransformOriginPoint(boundingRect().center());
    setRotation(part.rotate);
    setToolTip(QString::fromStdString(part.id));
    setZValue(0.5);
    restyle();
}

void TextItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget)
{
    QStyleOptionGraphicsItem o(*option);
    o.state &= ~QStyle::State_Selected; // draw our own selection outline
    QGraphicsSimpleTextItem::paint(painter, &o, widget);
    if (isSelected()) {
        QPen pen(Theme::instance().canvas().selection, 2);
        pen.setCosmetic(true);
        painter->setPen(pen);
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(boundingRect().adjusted(-2, -2, 2, 2));
    }
}

std::string itemPartId(const QGraphicsItem* item)
{
    if (!item)
        return {};
    if (item->type() == PartItem::Type)
        return static_cast<const PartItem*>(item)->partId();
    if (item->type() == TextItem::Type)
        return static_cast<const TextItem*>(item)->partId();
    return {};
}

void TextItem::restyle()
{
    setBrush(Theme::instance().isDark() ? QColor(0xcc, 0xcc, 0xcc) : QColor(0, 0, 0));
}
