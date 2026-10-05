#include "SimRunner.h"

#include "EditorSession.h"
#include "SchematicItems.h"
#include "SchematicView.h"

#include <QGraphicsScene>

#include <algorithm>
#include <chrono>

using chiply::sim::Time;
using chiply::sim::V;

namespace {
SimRunner* s_tooltipOwner = nullptr; // whose values the pin tooltips show
constexpr int kTickMs = 16;          // ~60 updates per second
constexpr double kBudgetMs = 12.0;   // CPU time per tick for simulation
constexpr Time kChunk = 20'000'000;  // simulate in 20 us chunks within a tick
}

SimRunner::SimRunner(EditorSession* s, std::shared_ptr<chiply::sim::ChipBackend> chip)
    : QObject(s)
    , m_s(s)
    , m_top(s->document())
{
    try {
        m_doc = chiply::flattenSheets(m_top, chiply::PartLibrary::builtin(), &m_flat);
    } catch (const std::exception& e) {
        m_doc = m_top; // simulate what can be simulated, and say why not the rest
        m_flat = {};
        m_setupError = QString::fromUtf8(e.what());
    }
    m_nl = std::make_unique<chiply::Netlist>(chiply::Netlist::build(m_doc, chiply::PartLibrary::builtin()));
    chiply::sim::Options opt;
    opt.chip = std::move(chip);
    opt.baseDir = s->baseDir().toStdString(); // ROM files
    m_engine = opt.chip ? QString::fromStdString(opt.chip->name()) : tr("built-in");
    m_sim = std::make_unique<chiply::sim::Simulator>(*m_nl, opt);
    m_trace = std::make_shared<chiply::sim::Trace>();
    m_trace->addLogicAnalyzers(*m_sim);
    for (const QString& p : s->probes())
        addProbe(p);
    m_timer.setInterval(kTickMs);
    connect(&m_timer, &QTimer::timeout, this, &SimRunner::tick);
    PartItem::setPinValueProvider([this](const std::string& part, const std::string& pin) { return valueText(part, pin); });
    s_tooltipOwner = this;
    refresh();
}

SimRunner::~SimRunner()
{
    if (s_tooltipOwner == this) {
        PartItem::setPinValueProvider(nullptr);
        s_tooltipOwner = nullptr;
    }
}

QString SimRunner::engineName() const
{
    // Options are not exposed; the chip backend is the only engine choice.
    return m_engine;
}

void SimRunner::play()
{
    m_wall.restart();
    m_timer.start();
    emit changed();
}

void SimRunner::pause()
{
    m_timer.stop();
    m_speed = 0;
    emit changed();
}

void SimRunner::step()
{
    const auto clocks = m_sim->clocks();
    const Time period = clocks.empty() ? Time(1'000'000'000) : Time(1e12 / clocks.front().hz);
    m_sim->advance(period);
    refresh();
    emit changed();
}

void SimRunner::tick()
{
    // Advance by the wall time since the last tick (real time), within a
    // CPU budget so the UI stays responsive; report the achieved speed.
    const double wallMs = std::max(1.0, double(m_wall.restart()));
    const Time want = Time(wallMs * 1e9 * m_simPerWall);
    const auto t0 = std::chrono::steady_clock::now();
    Time done = 0;
    while (done < want) {
        const Time dt = std::min(kChunk, want - done);
        m_sim->advance(dt);
        done += dt;
        const double used = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        if (used > kBudgetMs)
            break;
    }
    m_speed = double(done) / (wallMs * 1e9);
    refresh();
    emit changed();
}

void SimRunner::pressButton(const std::string& partId, bool pressed)
{
    m_sim->setPressed(partId, pressed);
    refresh();
    emit changed();
}

void SimRunner::toggleSwitch(const std::string& partId, int index)
{
    if (auto on = m_sim->switchState(partId, index)) {
        m_sim->setSwitch(partId, index, !*on);
        refresh();
        emit changed();
    }
}

void SimRunner::togglePort(const std::string& partId)
{
    const int net = pinNet({partId, "P"});
    if (net < 0)
        return;
    auto it = m_portDrive.find(partId);
    const V next = (it != m_portDrive.end() && it->second == V::H) ? V::L : V::H;
    m_portDrive[partId] = next;
    m_sim->drive(net, next);
    m_sim->settle();
    refresh();
    emit changed();
}

bool SimRunner::key(const QString& text, bool pressed)
{
    if (text.isEmpty())
        return false;
    bool handled = false;
    for (const chiply::Part& p : m_doc.parts) {
        if (p.type != "wokwi-pushbutton")
            continue;
        const std::string k = p.attrs.value("key", std::string());
        if (!k.empty() && QString::fromStdString(k).compare(text, Qt::CaseInsensitive) == 0) {
            m_sim->setPressed(p.id, pressed);
            handled = true;
        }
    }
    if (handled) {
        refresh();
        emit changed();
    }
    return handled;
}

QString SimRunner::valueText(const std::string& part, const std::string& pin) const
{
    const int net = pinNet({part, pin});
    if (net < 0)
        return {};
    switch (m_sim->value(net)) {
    case V::L: return QStringLiteral("0");
    case V::H: return QStringLiteral("1");
    case V::X: return QStringLiteral("X (unknown)");
    case V::Z: return QStringLiteral("Z (floating)");
    }
    return {};
}

void SimRunner::addProbe(const QString& pinRef)
{
    const auto ref = chiply::PinRef::parse(pinRef.toStdString());
    if (ref)
        m_trace->add(*m_sim, "probes", pinRef.toStdString(), pinNet(*ref));
}

void SimRunner::refresh()
{
    m_trace->collect(*m_sim);
    QGraphicsScene* scene = m_s->view()->scene();
    for (QGraphicsItem* it : scene->items()) {
        if (it->type() == WireItem::Type) {
            auto* w = static_cast<WireItem*>(it);
            if (w->index() < 0 || w->index() >= int(m_top.wires.size()))
                continue;
            const chiply::Wire& wire = m_top.wires[size_t(w->index())];
            const int net = pinNet(wire.from);
            w->setSimValue(net < 0 ? -1 : int(m_sim->value(net)));
        } else if (it->type() == PartItem::Type) {
            auto* p = static_cast<PartItem*>(it);
            const std::string& id = p->partId();
            unsigned bits = 0;
            bool live = true;
            if (auto lit = m_sim->ledLit(id))
                bits = *lit ? 1u : 0u;
            else if (auto seg = m_sim->segments(id))
                bits = *seg;
            else if (auto st = m_sim->switchState(id, 0)) {
                bits = *st ? 1u : 0u;
                for (int i = 1; i < 8; ++i)
                    if (auto si = m_sim->switchState(id, i); si && *si)
                        bits |= 1u << i;
            } else if (p->def() && p->def()->type.rfind("wokwi-flip-flop", 0) == 0) {
                const auto q = m_sim->value(chiply::PinRef{id, "Q"});
                bits = q == chiply::sim::V::H ? 1u : (q == chiply::sim::V::L ? 0u : 2u);
            } else if (p->def() && chiply::isPortType(p->def()->type)) {
                bits = m_sim->value(pinNet({id, "P"})) == chiply::sim::V::H ? 1u : 0u; // filled when 1
            } else
                live = false;
            p->setSim(live, bits);
        }
    }
}

void SimRunner::clearVisuals()
{
    for (QGraphicsItem* it : m_s->view()->scene()->items()) {
        if (it->type() == WireItem::Type)
            static_cast<WireItem*>(it)->setSimValue(-1);
        else if (it->type() == PartItem::Type)
            static_cast<PartItem*>(it)->setSim(false, 0);
    }
}
