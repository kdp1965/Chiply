#include "EditorSession.h"

#include "SchematicView.h"
#include "Theme.h"
#include "SchematicItems.h"
#include "core/Edit.h"
#include "core/IdGen.h"
#include "core/Geometry.h"
#include "core/JsonFormat.h"
#include "core/WokwiJson.h"

#include <QCursor>
#include <QFileInfo>
#include <QSignalBlocker>

#include <cmath>
#include <set>

namespace {
constexpr int kMaxWiresWithHandles = 40;
}

EditorSession::EditorSession(QObject* parent)
    : QObject(parent)
{
    m_scene.setItemIndexMethod(QGraphicsScene::BspTreeIndex);
    m_view = new SchematicView(&m_scene);
    connect(&m_undo, &QUndoStack::cleanChanged, this, &EditorSession::titleChanged);
    connect(&Theme::instance(), &Theme::changed, this, &EditorSession::rebuildScene);
    connect(m_view, &SchematicView::selectionEdited, this, &EditorSession::updateSelectionState);
    connect(m_view, &SchematicView::wireRouteEdited, this, &EditorSession::editWireRoute);
    connect(m_view, &SchematicView::moveStarted, this, [this](const QString& grab) {
        setMoveGrab(grab.toStdString());
        beginMove();
    });
    connect(m_view, &SchematicView::moveUpdated, this, [this](QPointF d, double grid) {
        setSnapMode(grid);
        previewMove(d.x(), d.y());
    });
    connect(m_view, &SchematicView::moveEnded, this, &EditorSession::endMove);
    connect(m_view, &SchematicView::placeMoved, this, &EditorSession::placingMoved);
    connect(m_view, &SchematicView::placeClicked, this, &EditorSession::placeAt);
    connect(m_view, &SchematicView::placeCancelled, this, &EditorSession::cancelPlacing);
    connect(m_view, &SchematicView::nudgeRequested, this, &EditorSession::nudgeSelection);
    connect(m_view, &SchematicView::rotateRequested, this, &EditorSession::rotateSelection);
    connect(m_view, &SchematicView::deleteRequested, this, &EditorSession::deleteSelection);
    connect(m_view, &SchematicView::duplicateRequested, this, &EditorSession::duplicateSelection);
}

void EditorSession::load(const QString& path)
{
    chiply::LoadResult r = chiply::loadWokwiFile(path.toStdString());
    m_doc = std::move(r.doc);
    m_warnings.clear();
    for (const std::string& w : r.warnings)
        m_warnings << QString::fromStdString(w);
    m_path = path;
    m_undo.clear();
    rebuildScene();
    emit titleChanged();
}

void EditorSession::newDocument(const QString& author)
{
    m_doc = chiply::Document::makeEmpty(author.toStdString());
    m_path.clear();
    m_warnings.clear();
    m_undo.clear();
    rebuildScene();
    emit titleChanged();
}

void EditorSession::save(const QString& path)
{
    const QString target = path.isEmpty() ? m_path : path;
    chiply::saveWokwiFile(m_doc, target.toStdString());
    m_path = target;
    m_undo.setClean();
    emit titleChanged();
}

QString EditorSession::displayName() const
{
    return m_path.isEmpty() ? tr("Untitled") : QFileInfo(m_path).fileName();
}

namespace {

class SetWirePathCommand : public QUndoCommand {
public:
    SetWirePathCommand(EditorSession* s, int index, chiply::WirePath before, chiply::WirePath after)
        : QUndoCommand(QObject::tr("Reroute wire"))
        , m_s(s)
        , m_index(index)
        , m_before(std::move(before))
        , m_after(std::move(after))
    {
    }
    void undo() override { m_s->applyWirePath(m_index, m_before); }
    void redo() override { m_s->applyWirePath(m_index, m_after); }

private:
    EditorSession* m_s;
    int m_index;
    chiply::WirePath m_before, m_after;
};

} // namespace

