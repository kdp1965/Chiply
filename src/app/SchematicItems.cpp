#include "SchematicItems.h"

#include "SymbolPainter.h"
#include "Theme.h"

#include <QPainter>
#include <QPainterPath>
#include <QPen>

namespace {
constexpr double kUnknownSize = 38.4;
constexpr double kWireWidth = 2.0; // scene px
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
    QPainterPath path;
    if (!route.empty()) {
        path.moveTo(route[0].x, route[0].y);
        for (std::size_t i = 1; i < route.size(); ++i)
            path.lineTo(route[i].x, route[i].y);
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
