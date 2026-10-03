#pragma once
// Scene items. They read the document; they never modify it (edits arrive
// as undo commands in later milestones and rebuild the affected items).
#include "core/Document.h"
#include "core/PartLibrary.h"

#include <QGraphicsItem>
#include <QGraphicsPathItem>
#include <QGraphicsSimpleTextItem>

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
    const chiply::PinDef* m_hoverPin = nullptr;
};

class WireItem : public QGraphicsPathItem {
public:
    WireItem(const chiply::Wire& wire, const std::vector<chiply::Point>& route);
    static constexpr int Type = UserType + 2;
    int type() const override { return Type; }
    void restyle();
    // Hit area is the drawn line plus a few pixels, not the area the route
    // encloses (QGraphicsPathItem's default), so wires never steal hover or
    // clicks from the parts they loop around; the pins at both ends are left
    // to the parts.
    QPainterPath shape() const override;

private:
    QColor m_fileColor;
};

class TextItem : public QGraphicsSimpleTextItem {
public:
    explicit TextItem(const chiply::Part& part);
    static constexpr int Type = UserType + 3;
    int type() const override { return Type; }
    void restyle();
};
