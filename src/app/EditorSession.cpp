#include "EditorSession.h"

#include "SchematicView.h"
#include "Theme.h"
#include "SchematicItems.h"
#include "core/Geometry.h"
#include "core/WokwiJson.h"

#include <QFileInfo>

EditorSession::EditorSession(QObject* parent)
    : QObject(parent)
{
    m_scene.setItemIndexMethod(QGraphicsScene::BspTreeIndex);
    m_view = new SchematicView(&m_scene);
    connect(&m_undo, &QUndoStack::cleanChanged, this, &EditorSession::titleChanged);
    connect(&Theme::instance(), &Theme::changed, this, &EditorSession::rebuildScene);
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

void EditorSession::rebuildScene()
{
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
        m_scene.addItem(new WireItem(w, chiply::routePolyline(*a, *b, w.path)));
    }
    m_scene.setSceneRect(m_scene.itemsBoundingRect().adjusted(-2000, -2000, 2000, 2000));
}
