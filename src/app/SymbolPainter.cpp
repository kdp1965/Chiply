#include "SymbolPainter.h"

#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>

#include <cmath>
#include <functional>
#include <map>

using chiply::Part;
using chiply::PartDef;
using chiply::PartLibrary;

namespace {

constexpr double kMm = PartLibrary::kPxPerMm;
constexpr double kStroke = 0.4; // mm, same weight as Wokwi's symbols

struct Ctx {
    QPainter* p;
    const PartDef& def;
    const Part& part;
    const CanvasColors& c;
    const SymbolPainter::SimVisual* sim = nullptr;
    bool bit(int i) const { return sim && ((sim->bits >> i) & 1u); }

    QPen leadPen() const { return QPen(c.lead, kStroke, Qt::SolidLine, Qt::FlatCap, Qt::MiterJoin); }
    QPen bodyPen() const { return QPen(c.partStroke, kStroke, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin); }
    void line(double x1, double y1, double x2, double y2) const { p->drawLine(QPointF(x1, y1), QPointF(x2, y2)); }
    void leads(std::initializer_list<QLineF> ls) const
    {
        p->setPen(leadPen());
        for (const QLineF& l : ls)
            p->drawLine(l);
    }
    void body(const QPainterPath& path) const
    {
        p->setPen(bodyPen());
        p->setBrush(Qt::NoBrush);
        p->drawPath(path);
    }
    void bubble(double cx, double cy) const
    {
        p->setPen(bodyPen());
        p->setBrush(Qt::NoBrush);
        p->drawEllipse(QPointF(cx, cy), 0.75, 0.75);
    }
    void text(double x, double y, const QString& s, double size, Qt::Alignment align = Qt::AlignCenter,
              const QColor* color = nullptr) const
    {
        QFont f("Helvetica");
        f.setPixelSize(100); // draw large and scale down for crisp small text
        p->save();
        p->setFont(f);
        p->setPen(color ? *color : c.partStroke);
        p->translate(x, y);
        p->scale(size / 100.0, size / 100.0);
        QRectF r(-2000, -60, 4000, 120);
        if (align & Qt::AlignLeft)
            r = QRectF(0, -60, 4000, 120);
        else if (align & Qt::AlignRight)
            r = QRectF(-4000, -60, 4000, 120);
        p->drawText(r, int(align | Qt::AlignVCenter), s);
        p->restore();
    }
};

// ---- logic gates (25.4 x 10 mm, inputs at y 2.54 / 7.62, output 5.08) ----

QPainterPath andBody()
{
    QPainterPath b;
    b.moveTo(7.62, 0.4);
    b.lineTo(12.7, 0.4);
    b.arcTo(QRectF(12.7 - 4.68, 5.08 - 4.68, 9.36, 9.36), 90, -180);
    b.lineTo(7.62, 9.76);
    b.closeSubpath();
    return b;
}

constexpr double kOrBulge = 2.6; // control-point offset of the OR back curve

void orBack(QPainterPath& b, double x)
{
    b.moveTo(x, 0.4);
    b.quadTo(x + kOrBulge, 5.08, x, 9.76);
}

// x of the OR back curve starting at x0, at height y. The quadratic runs
// from (x0, 0.4) via (x0 + bulge, 5.08) to (x0, 9.76); its y is linear in t.
double orBackX(double x0, double y)
{
    const double t = (y - 0.4) / 9.36;
    return x0 + 2 * t * (1 - t) * kOrBulge;
}

QPainterPath orBody(double x0)
{
    QPainterPath b;
    orBack(b, x0);
    b.moveTo(x0, 0.4);
    b.quadTo(x0 + 7.5, 0.4, x0 + 11.2, 5.08);
    b.quadTo(x0 + 7.5, 9.76, x0, 9.76);
    return b;
}

void gate(const Ctx& k, const std::string& kind)
{
    const bool inv = kind == "nand" || kind == "nor" || kind == "xnor";
    double outX = 17.38;
    double inEnd = 7.62;
    if (kind == "and" || kind == "nand") {
        k.body(andBody());
    } else {
        const double x0 = (kind == "xor" || kind == "xnor") ? 8.4 : 7.2;
        QPainterPath b = orBody(x0);
        if (kind == "xor" || kind == "xnor")
            orBack(b, x0 - 1.2);
        k.body(b);
        outX = x0 + 11.2;
        // Leads end exactly on the (outer) back curve; both inputs sit at
        // the same height from the curve's ends, so one x serves both.
        const double backX = (kind == "xor" || kind == "xnor") ? x0 - 1.2 : x0;
        inEnd = orBackX(backX, 2.54);
    }
    if (inv) {
        k.bubble(outX + 0.75, 5.08);
        outX += 1.5;
    }
    k.leads({QLineF(0, 2.54, inEnd, 2.54), QLineF(0, 7.62, inEnd, 7.62), QLineF(outX, 5.08, 25.4, 5.08)});
}

void inverter(const Ctx& k, bool bubble)
{
    QPainterPath t;
    t.moveTo(7.62, 0.9);
    t.lineTo(16.0, 5.08);
    t.lineTo(7.62, 9.26);
    t.closeSubpath();
    k.body(t);
    double outX = 16.0;
    if (bubble) {
        k.bubble(16.75, 5.08);
        outX = 17.5;
    }
    k.leads({QLineF(0, 5.08, 7.62, 5.08), QLineF(outX, 5.08, 25.4, 5.08)});
}

void mux(const Ctx& k)
{
    // Trapezoid, wide side at the inputs; SEL enters from below.
    QPainterPath t;
    t.moveTo(8.89, 0.2);
    t.lineTo(16.51, 2.4);
    t.lineTo(16.51, 7.76);
    t.lineTo(8.89, 9.96);
    t.closeSubpath();
    k.body(t);
    const double selY = 9.96 - (12.7 - 8.89) / (16.51 - 8.89) * 2.2;
    k.leads({QLineF(0, 2.54, 8.89, 2.54), QLineF(0, 7.62, 8.89, 7.62), QLineF(16.51, 5.08, 25.4, 5.08),
             QLineF(12.7, 12.7, 12.7, selY)});
    k.text(10.0, 2.54, "0", 1.9, Qt::AlignLeft);
    k.text(10.0, 7.62, "1", 1.9, Qt::AlignLeft);
}

void flipFlop(const Ctx& k, const std::string& kind)
{
    // Box between x 7.62 and 17.78; vertical extent depends on the variant.
    double top = 0.4, bottom = 9.76, dy = 0;
    if (kind == "dff-sr") {
        top = 2.54;
        bottom = 12.7;
        dy = 2.54;
    }
    k.p->setPen(k.bodyPen());
    k.p->setBrush(Qt::NoBrush);
    k.p->drawRect(QRectF(7.62, top, 10.16, bottom - top));

    std::vector<QLineF> l;
    if (kind == "sr") {
        l = {QLineF(0, 2.54, 7.62, 2.54), QLineF(0, 5.08, 7.62, 5.08), QLineF(0, 7.62, 7.62, 7.62)};
        k.text(8.4, 2.54, "S", 2.2, Qt::AlignLeft);
        k.text(8.4, 7.62, "R", 2.2, Qt::AlignLeft);
    } else {
        l = {QLineF(0, 2.54 + dy, 7.62, 2.54 + dy), QLineF(0, 7.62 + dy, 7.62, 7.62 + dy)};
        k.text(8.4, 2.54 + dy, "D", 2.2, Qt::AlignLeft);
    }
    l.push_back(QLineF(17.78, 2.54 + dy, 25.4, 2.54 + dy));
    l.push_back(QLineF(17.78, 7.62 + dy, 25.4, 7.62 + dy));
    if (kind == "dff-r")
        l.push_back(QLineF(12.7, 12.7, 12.7, bottom));
    if (kind == "dff-sr") {
        l.push_back(QLineF(12.7, 0, 12.7, top));
        l.push_back(QLineF(12.7, 15.24, 12.7, bottom));
        k.text(12.7, top + 1.4, "S", 2.0);
        k.text(12.7, bottom - 1.4, "R", 2.0);
    }
    if (kind == "dff-r")
        k.text(12.7, bottom - 1.4, "R", 2.0);
    k.p->setPen(k.leadPen());
    for (const QLineF& x : l)
        k.p->drawLine(x);

    // Clock wedge on the CLK input.
    const double cy = (kind == "sr") ? 5.08 : 7.62 + dy;
    QPainterPath w;
    w.moveTo(7.62, cy - 1.1);
    w.lineTo(9.3, cy);
    w.lineTo(7.62, cy + 1.1);
    k.body(w);
    // Q and Q-bar labels.
    k.text(17.0, 2.54 + dy, "Q", 2.2, Qt::AlignRight);
    k.text(17.0, 7.62 + dy, "Q", 2.2, Qt::AlignRight);
    k.p->setPen(QPen(k.c.partStroke, 0.18));
    k.line(15.35, 6.35 + dy, 16.95, 6.35 + dy);
}

void vcc(const Ctx& k)
{
    k.leads({QLineF(2.54, 7.42, 2.54, 3.0)});
    k.p->setPen(QPen(k.c.lead, kStroke, Qt::SolidLine, Qt::RoundCap));
    k.line(0.6, 3.0, 4.48, 3.0);
    k.text(2.54, 1.3, "VCC", 2.0, Qt::AlignCenter, &k.c.lead);
}

void gnd(const Ctx& k)
{
    k.leads({QLineF(2.7, 0, 2.7, 5.08)});
    QPainterPath t;
    t.moveTo(0.12, 5.08);
    t.lineTo(5.28, 5.08);
    t.lineTo(2.7, 9.5);
    t.closeSubpath();
    k.p->setPen(QPen(k.c.lead, kStroke, Qt::SolidLine, Qt::FlatCap, Qt::MiterJoin));
    k.p->setBrush(Qt::NoBrush);
    k.p->drawPath(t);
}

void clockGen(const Ctx& k)
{
    k.p->setPen(k.bodyPen());
    k.p->setBrush(Qt::NoBrush);
    k.p->drawRect(QRectF(0.2, 0.2, 9.8, 9.6));
    QPainterPath sq;
    sq.moveTo(1.5, 6.5);
    sq.lineTo(3.4, 6.5);
    sq.lineTo(3.4, 2.6);
    sq.lineTo(5.3, 2.6);
    sq.lineTo(5.3, 6.5);
    sq.lineTo(7.2, 6.5);
    sq.lineTo(7.2, 2.6);
    sq.lineTo(8.8, 2.6);
    k.body(sq);
    k.leads({QLineF(10.0, 5.08, 17.78, 5.08)});
    std::string f = k.part.attrs.value("frequency", std::string("10k"));
    k.text(5.1, 8.5, QString::fromStdString(f) + "Hz", 1.7);
}

void ttBlock(const Ctx& k)
{
    const double w = k.def.width / kMm, h = k.def.height / kMm;
    k.p->setPen(k.bodyPen());
    k.p->setBrush(Qt::NoBrush);
    k.p->drawRoundedRect(QRectF(2.54, 0.6, w - 5.08, h - 1.2), 0.8, 0.8);
    QString title;
    if (k.def.symbol == "tt-input" || k.def.symbol == "tt-input-8")
        title = "INPUT";
    else if (k.def.symbol == "tt-output")
        title = "OUTPUT";
    else
        title = "D" + QString::fromStdString(k.part.attrs.value("verilogBit", std::string("?")));
    k.text(w / 2, k.def.symbol == "tt-bidir" ? h / 2 : 3.2, title, 2.4);
    if (k.def.symbol != "tt-bidir")
        k.text(w / 2, 5.4, "Tiny Tapeout", 1.5);
    k.p->setPen(k.leadPen());
    for (const auto& pin : k.def.pins) {
        const double x = pin.x / kMm, y = pin.y / kMm;
        const bool left = x < w / 2;
        k.line(x, y, left ? 2.54 : w - 2.54, y);
        k.text(left ? 3.2 : w - 3.2, y, QString::fromStdString(pin.name), 1.45, left ? Qt::AlignLeft : Qt::AlignRight);
        k.p->setPen(k.leadPen());
    }
}

void junction(const Ctx& k)
{
    k.p->setPen(Qt::NoPen);
    k.p->setBrush(k.c.lead);
    k.p->drawEllipse(QPointF(1.27, 1.27), 1.0, 1.0);
}

// ---- physical parts (drawn in px; approximate artwork, exact pins) ----

QColor namedColor(const Part& part, const char* fallback)
{
    QColor c(QString::fromStdString(part.attrs.value("color", std::string(fallback))));
    return c.isValid() ? c : QColor(fallback);
}

void pins(const Ctx& k, double len, bool vertical)
{
    k.p->setPen(QPen(QColor(0xaa, 0xaa, 0xaa), 2.0, Qt::SolidLine, Qt::FlatCap));
    for (const auto& pin : k.def.pins) {
        if (vertical)
            k.line(pin.x, pin.y, pin.x, pin.y + (pin.y > k.def.height / 2 ? -len : len));
        else
            k.line(pin.x, pin.y, pin.x + (pin.x > k.def.width / 2 ? -len : len), pin.y);
    }
}

void pushbutton(const Ctx& k)
{
    pins(k, 12, false);
    k.p->setPen(QPen(QColor(0x46, 0x46, 0x46), 1));
    k.p->setBrush(QColor(0x46, 0x46, 0x46));
    k.p->drawRoundedRect(QRectF(11.3, 0, 45.4, 45.4), 2, 2);
    k.p->setBrush(QColor(0xea, 0xea, 0xea));
    k.p->drawRoundedRect(QRectF(14.1, 2.8, 39.8, 39.8), 1, 1);
    const bool pressed = k.bit(0);
    QColor cap = namedColor(k.part, "red");
    k.p->setBrush(pressed ? cap.darker(150) : cap);
    k.p->setPen(QPen(QColor(0, 0, 0, pressed ? 160 : 80), pressed ? 2.5 : 1));
    k.p->drawEllipse(QPointF(34, 22.7), pressed ? 11.5 : 13, pressed ? 11.5 : 13);
    std::string label = k.part.attrs.value("label", std::string());
    if (!label.empty())
        k.text(34, 52, QString::fromStdString(label), 9, Qt::AlignCenter, &k.c.partText);
}

void slideSwitch(const Ctx& k)
{
    pins(k, 14, true);
    k.p->setPen(QPen(QColor(0x66, 0x66, 0x66), 1));
    k.p->setBrush(QColor(0x88, 0x88, 0x88));
    k.p->drawRect(QRectF(0, 7.8, 32.1, 13.2));
    const bool right = k.sim ? k.bit(0) : k.part.attrs.value("value", std::string("")) == "1";
    // Light lever with a dark outline: easy to see, and its position (not
    // its colour) shows the state.
    k.p->setPen(QPen(QColor(0x33, 0x33, 0x33), 1));
    k.p->setBrush(QColor(0xf0, 0xf0, 0xf0));
    k.p->drawRoundedRect(QRectF(right ? 18.5 : 7.6, 1, 6, 8), 1, 1);
}

void dipSwitch(const Ctx& k)
{
    // Layout and colors follow wokwi-elements: red board, brown slots, a
    // white knob at the bottom of each slot when off (left when the part is
    // rotated 90 degrees), white "ON" and numbers.
    pins(k, 8, true);
    k.p->setPen(Qt::NoPen);
    k.p->setBrush(QColor(0xd7, 0x2c, 0x2c));
    k.p->drawRect(QRectF(0, 8.5, 82.87, 38.08));
    const QColor white(0xff, 0xfe, 0xf4);
    k.text(6.3, 15.5, "ON", 7.0, Qt::AlignLeft, &white);
    std::string values = k.part.attrs.value("values", std::string());
    for (int i = 0; i < 8; ++i) {
        const double x = 8.1 + 9.6 * i;
        k.p->setBrush(QColor(0x91, 0x7c, 0x6f));
        k.p->drawRect(QRectF(x - 2.9, 21.2, 5.8, 13));
        const bool on = k.sim ? k.bit(i) : (i < int(values.size()) && values[size_t(i)] == '1');
        k.p->setBrush(white);
        k.p->drawRoundedRect(QRectF(x - 2.6, on ? 21.6 : 28.6, 5.2, 5.3), 0.7, 0.7);
        k.text(x - 0.6, 40.5, QString::number(i + 1), 7.0, Qt::AlignCenter, &white);
    }
}

void resistor(const Ctx& k)
{
    k.p->setPen(QPen(QColor(0xaa, 0xaa, 0xaa), 2.4));
    k.line(0, 5.65, 58.8, 5.65);
    k.p->setPen(QPen(QColor(0x99, 0x80, 0x60), 0.6));
    k.p->setBrush(QColor(0xd5, 0xb5, 0x97));
    k.p->drawRoundedRect(QRectF(13, 1, 33, 9.3), 3, 3);
    const QColor bands[] = {QColor("brown"), QColor("black"), QColor("red"), QColor(0xf1, 0xd8, 0x63)};
    for (int i = 0; i < 4; ++i) {
        k.p->setPen(Qt::NoPen);
        k.p->setBrush(bands[i]);
        k.p->drawRect(QRectF(17 + i * 6.5, 1, 3, 9.3));
    }
}

void led(const Ctx& k)
{
    k.p->setPen(QPen(QColor(0x8c, 0x8c, 0x8c), 2.2));
    k.line(15, 42, 15, 28);
    k.line(25, 42, 25, 28);
    QColor c = namedColor(k.part, "red");
    const bool lit = k.bit(0);
    if (lit) {
        // Glow around a lit LED.
        QRadialGradient glow(QPointF(20, 16), 22);
        QColor g = c;
        g.setAlpha(150);
        glow.setColorAt(0, g);
        g.setAlpha(0);
        glow.setColorAt(1, g);
        k.p->setPen(Qt::NoPen);
        k.p->setBrush(glow);
        k.p->drawEllipse(QPointF(20, 16), 22, 22);
        c = c.lighter(130);
    } else if (k.sim) {
        c = c.darker(220); // off while simulating
    }
    c.setAlpha(lit ? 255 : 200);
    k.p->setPen(QPen(c.darker(140), 1));
    k.p->setBrush(c);
    QPainterPath d;
    d.moveTo(8, 30);
    d.lineTo(8, 14);
    d.arcTo(QRectF(8, 2, 24, 24), 180, -180);
    d.lineTo(32, 30);
    d.closeSubpath();
    k.p->drawPath(d);
    k.text(13, 46, "C", 6, Qt::AlignCenter, &k.c.partText);
    k.text(27, 46, "A", 6, Qt::AlignCenter, &k.c.partText);
}

void sevenSegment(const Ctx& k)
{
    // Ported from wokwi-elements' 7segment-element (MIT), single digit:
    // drawn in mm, black body 12.55 x 20.5, segments are polygons in a
    // skewX(-8deg) translate(3.5, 2.4) scale(0.81) group, each shrunk to 90 %
    // about its own centre; unlit segments #444; pins are #aaa dots.
    k.p->save();
    k.p->scale(kMm, kMm);
    k.p->setPen(Qt::NoPen);
    k.p->setBrush(QColor(0, 0, 0));
    k.p->drawRect(QRectF(0, 0, 12.55, 20.5));

    const QColor off(0x44, 0x44, 0x44);
    static const std::vector<std::vector<QPointF>> segments = {
        {{2, 0}, {8, 0}, {9, 1}, {8, 2}, {2, 2}, {1, 1}},         // A
        {{10, 2}, {10, 8}, {9, 9}, {8, 8}, {8, 2}, {9, 1}},       // B
        {{10, 10}, {10, 16}, {9, 17}, {8, 16}, {8, 10}, {9, 9}},  // C
        {{8, 18}, {2, 18}, {1, 17}, {2, 16}, {8, 16}, {9, 17}},   // D
        {{0, 16}, {0, 10}, {1, 9}, {2, 10}, {2, 16}, {1, 17}},    // E
        {{0, 8}, {0, 2}, {1, 1}, {2, 2}, {2, 8}, {1, 9}},         // F
        {{2, 8}, {8, 8}, {9, 9}, {8, 10}, {2, 10}, {1, 9}},       // G
    };
    k.p->save();
    k.p->setTransform(QTransform().shear(std::tan(-8 * M_PI / 180), 0), true);
    k.p->translate(3.5, 2.4);
    k.p->scale(0.81, 0.81);
    QColor litColor = namedColor(k.part, "red");
    int segIndex = 0;
    for (const auto& seg : segments) {
        k.p->setBrush(k.bit(segIndex++) ? litColor : off);
        QPolygonF poly;
        for (const QPointF& pt : seg)
            poly << pt;
        const QPointF c = poly.boundingRect().center();
        QPolygonF shrunk;
        for (const QPointF& pt : poly)
            shrunk << c + (pt - c) * 0.9;
        k.p->drawPolygon(shrunk);
    }
    k.p->restore();
    k.p->setBrush(k.bit(7) ? litColor : off);
    k.p->drawEllipse(QPointF(3.5 + 7.4, 16), 0.89, 0.89); // decimal point

    // Pin dots: 5 per row, top (y 1) and bottom (y 19).
    k.p->setBrush(QColor(0xaa, 0xaa, 0xaa));
    const double startX = (12.55 - 5 * 2.54) / 2;
    for (int i = 0; i < 5; ++i) {
        const double x = startX + 1.27 + i * 2.54;
        k.p->drawEllipse(QPointF(x, 1), 0.5, 0.5);
        k.p->drawEllipse(QPointF(x, 19), 0.5, 0.5);
    }
    k.p->restore();
}

void logicAnalyzer(const Ctx& k)
{
    k.p->setPen(QPen(QColor(0xcc, 0xcc, 0xcc), 2.8));
    for (const auto& pin : k.def.pins)
        k.line(0, pin.y, 16, pin.y);
    k.p->setPen(QPen(QColor(0x44, 0x37, 0x4b), 0.8));
    k.p->setBrush(QColor(0x80, 0x00, 0x80));
    k.p->drawRect(QRectF(15.4, 0.4, 140.6, 93.2));
    const QColor white(0xf2, 0xf2, 0xf2);
    for (const auto& pin : k.def.pins)
        k.text(19, pin.y, QString::fromStdString(pin.name), 8, Qt::AlignLeft, &white);
    k.text(105, 47, "Logic Analyzer", 10, Qt::AlignCenter, &white);
}

void piPico(const Ctx& k)
{
    k.p->setPen(Qt::NoPen);
    k.p->setBrush(QColor(0x1d, 0x7f, 0x3a));
    k.p->drawRoundedRect(QRectF(0, 0, k.def.width, k.def.height), 3, 3);
    k.p->setBrush(QColor(0xd4, 0xb0, 0x4a));
    for (const auto& pin : k.def.pins)
        k.p->drawEllipse(QPointF(pin.x, pin.y), 3, 3);
    k.p->setBrush(QColor(0x22, 0x22, 0x22));
    k.p->drawRect(QRectF(24, 80, 32, 32));
    k.p->setBrush(QColor(0xbb, 0xbb, 0xbb));
    k.p->drawRect(QRectF(28, 0, 23, 14));
    const QColor white(0xff, 0xff, 0xff);
    k.text(40, 128, "Raspberry Pi", 6, Qt::AlignCenter, &white);
    k.text(40, 136, "Pico", 6, Qt::AlignCenter, &white);
}

} // namespace

