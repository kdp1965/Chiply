#include "MiniToolbar.h"

#include "EditorSession.h"
#include "SchematicItems.h"
#include "SchematicView.h"
#include "Theme.h"

#include <QGraphicsScene>
#include <QHBoxLayout>
#include <QToolButton>

MiniToolbar::MiniToolbar(EditorSession* s, QWidget* viewport)
    : QWidget(viewport)
    , m_s(s)
{
    setAttribute(Qt::WA_StyledBackground);
    setObjectName("miniToolbar");
    setStyleSheet("#miniToolbar { background: #2196f3; border-radius: 6px; }"
                  "QToolButton { color: white; font-size: 15pt; padding: 3px 8px; border: none; }"
                  "QToolButton:hover { background: rgba(255,255,255,0.25); border-radius: 4px; }");
    auto* lay = new QHBoxLayout(this);
    lay->setContentsMargins(4, 2, 4, 2);
    lay->setSpacing(2);
    auto add = [&](const QString& text, const QString& tip) {
        auto* b = new QToolButton(this);
        b->setText(text);
        b->setToolTip(tip);
        lay->addWidget(b);
        return b;
    };
    connect(add(QString::fromUtf8("↻"), tr("Rotate (R)")), &QToolButton::clicked, this, [this] { m_s->rotateSelection(); });
    connect(add(QString::fromUtf8("✎"), tr("Edit in Inspector (F2)")), &QToolButton::clicked, this, &MiniToolbar::editRequested);
    connect(add(QString::fromUtf8("✕"), tr("Delete (Del)")), &QToolButton::clicked, this, [this] { m_s->deleteSelection(); });
    hide();
}

void MiniToolbar::reposition()
{
    const std::vector<std::string> ids = m_s->selectedPartIds();
    SchematicView* v = m_s->view();
    if (ids.size() != 1 || !m_s->selectedWireIndices().empty() || m_s->moving()) {
        hide();
        return;
    }
    QRectF r;
    for (QGraphicsItem* it : v->scene()->selectedItems()) {
        if (it->type() == PartItem::Type)
            r = static_cast<PartItem*>(it)->outlineSceneRect();
        else if (it->type() == TextItem::Type)
            r = it->sceneBoundingRect();
    }
    if (r.isNull()) {
        hide();
        return;
    }
    adjustSize();
    const QRect vr = v->mapFromScene(r).boundingRect();
    int x = vr.center().x() - width() / 2;
    int y = vr.top() - height() - 10;
    if (y < 2)
        y = vr.bottom() + 10;
    move(std::clamp(x, 2, std::max(2, parentWidget()->width() - width() - 2)), y);
    show();
    raise();
}
