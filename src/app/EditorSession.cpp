#include "EditorSession.h"

#include "SchematicView.h"
#include "Theme.h"
#include "core/IdGen.h"
#include "core/WokwiJson.h"

#include <QFileInfo>
#include <QGraphicsRectItem>
#include <QGraphicsSimpleTextItem>
#include <QPen>

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
    // Placeholder rendering until the part library lands (milestone M2):
    // each part is a small box at its top/left labelled with its id, so a
    // loaded design can already be navigated. Wires are not drawn yet.
    const CanvasColors& colors = Theme::instance().canvas();
    const QPen pen(colors.partStroke, 0);
    const QBrush brush(colors.partFill);
    const double s = SchematicView::kGrid * 4;
    for (const chiply::Part& p : m_doc.parts) {
        auto* box = m_scene.addRect(QRectF(0, 0, s, s), pen, brush);
        box->setPos(p.left, p.top);
        box->setToolTip(QString::fromStdString(p.id + "  (" + p.type + ")"));
        auto* label = new QGraphicsSimpleTextItem(QString::fromStdString(p.id), box);
        QFont f = label->font();
        f.setPointSizeF(5);
        label->setFont(f);
        label->setBrush(colors.partText);
        label->setPos(1, 1);
    }
    m_scene.setSceneRect(m_scene.itemsBoundingRect().adjusted(-2000, -2000, 2000, 2000));
}
