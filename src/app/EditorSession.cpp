#include "EditorSession.h"

#include "SchematicView.h"
#include "Theme.h"
#include "SchematicItems.h"
#include "core/Geometry.h"
#include "core/WokwiJson.h"

#include <QFileInfo>
#include <QSignalBlocker>

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
    // Keep the selection across rebuilds (theme change, reload of items).
    const std::vector<std::string> keepParts = selectedPartIds();
    const std::vector<int> keepWires = selectedWireIndices();
    m_scene.clear();
    const chiply::PartLibrary& lib = chiply::PartLibrary::builtin();
    for (const chiply::Part& p : m_doc.parts) {
        if (p.type == "wokwi-text") {
            m_scene.addItem(new TextItem(p));
            continue;
        }
        m_scene.addItem(new PartItem(p, lib.find(p.type)));
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
        m_scene.addItem(new WireItem(w, chiply::routePolyline(*a, *b, w.path), index));
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
