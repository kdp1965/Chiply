#include "EditorSession.h"

#include "SchematicView.h"
#include "Theme.h"
#include "SchematicItems.h"
#include "core/Geometry.h"
#include "core/WokwiJson.h"

#include <QFileInfo>
#include <QSignalBlocker>

#include <set>

EditorSession::EditorSession(QObject* parent)
    : QObject(parent)
{
    m_scene.setItemIndexMethod(QGraphicsScene::BspTreeIndex);
    m_view = new SchematicView(&m_scene);
    connect(&m_undo, &QUndoStack::cleanChanged, this, &EditorSession::titleChanged);
    connect(&Theme::instance(), &Theme::changed, this, &EditorSession::rebuildScene);
    connect(m_view, &SchematicView::selectionEdited, this, &EditorSession::updateSelectionState);
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
