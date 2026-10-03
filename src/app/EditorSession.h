#pragma once
// One open file (PLAN.md 4.10): its document, scene, view and undo stack.
// Tabs never share any of these; only the clipboard and preferences are global.
#include "core/Document.h"
#include "core/WirePath.h"

#include <map>
#include <string>
#include <vector>

#include <QGraphicsScene>
#include <QObject>
#include <QString>
#include <QUndoStack>

class SchematicView;

class EditorSession : public QObject {
    Q_OBJECT
public:
    explicit EditorSession(QObject* parent = nullptr);

    // Throws chiply::LoadError on failure.
    void load(const QString& path);
    void newDocument(const QString& author);
    // Throws std::runtime_error on failure. Empty path = current path.
    void save(const QString& path = {});

    const chiply::Document& document() const { return m_doc; }
    QString filePath() const { return m_path; }
    QString displayName() const;
    bool isModified() const { return !m_undo.isClean(); }
    QStringList loadWarnings() const { return m_warnings; }

    SchematicView* view() const { return m_view; }
    class MiniToolbar* miniToolbar() const { return m_mini; }
    QUndoStack* undoStack() { return &m_undo; }

    struct SelectionSummary {
        int parts = 0;      // includes text annotations
        int wires = 0;      // explicitly selected
        int implicitWires = 0; // both ends on selected parts
        int stretchWires = 0;  // one end on a selected part
        bool empty() const { return parts == 0 && wires == 0; }
    };
    SelectionSummary selectionSummary() const { return m_summary; }
    std::vector<std::string> selectedPartIds() const;

    // ---- part editing (all undoable) ----
    struct Placement {
        std::string id;
        double left = 0, top = 0;
        int rotate = 0;
    };
    // Interactive drag of the selected parts: begin, preview, end.
    void beginMove();
    void previewMove(double dx, double dy); // raw scene delta of the grabbed part
    void endMove(bool commit);
    bool moving() const { return !m_moveStart.empty(); }
    void setMoveGrab(const std::string& partId) { m_moveGrab = partId; }
    void setSnapMode(double grid) { m_snap = grid; } // 0 = no snapping
    // Keyboard nudge by whole grid steps; repeats of a held key merge.
    void nudgeSelection(int gx, int gy, bool autoRepeat);
    void rotateSelection();
    void deleteSelection();
    void duplicateSelection();

    // Property edits (undoable). renamePart returns an error message, or an
    // empty string on success.
    QString renamePart(const std::string& from, const std::string& to);
    void setPartAttr(const std::string& id, const std::string& key, const std::string& value);
    void setWireColor(int wireIndex, const std::string& color);

    // Adding parts: a translucent preview follows the cursor until placed.
    void startPlacing(const std::string& type);
    void placingMoved(QPointF scenePos);
    void placeAt(QPointF scenePos); // adds the part (undoable) and ends placing
    void cancelPlacing();
    bool placing() const { return m_ghost != nullptr; }

    struct WireChange {
        int index;
        chiply::WirePath path;
    };
    // Applies placements and wire paths without undo (used by commands).
    void applyPlacements(const std::vector<Placement>& ps);
    void applyWirePaths(const std::vector<WireChange>& ws);
    // Elastic re-routing for moving parts from `from` to `to` placements:
    // wires with exactly one end on a moving part keep their route except
    // the segment nearest that part along the move direction (PLAN.md 4.4).
    // Returns the new paths; `before` receives the current ones.
    std::vector<WireChange> elasticWires(const std::vector<Placement>& from, const std::vector<Placement>& to,
                                         std::vector<WireChange>* before) const;
    // Replaces the document wholesale and rebuilds, selecting `select`
    // (used by structural commands).
    void replaceDocument(const chiply::Document& doc, const std::vector<std::string>& select);
    const chiply::Document& doc() const { return m_doc; }

    // Replaces a wire's path (undoable through the session's undo stack).
    void editWireRoute(int wireIndex, const std::vector<chiply::Point>& route);
    // Applies a path without recording undo (used by the undo command).
    void applyWirePath(int wireIndex, const chiply::WirePath& path);
    std::vector<int> selectedWireIndices() const;

signals:
    void titleChanged();
    void selectionChanged();
    void documentChanged(); // any edit, including undo/redo

private:
    void rebuildScene();
    void updateSelectionState();
    void refreshWiresOf(const std::string& partId);
    void selectParts(const std::vector<std::string>& ids);
    Placement placementOf(const std::string& id) const;

    chiply::Document m_doc;
    QString m_path;
    QStringList m_warnings;
    QGraphicsScene m_scene;
    SchematicView* m_view = nullptr;
    QUndoStack m_undo;
    SelectionSummary m_summary;

    std::map<std::string, QGraphicsItem*> m_partItems;            // id -> PartItem/TextItem
    std::map<std::string, std::vector<class WireItem*>> m_wiresOf; // id -> attached wires
    QGraphicsItem* m_ghost = nullptr;
    class MiniToolbar* m_mini = nullptr;
    std::string m_placeType;
    std::vector<Placement> m_moveStart;
    std::vector<WireChange> m_moveWireStart; // paths of affected wires at drag start
    void applyPlacementsOnly(const std::vector<Placement>& ps); // document only
    void pushPlacement(const QString& text, const std::vector<Placement>& before,
                       const std::vector<Placement>& after, bool mergeable);
    std::string m_moveGrab;
    double m_snap = 9.6;
};
