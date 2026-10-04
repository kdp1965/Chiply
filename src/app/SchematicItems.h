#pragma once
// Scene items. They read the document; they never modify it (edits arrive
// as undo commands in later milestones and rebuild the affected items).
//
// Selection uses QGraphicsItem's selected flag; the view decides what gets
// selected (Wokwi modifiers), the items only draw it.
#include "core/Document.h"
#include "core/PartLibrary.h"

#include <QGraphicsItem>
#include <QGraphicsPathItem>
#include <QGraphicsSimpleTextItem>

#include <functional>

// A part drawn at Wokwi's placement: position = unrotated top-left,
// rotation about the outline center.
class PartItem : public QGraphicsItem {
public:
    PartItem(const chiply::Part& part, const chiply::PartDef* def);

    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

    const std::string& partId() const { return m_part.id; }
    static constexpr int Type = UserType + 1;
    int type() const override { return Type; }
    QPainterPath shape() const override;
    // The part's outline in scene coordinates (rotated bounds).
    QRectF outlineSceneRect() const;
    // Pin within the hit radius of a scene point, or null.
    const chiply::PinDef* pinAtScene(QPointF scenePos) const { return pinAt(mapFromScene(scenePos)); }
    // Scene position of one of this part's pins.
    QPointF pinScenePos(const chiply::PinDef& pin) const { return mapToScene(QPointF(pin.x, pin.y)); }
    const chiply::PartDef* def() const { return m_def; }
    // Simulation: live state for interactive/display parts (see
    // SymbolPainter::SimVisual), and a provider of pin values for tooltips.
    void setSim(bool active, unsigned bits);
    bool simActive() const { return m_simActive; }
    unsigned simBits() const { return m_simBits; }
    static void setPinValueProvider(std::function<QString(const std::string& part, const std::string& pin)> f);
    static void setShowNames(bool on);
    static bool showNames();
    // Follows a placement change (left/top/rotate) of the document part.
    void setPlacement(const chiply::Part& part);

protected:
    void hoverEnterEvent(QGraphicsSceneHoverEvent* event) override;
    void hoverMoveEvent(QGraphicsSceneHoverEvent* event) override;
    void hoverLeaveEvent(QGraphicsSceneHoverEvent* event) override;

private:
    const chiply::PinDef* pinAt(QPointF local) const;

    chiply::Part m_part;              // snapshot for painting
    const chiply::PartDef* m_def;     // null for unknown types
    double m_w, m_h;
    bool m_hovered = false;
    bool m_simActive = false;
    unsigned m_simBits = 0;
    const chiply::PinDef* m_hoverPin = nullptr;
};

// Yellow drag handle at the middle of a wire segment (constant screen size).
class SegmentHandle : public QGraphicsEllipseItem {
public:
    SegmentHandle(QGraphicsItem* parent, std::size_t segment, bool horizontal);
    static constexpr int Type = UserType + 4;
    int type() const override { return Type; }
    std::size_t segment() const { return m_segment; }
    bool horizontal() const { return m_horizontal; }

private:
    std::size_t m_segment;
    bool m_horizontal;
};

// Square handle on an interior corner of a selected wire.
class CornerHandle : public QGraphicsRectItem {
public:
    CornerHandle(QGraphicsItem* parent, std::size_t corner);
    static constexpr int Type = UserType + 5;
    int type() const override { return Type; }
    std::size_t corner() const { return m_corner; }

private:
    std::size_t m_corner;
};

// Round handle on a selected wire's end: drag onto another pin to reconnect.
class EndHandle : public QGraphicsEllipseItem {
public:
    EndHandle(QGraphicsItem* parent, bool atStart);
    static constexpr int Type = UserType + 6;
    int type() const override { return Type; }
    bool atStart() const { return m_atStart; }

private:
    bool m_atStart;
};

class WireItem : public QGraphicsPathItem {
public:
    // Implicit: not selected itself, but both ends are on selected parts, so
    // it travels with them. Stretch: exactly one end is on a selected part.
    enum class Link { None, Implicit, Stretch };

    WireItem(const chiply::Wire& wire, const std::vector<chiply::Point>& route, int index);
    static constexpr int Type = UserType + 2;
    int type() const override { return Type; }
    void restyle();
    int index() const { return m_index; }
    const std::string& fromPart() const { return m_fromPart; }
    const std::string& toPart() const { return m_toPart; }
    Link link() const { return m_link; }
    void setLink(Link l);
    // Segments (indices into route()) selected by a marquee: drawn with the
    // selection halo; they move with the selection.
    void setSelectedSegments(const std::vector<int>& segs);
    const std::vector<int>& selectedSegments() const { return m_selSegs; }
    // Simulation value of the wire's net: -1 = not simulating, else
    // 0 = low, 1 = high, 2 = X, 3 = Z (chiply::sim::V).
    void setSimValue(int v);
    int simValue() const { return m_simValue; }
    // LiveWire (toolbar): colour wires by their simulated value. When off,
    // wires keep their file colours; tooltips still show the value.
    static void setLiveWires(bool on);
    static bool liveWires();
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;
    QRectF boundingRect() const override;

    const std::vector<chiply::Point>& route() const { return m_route; }
    void setRoute(const std::vector<chiply::Point>& route); // committed route
    void showPreview(const std::vector<chiply::Point>& route); // during a drag
    void setHandlesVisible(bool on);
    // Visible part of the scene (inset by a margin): segment handles whose
    // midpoint is off-screen slide along their segment into it.
    void setVisibleRect(const QRectF& r);
    // Hit area is the drawn line plus a few pixels, not the area the route
    // encloses (QGraphicsPathItem's default), so wires never steal hover or
    // clicks from the parts they loop around; the pins at both ends are left
    // to the parts.
    QPainterPath shape() const override;

private:
    void rebuildHandles(const std::vector<chiply::Point>& route);

    QColor m_fileColor;
    std::vector<chiply::Point> m_route;
    std::vector<QGraphicsItem*> m_handles;
    bool m_handlesOn = false;
    QRectF m_visible;
    std::vector<chiply::Point> m_handleRoute; // route the handles were built for
    int m_index;
    std::string m_fromPart, m_toPart;
    Link m_link = Link::None;
    std::vector<int> m_selSegs;
    int m_simValue = -1;
    QString m_tip;
};

class TextItem : public QGraphicsSimpleTextItem {
public:
    explicit TextItem(const chiply::Part& part);
    static constexpr int Type = UserType + 3;
    int type() const override { return Type; }
    void restyle();
    const std::string& partId() const { return m_id; }
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;
    void setPlacement(const chiply::Part& part);

private:
    std::string m_id;
};

// Part id for part and text items, empty for anything else.
std::string itemPartId(const QGraphicsItem* item);
