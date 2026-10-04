#pragma once
// One open file (PLAN.md 4.10): its document, scene, view and undo stack.
// Tabs never share any of these; only the clipboard and preferences are global.
#include "core/Document.h"
#include "core/Drc.h"
#include "core/Edit.h"
#include "core/WirePath.h"
#include "sim/Trace.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

#include <QGraphicsScene>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QUndoStack>

class SchematicView;

class EditorSession : public QObject {
    Q_OBJECT
public:
    explicit EditorSession(QObject* parent = nullptr);

    // Throws chiply::LoadError on failure.
    void load(const QString& path);
    void newDocument(const QString& author);
    // An untitled design that starts as a copy of Tiny Tapeout's Wokwi
    // template (built in; no file is opened). The author is cleared.
    void newFromTemplate();
    // Throws std::runtime_error on failure. Empty path = current path.
    void save(const QString& path = {});

    const chiply::Document& document() const { return m_doc; }
    QString filePath() const { return m_path; }
    QString displayName() const;
    // Edits, or DRC settings/waivers for the sidecar file, not yet saved.
    bool isModified() const { return !m_undo.isClean() || m_sidecarDirty; }
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
    // Paste name format ("r#_*", PLAN.md 4.7): paste, Duplicate and Alt-drag
    // step the number at '#' in matching ids. A global preference.
    static QString pasteNameFormat();
    static void setPasteNameFormat(const QString& format);
    static chiply::NameFormat pasteFormat();
    // Find and replace in the selected parts' names (and their wires);
    // undoable. Returns an error message, or "".
    QString replaceInNames(const QString& from, const QString& to, int* changed = nullptr);
    std::map<std::string, std::string> previewReplaceInNames(const QString& from, const QString& to, QString* error) const;
    void setPartAttr(const std::string& id, const std::string& key, const std::string& value);
    // Changes a part's type, keeping its id, place and attrs (undoable); used
    // to resize a RAM/ROM. Wires to pins the new type lacks are kept (DRC
    // reports them).
    void setPartType(const std::string& id, const std::string& type);
    // The design's folder (ROM files are read from it); "" when untitled.
    QString baseDir() const;
    void setWireColor(int wireIndex, const std::string& color);
    void setSelectedWiresColor(const std::string& color);
    void deleteWire(int wireIndex);
    // Moves one end of a wire to another pin, with the given route.
    void reanchorWire(int wireIndex, bool atStart, const chiply::PinRef& pin, const std::vector<chiply::Point>& route);
    // Adds a wire along an orthogonal polyline from `from` to `to` (undoable).
    void addWire(const chiply::PinRef& from, const chiply::PinRef& to, const std::string& color,
                 const std::vector<chiply::Point>& route);
    // Default color for a wire starting at this pin (Wokwi: GND black,
    // VCC red, otherwise green).
    std::string defaultWireColor(const chiply::PinRef& from) const;

    // ---- clipboard (PLAN.md 4.7) ----
    // Selected parts plus the wires between them, as Wokwi JSON text; empty
    // if nothing is selected.
    QString copySelection() const;
    void cutSelection(); // copy is done by the caller; this deletes
    struct PasteReport {
        int parts = 0;
        int renamed = 0;       // ids that changed
        int droppedWires = 0;  // wires to parts not in the fragment / skipped
        int skippedBlocks = 0; // Tiny Tapeout I/O blocks left out
        QString error;
    };
    // Tiny Tapeout I/O block types in `text` that this document already has.
    QStringList existingTtBlocksIn(const QString& text) const;
    // Pastes with the fragment's top-left at `anchor` (snapped); the pasted
    // parts are selected and follow the cursor until a click drops them
    // (Esc cancels). One undo step.
    PasteReport paste(const QString& text, QPointF anchor, bool skipExistingTtBlocks);
    bool pasteFloating() const { return m_pasteFloating; }
    // Copies the selection in place and selects the copy; returns the copy
    // of `grab` (Alt/Option+drag).
    std::string duplicateInPlace(const std::string& grab);

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
    // ---- simulation (PLAN.md 6.3) ----
    // Compiles and enters simulation mode (paused). `chip`: the chip in
    // another engine (Verilator), built from this document.
    void startSimulation(std::shared_ptr<chiply::sim::ChipBackend> chip = {});
    void stopSimulation();    // back to edit mode
    class SimRunner* sim() const { return m_sim; }
    bool simulating() const { return m_sim != nullptr; }