void EditorSession::editWireRoute(int wireIndex, const std::vector<chiply::Point>& route)
{
    if (wireIndex < 0 || wireIndex >= int(m_doc.wires.size()))
        return;
    chiply::WirePath after = chiply::pathFromPolyline(route);
    m_undo.push(new SetWirePathCommand(this, wireIndex, m_doc.wires[size_t(wireIndex)].path, after));
}

void EditorSession::applyWirePath(int wireIndex, const chiply::WirePath& path)
{
    chiply::Wire& w = m_doc.wires[size_t(wireIndex)];
    w.path = path;
    w.rawPath.reset();
    w.hasPathElement = true;
    const chiply::PartLibrary& lib = chiply::PartLibrary::builtin();
    auto a = chiply::pinPosition(m_doc, lib, w.from);
    auto b = chiply::pinPosition(m_doc, lib, w.to);
    if (!a || !b)
        return;
    for (QGraphicsItem* it : m_scene.items()) {
        if (it->type() == WireItem::Type && static_cast<WireItem*>(it)->index() == wireIndex) {
            static_cast<WireItem*>(it)->setRoute(chiply::routePolyline(*a, *b, w.path));
            break;
        }
    }
}

// ---------------------------------------------------------------------------
// Part editing

class PlacementCommand : public QUndoCommand {
public:
    PlacementCommand(EditorSession* s, const QString& text, std::vector<EditorSession::Placement> before,
                     std::vector<EditorSession::Placement> after, bool mergeable)
        : QUndoCommand(text)
        , m_s(s)
        , m_before(std::move(before))
        , m_after(std::move(after))
        , m_mergeable(mergeable)
    {
    }
    void undo() override { m_s->applyPlacements(m_before); }
    void redo() override { m_s->applyPlacements(m_after); }
    int id() const override { return 1; }
    bool mergeWith(const QUndoCommand* other) override
    {
        auto* o = static_cast<const PlacementCommand*>(other);
        if (!o->m_mergeable || o->text() != text() || o->m_after.size() != m_after.size())
            return false;
        for (std::size_t i = 0; i < m_after.size(); ++i)
            if (o->m_after[i].id != m_after[i].id)
                return false;
        m_after = o->m_after;
        return true;
    }

private:
    EditorSession* m_s;
    std::vector<EditorSession::Placement> m_before, m_after;
    bool m_mergeable;
};

class DocumentCommand : public QUndoCommand {
public:
    DocumentCommand(EditorSession* s, const QString& text, chiply::Document before, chiply::Document after,
                    std::vector<std::string> selBefore, std::vector<std::string> selAfter)
        : QUndoCommand(text)
        , m_s(s)
        , m_before(std::move(before))
        , m_after(std::move(after))
        , m_selBefore(std::move(selBefore))
        , m_selAfter(std::move(selAfter))
    {
    }
    void undo() override { m_s->replaceDocument(m_before, m_selBefore); }
    void redo() override { m_s->replaceDocument(m_after, m_selAfter); }

private:
    EditorSession* m_s;
    chiply::Document m_before, m_after;
    std::vector<std::string> m_selBefore, m_selAfter;
};

EditorSession::Placement EditorSession::placementOf(const std::string& id) const
{
    const chiply::Part* p = m_doc.findPart(id);
    return p ? Placement{id, p->left, p->top, p->rotate} : Placement{id};
}

void EditorSession::applyPlacements(const std::vector<Placement>& ps)
{
    for (const Placement& pl : ps) {
        chiply::Part* p = m_doc.findPart(pl.id);
        if (!p)
            continue;
        p->left = pl.left;
        p->top = pl.top;
        p->rotate = pl.rotate;
        auto it = m_partItems.find(pl.id);
        if (it != m_partItems.end()) {
            if (it->second->type() == PartItem::Type)
                static_cast<PartItem*>(it->second)->setPlacement(*p);
            else if (it->second->type() == TextItem::Type)
                static_cast<TextItem*>(it->second)->setPlacement(*p);
        }
        refreshWiresOf(pl.id);
    }
    m_view->viewport()->update(); // group box follows
}

