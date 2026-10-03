#pragma once
// Runs the built-in simulator for one tab (PLAN.md 6.3): real-time pacing,
// interactive parts, and pushing live values to the scene.
#include "core/Document.h"
#include "core/Netlist.h"
#include "sim/Simulator.h"
#include "sim/Trace.h"

#include <QElapsedTimer>
#include <QObject>
#include <QTimer>

#include <map>
#include <memory>
#include <vector>

class EditorSession;
class PartItem;
class WireItem;

class SimRunner : public QObject {
    Q_OBJECT
public:
    // Compiles the session's current document. The document is copied, so
    // the simulation never changes the file.
    // `chip`: another engine for the chip (Verilator, PLAN.md 6.6); null =
    // built-in.
    explicit SimRunner(EditorSession* s, std::shared_ptr<chiply::sim::ChipBackend> chip = {});
    QString engineName() const;
    ~SimRunner() override;

    void play();
    void pause();
    bool running() const { return m_timer.isActive(); }
    // One period of the first clock generator (or 1 ms without one).
    void step();

    // Interaction (from clicks on the canvas or keys).
    void pressButton(const std::string& partId, bool pressed);
    void toggleSwitch(const std::string& partId, int index);
    // Pushbuttons whose "key" attr matches; true if one was handled.
    bool key(const QString& text, bool pressed);

    chiply::sim::Time now() const { return m_sim->now(); }
    double speed() const { return m_speed; }       // simulated / real time
    QString valueText(const std::string& part, const std::string& pin) const;
    QString error() const { return QString::fromStdString(m_sim->lastError()); }
    chiply::sim::Simulator& simulator() { return *m_sim; }

    // Recording (PLAN.md 6.4): logic analyzers and the session's probes.
    std::shared_ptr<chiply::sim::Trace> trace() const { return m_trace; }
    void addProbe(const QString& pinRef);
    void collect() { m_trace->collect(*m_sim); }

    // Pushes current values to wires and parts.
    void refresh();
    // Restores the scene to edit-mode drawing.
    void clearVisuals();

signals:
    void changed(); // time / state advanced (for the status bar)

private:
    void tick();

    EditorSession* m_s;
    chiply::Document m_doc;
    std::unique_ptr<chiply::Netlist> m_nl;
    std::unique_ptr<chiply::sim::Simulator> m_sim;
    std::shared_ptr<chiply::sim::Trace> m_trace;
    QString m_engine;
    QTimer m_timer;
    QElapsedTimer m_wall;
    double m_simPerWall = 1.0; // pacing: 1 = real time
    double m_speed = 0;
    std::map<std::string, unsigned> m_keysDown;
};