    // ---- traces (PLAN.md 6.4) ----
    // Probed pins ("flop30:Q"), shown in the Waveforms pane and written to
    // VCD. Remembered per file in the app settings.
    const QStringList& probes() const { return m_probes; }
    bool isProbed(const QString& pinRef) const { return m_probes.contains(pinRef); }
    void addProbe(const QString& pinRef);
    void removeProbe(const QString& pinRef);
    // The current run's recording, kept after Stop until the next run.
    std::shared_ptr<chiply::sim::Trace> trace() const { return m_trace; }
    chiply::sim::Time traceEnd() const; // current time while running
    // Writes the recording as VCD; returns an error message or "".
    QString writeTraceVcd(const QString& path) const;
    // Default VCD path: the logic analyzer's file name next to the diagram.
    QString defaultTracePath() const;

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
    void replaceDocument(const chiply::Document& doc, const std::vector<std::string>& select,
                         const std::vector<int>& selectWires = {});
    const chiply::Document& doc() const { return m_doc; }

    // Replaces a wire's path (undoable through the session's undo stack).
    void editWireRoute(int wireIndex, const std::vector<chiply::Point>& route);
    // Applies a path without recording undo (used by the undo command).
    void applyWirePath(int wireIndex, const chiply::WirePath& path);
    std::vector<int> selectedWireIndices() const;

    // ---- design rule checks (PLAN.md 5.2) ----
    // Live: every edit re-checks what it touched (debounced); otherwise only
    // runDrc(true) checks. A global preference.
    // Extended mode (PLAN.md 7.1): Chiply's own parts are offered. A global
    // preference; also tells DRC whether to flag extension parts.
    static bool extensionsEnabled();
    static void setExtensionsEnabled(bool on);
    void setExtensionsAllowed(bool on); // this session's DRC
    bool usesExtensionParts() const;
    bool usesBlocks() const;      // custom blocks (Verilator-only simulation)
    void refreshParts();          // part definitions changed (blocks reloaded)
    static bool drcLive();
    static void setDrcLive(bool on);
    void runDrc(bool full);
    const chiply::drc::Engine& drc() const { return m_drc; }
    const chiply::drc::Stats& drcStats() const { return m_drc.lastStats(); }
    // Current violations of the enabled checks (waived ones included).
    const std::vector<chiply::drc::Violation>& violations() const { return m_violations; }
    // Per document, saved in <file>.chiply.json next to the diagram.
    void setCheckEnabled(const std::string& check, bool on);
    bool isWaived(const std::string& key) const { return m_waivers.count(key) > 0; }
    QString waiverReason(const std::string& key) const;
    void waive(const std::string& key, const QString& reason);
    void unwaive(const std::string& key);
    // Counts that exclude waived violations.
    int unwaivedCount(chiply::drc::Severity s) const;
    // Centres and zooms the canvas on a violation, selects its parts and
    // highlights them (and its pins / net) with a short pulse.
    void showViolation(const chiply::drc::Violation& v);
    static QString sidecarPath(const QString& diagramPath);

signals:
    void drcChanged();
    void titleChanged();
    void selectionChanged();
    void documentChanged(); // any edit, including undo/redo
    void simulationChanged(); // started, stopped, running state or time
    void probesChanged();

private:
    void rebuildScene();
    void updateSelectionState();
    void refreshWiresOf(const std::string& partId);
    void selectParts(const std::vector<std::string>& ids, const std::vector<int>& wires = {});
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
    class SimRunner* m_sim = nullptr;
    chiply::drc::Engine m_drc;
    std::vector<chiply::drc::Violation> m_violations;
    QTimer m_drcTimer;
    std::map<std::string, QString> m_waivers;      // key -> reason
    std::map<std::string, bool> m_checkOverrides;  // check id -> on, where it differs from the default
    bool m_sidecarDirty = false;
    void loadSidecar();
    void saveSidecar();
    void refreshViolations();
    QStringList m_probes;
    std::shared_ptr<chiply::sim::Trace> m_trace;
    chiply::sim::Time m_traceEnd = 0;
    void loadProbes();
    void saveProbes() const;
    std::string m_simHeldButton;
    bool m_pasteFloating = false;
    QPointF m_pasteAnchor;
    class DocumentCommand* m_pasteCmd = nullptr;
    const QUndoCommand* m_pasteCmdBase = nullptr; // same object, for comparisons
    void finishPaste(bool keep);
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