void EditorSession::refreshWiresOf(const std::string& partId)
{
    auto it = m_wiresOf.find(partId);
    if (it == m_wiresOf.end())
        return;
    const chiply::PartLibrary& lib = chiply::PartLibrary::builtin();
    for (WireItem* wi : it->second) {
        const chiply::Wire& w = m_doc.wires[size_t(wi->index())];
        auto a = chiply::pinPosition(m_doc, lib, w.from);
        auto b = chiply::pinPosition(m_doc, lib, w.to);
        if (a && b)
            wi->setRoute(chiply::routePolyline(*a, *b, w.path));
    }
}

void EditorSession::beginMove()
{
    m_moveStart.clear();
    for (const std::string& id : selectedPartIds())
        m_moveStart.push_back(placementOf(id));
    if (m_moveGrab.empty() && !m_moveStart.empty())
        m_moveGrab = m_moveStart.front().id;
}

void EditorSession::previewMove(double dx, double dy)
{
    if (m_moveStart.empty())
        return;
    // Snap the grabbed part's origin to the grid; everything moves by the
    // same delta so the group stays rigid.
    Placement grab = m_moveStart.front();
    for (const Placement& p : m_moveStart)
        if (p.id == m_moveGrab)
            grab = p;
    double nx = grab.left + dx, ny = grab.top + dy;
    if (m_snap > 0) {
        nx = std::round(nx / m_snap) * m_snap;
        ny = std::round(ny / m_snap) * m_snap;
    }
    const double ddx = nx - grab.left, ddy = ny - grab.top;
    std::vector<Placement> ps = m_moveStart;
    for (Placement& p : ps) {
        p.left = chiply::round2(p.left + ddx);
        p.top = chiply::round2(p.top + ddy);
    }
    applyPlacements(ps);
}

void EditorSession::endMove(bool commit)
{
    if (m_moveStart.empty())
        return;
    std::vector<Placement> after;
    for (const Placement& p : m_moveStart)
        after.push_back(placementOf(p.id));
    const std::vector<Placement> before = m_moveStart;
    m_moveStart.clear();
    m_moveGrab.clear();
    applyPlacements(before); // back to the start; the command re-applies
    bool changed = false;
    for (std::size_t i = 0; i < before.size(); ++i)
        changed |= before[i].left != after[i].left || before[i].top != after[i].top;
    if (commit && changed)
        m_undo.push(new PlacementCommand(this, tr("Move"), before, after, false));
}

void EditorSession::nudgeSelection(int gx, int gy, bool autoRepeat)
{
    const std::vector<std::string> ids = selectedPartIds();
    if (ids.empty())
        return;
    std::vector<Placement> before, after;
    for (const std::string& id : ids) {
        Placement p = placementOf(id);
        before.push_back(p);
        p.left = chiply::round2(p.left + gx * SchematicView::kGrid);
        p.top = chiply::round2(p.top + gy * SchematicView::kGrid);
        after.push_back(p);
    }
    m_undo.push(new PlacementCommand(this, tr("Nudge"), before, after, autoRepeat));
}

void EditorSession::rotateSelection()
{
    const std::vector<std::string> ids = selectedPartIds();
    if (ids.empty())
        return;
    std::vector<Placement> before, after;
    for (const std::string& id : ids) {
        Placement p = placementOf(id);
        before.push_back(p);
        p.rotate = (p.rotate + 90) % 360; // 90 degrees clockwise, as Wokwi's R
        after.push_back(p);
    }
    m_undo.push(new PlacementCommand(this, tr("Rotate"), before, after, false));
}

void EditorSession::deleteSelection()
{
    const std::vector<std::string> ids = selectedPartIds();
    const std::vector<int> wires = selectedWireIndices();
    if (ids.empty() && wires.empty())
        return;
    chiply::Document after = m_doc;
    chiply::removeItems(after, std::set<std::string>(ids.begin(), ids.end()),
                        std::set<std::size_t>(wires.begin(), wires.end()));
    m_undo.push(new DocumentCommand(this, tr("Delete"), m_doc, after, ids, {}));
}

