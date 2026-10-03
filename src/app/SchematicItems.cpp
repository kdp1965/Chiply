#include "SchematicItems.h"

#include "SymbolPainter.h"
#include "Theme.h"

#include <QPainter>
#include <QPainterPath>
#include <QPen>

#include <algorithm>

namespace {
constexpr double kUnknownSize = 38.4;
constexpr double kWireWidth = 2.0; // scene px, Wokwi's stroke width
constexpr double kCornerRadius = 4.0;
}

PartItem::PartItem(const chiply::Part& part, const chiply::PartDef* def)
    : m_part(part)
    , m_def(def)
    , m_w(def ? def->width : kUnknownSize)
    , m_h(def ? def->height : kUnknownSize)
{
    setPos(part.left, part.top);
    setTransformOriginPoint(m_w / 2, m_h / 2);
    setRotation(part.rotate);
    setCacheMode(QGraphicsItem::DeviceCoordinateCache);
    setToolTip(QString::fromStdString(part.id + "  (" + part.type + ")"));
    setZValue(0);
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
}

WireItem::WireItem(const chiply::Wire& wire, const std::vector<chiply::Point>& route)
{
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

void WireItem::restyle()
{
    const CanvasColors& c = Theme::instance().canvas();
    setPen(QPen(c.displayWireColor(m_fileColor), kWireWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
}

TextItem::TextItem(const chiply::Part& part)
{
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

void TextItem::restyle()
{
    setBrush(Theme::instance().isDark() ? QColor(0xcc, 0xcc, 0xcc) : QColor(0, 0, 0));
}
