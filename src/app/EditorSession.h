#pragma once
// One open file (PLAN.md 4.10): its document, scene, view and undo stack.
// Tabs never share any of these; only the clipboard and preferences are global.
#include "core/Document.h"

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
    std::vector<int> selectedWireIndices() const;

signals:
    void titleChanged();
    void selectionChanged();

private:
    void rebuildScene();
    void updateSelectionState();

    chiply::Document m_doc;
    QString m_path;
    QStringList m_warnings;
    QGraphicsScene m_scene;
    SchematicView* m_view = nullptr;
    QUndoStack m_undo;
    SelectionSummary m_summary;
};