void EditorSession::duplicateSelection()
{
    const std::vector<std::string> ids = selectedPartIds();
    if (ids.empty())
        return;
    chiply::Document after = m_doc;
    chiply::Fragment f = chiply::extractFragment(after, std::set<std::string>(ids.begin(), ids.end()));
    const double off = 2 * SchematicView::kGrid;
    std::vector<std::string> newIds = chiply::insertFragment(after, f, off, off);
    m_undo.push(new DocumentCommand(this, tr("Duplicate"), m_doc, after, ids, newIds));
}

namespace {
chiply::Part newPart(const std::string& type, const chiply::Document& doc)
{
    const chiply::PartDef* def = chiply::PartLibrary::builtin().find(type);
    chiply::Part p;
    p.type = type;
    p.id = chiply::nextFreeId(def ? def->prefix : chiply::idPrefixForType(type), chiply::usedIds(doc));
    if (def)
        p.attrs = def->attrs;
    if (type == "wokwi-text")
        p.attrs["text"] = "Text";
    return p;
}

// Top-left that puts the part's center under `c`, snapped to the grid.
QPointF placementOrigin(const chiply::Part& p, QPointF c)
{
    const chiply::PartDef* def = chiply::PartLibrary::builtin().find(p.type);
    const double w = def ? def->width : 38.4, h = def ? def->height : 38.4;
    const double g = SchematicView::kGrid;
    return QPointF(chiply::round2(std::round((c.x() - w / 2) / g) * g),
                   chiply::round2(std::round((c.y() - h / 2) / g) * g));
}
} // namespace

void EditorSession::startPlacing(const std::string& type)
{
    cancelPlacing();
    m_placeType = type;
    chiply::Part p = newPart(type, m_doc);
    if (type == "wokwi-text")
        m_ghost = new TextItem(p);
    else
        m_ghost = new PartItem(p, chiply::PartLibrary::builtin().find(type));
    m_ghost->setOpacity(0.55);
    m_ghost->setFlag(QGraphicsItem::ItemIsSelectable, false);
    m_ghost->setAcceptHoverEvents(false);
    m_ghost->setZValue(20);
    m_scene.addItem(m_ghost);
    m_view->setPlacing(true);
    const QPoint cur = m_view->viewport()->mapFromGlobal(QCursor::pos());
    placingMoved(m_view->mapToScene(m_view->viewport()->rect().contains(cur) ? cur : m_view->viewport()->rect().center()));
}

void EditorSession::placingMoved(QPointF scenePos)
{
    if (!m_ghost)
        return;
    chiply::Part p;
    p.type = m_placeType;
    m_ghost->setPos(placementOrigin(p, scenePos));
}

void EditorSession::placeAt(QPointF scenePos)
{
    if (!m_ghost)
        return;
    chiply::Part p = newPart(m_placeType, m_doc);
    const QPointF o = placementOrigin(p, scenePos);
    p.left = o.x();
    p.top = o.y();
    cancelPlacing();
    chiply::Document after = m_doc;
    after.parts.push_back(p);
    m_undo.push(new DocumentCommand(this, tr("Add %1").arg(QString::fromStdString(p.id)), m_doc, after,
                                    selectedPartIds(), {p.id}));
}

void EditorSession::cancelPlacing()
{
    if (m_ghost) {
        m_scene.removeItem(m_ghost);
        delete m_ghost;
        m_ghost = nullptr;
    }
    m_view->setPlacing(false);
}

void EditorSession::replaceDocument(const chiply::Document& doc, const std::vector<std::string>& select)
{
    m_doc = doc;
    {
        const QSignalBlocker block(&m_scene);
        m_scene.clearSelection();
    }
    rebuildScene();
    selectParts(select);
}

void EditorSession::selectParts(const std::vector<std::string>& ids)
{
    {
        const QSignalBlocker block(&m_scene);
        m_scene.clearSelection();
        for (const std::string& id : ids) {
            auto it = m_partItems.find(id);
            if (it != m_partItems.end())
                it->second->setSelected(true);
        }
    }
    updateSelectionState();
    m_view->viewport()->update();
}

std::vector<std::string> EditorSession::selectedPartIds() const
{
    std::vector<std::string> ids;
    for (const QGraphicsItem* it : m_scene.selectedItems()) {
        std::string id = itemPartId(it);
        if (!id.empty())
            ids.push_back(std::move(id));
    }
    return ids;
}