namespace SymbolPainter {

void paint(QPainter* p, const PartDef& def, const Part& part, const CanvasColors& colors, const SimVisual* sim)
{
    Ctx k{p, def, part, colors, sim};
    p->save();
    p->setRenderHint(QPainter::Antialiasing);
    const std::string& s = def.symbol;
    static const std::map<std::string, bool> mmSymbols = {
        {"and", true}, {"nand", true}, {"or", true}, {"nor", true}, {"xor", true}, {"xnor", true},
        {"not", true}, {"buffer", true}, {"mux", true}, {"dff", true}, {"dff-r", true}, {"dff-sr", true},
        {"sr", true}, {"vcc", true}, {"gnd", true}, {"clock", true}, {"tt-input", true}, {"tt-input-8", true},
        {"tt-output", true}, {"tt-bidir", true}, {"junction", true}};
    if (mmSymbols.count(s))
        p->scale(kMm, kMm);

    if (s == "and" || s == "nand" || s == "or" || s == "nor" || s == "xor" || s == "xnor")
        gate(k, s);
    else if (s == "not")
        inverter(k, true);
    else if (s == "buffer")
        inverter(k, false);
    else if (s == "mux")
        mux(k);
    else if (s == "dff" || s == "dff-r" || s == "dff-sr" || s == "sr")
        flipFlop(k, s);
    else if (s == "vcc")
        vcc(k);
    else if (s == "gnd")
        gnd(k);
    else if (s == "clock")
        clockGen(k);
    else if (s.rfind("tt-", 0) == 0)
        ttBlock(k);
    else if (s == "junction")
        junction(k);
    else if (s == "pushbutton")
        pushbutton(k);
    else if (s == "slide-switch")
        slideSwitch(k);
    else if (s == "dip-switch-8")
        dipSwitch(k);
    else if (s == "resistor")
        resistor(k);
    else if (s == "led")
        led(k);
    else if (s == "7segment")
        sevenSegment(k);
    else if (s == "logic-analyzer")
        logicAnalyzer(k);
    else if (s == "pi-pico")
        piPico(k);
    else
        paintUnknown(p, def.width, def.height, QString::fromStdString(def.type), colors);
    p->restore();
}

void paintUnknown(QPainter* p, double w, double h, const QString& type, const CanvasColors& colors)
{
    p->save();
    p->setPen(QPen(colors.gridDot.darker(150), 1, Qt::DashLine));
    p->setBrush(QColor(128, 128, 128, 40));
    p->drawRect(QRectF(0, 0, w, h));
    QFont f("Helvetica");
    f.setPixelSize(7);
    p->setFont(f);
    p->setPen(colors.partText);
    p->drawText(QRectF(2, 2, w - 4, h - 4), Qt::AlignCenter | Qt::TextWordWrap, type);
    p->restore();
}

} // namespace SymbolPainter