std::vector<int> EditorSession::selectedWireIndices() const
{
    std::vector<int> out;
    for (const QGraphicsItem* it : m_scene.selectedItems())
        if (it->type() == WireItem::Type)
            out.push_back(static_cast<const WireItem*>(it)->index());
    return out;
}

void EditorSession::updateSelectionState()
{
    std::set<std::string> parts;
    SelectionSummary s;
    for (const QGraphicsItem* it : m_scene.selectedItems()) {
        std::string id = itemPartId(it);
        if (!id.empty()) {
            parts.insert(std::move(id));
            ++s.parts;
        } else if (it->type() == WireItem::Type) {
            ++s.wires;
        }
    }
    for (QGraphicsItem* it : m_scene.items()) {
        if (it->type() != WireItem::Type)
            continue;
        auto* w = static_cast<WireItem*>(it);
        // Segment handles on explicitly selected wires (not on mass selections).
        w->setHandlesVisible(w->isSelected() && s.wires <= kMaxWiresWithHandles);
        WireItem::Link link = WireItem::Link::None;
        if (!w->isSelected()) {
            const bool a = parts.count(w->fromPart()), b = parts.count(w->toPart());
            if (a && b) {
                link = WireItem::Link::Implicit;
                ++s.implicitWires;
            } else if (a || b) {
                link = WireItem::Link::Stretch;
                ++s.stretchWires;
            }
        }
        w->setLink(link);
    }
    m_summary = s;
    emit selectionChanged();
}

void EditorSession::rebuildScene()
{
    if (m_ghost) { // the scene is about to be cleared
        m_ghost = nullptr;
        m_view->setPlacing(false);
    }
    // Keep the selection across rebuilds (theme change, reload of items).
    const std::vector<std::string> keepParts = selectedPartIds();
    const std::vector<int> keepWires = selectedWireIndices();
    m_scene.clear();
    const chiply::PartLibrary& lib = chiply::PartLibrary::builtin();
    m_partItems.clear();
    m_wiresOf.clear();
    for (const chiply::Part& p : m_doc.parts) {
        QGraphicsItem* item;
        if (p.type == "wokwi-text")
            item = new TextItem(p);
        else
            item = new PartItem(p, lib.find(p.type));
        m_scene.addItem(item);
        m_partItems[p.id] = item;
    }
    for (const chiply::Wire& w : m_doc.wires) {
        auto a = chiply::pinPosition(m_doc, lib, w.from);
        auto b = chiply::pinPosition(m_doc, lib, w.to);
        if (!a || !b) {
            // Unknown part or pin: anchor at the part's origin so the wire is
            // still visible (and the problem obvious).
            auto origin = [&](const chiply::PinRef& r) {
                const chiply::Part* p = m_doc.findPart(r.part);
                return p ? chiply::Point{p->left, p->top} : chiply::Point{};
            };
            if (!a)
                a = origin(w.from);
            if (!b)
                b = origin(w.to);
        }
        const int index = int(&w - m_doc.wires.data());
        auto* wi = new WireItem(w, chiply::routePolyline(*a, *b, w.path), index);
        m_scene.addItem(wi);
        m_wiresOf[w.from.part].push_back(wi);
        if (w.to.part != w.from.part)
            m_wiresOf[w.to.part].push_back(wi);
    }
    if (!keepParts.empty() || !keepWires.empty()) {
        const std::set<std::string> ps(keepParts.begin(), keepParts.end());
        const std::set<int> ws(keepWires.begin(), keepWires.end());
        const QSignalBlocker block(&m_scene);
        for (QGraphicsItem* it : m_scene.items()) {
            const std::string id = itemPartId(it);
            if ((!id.empty() && ps.count(id))
                || (it->type() == WireItem::Type && ws.count(static_cast<WireItem*>(it)->index())))
                it->setSelected(true);
        }
    }
    updateSelectionState();
    m_scene.setSceneRect(m_scene.itemsBoundingRect().adjusted(-2000, -2000, 2000, 2000));
}
