#include "WaveformView.h"

#include "EditorSession.h"
#include "SimRunner.h"
#include "Theme.h"

#include <QContextMenuEvent>
#include <QLocale>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

using chiply::sim::Time;
using chiply::sim::TraceSignal;
using chiply::sim::V;

namespace {
constexpr Time kMinSpan = 1000;                  // 1 ns
constexpr Time kMaxSpan = Time(3600) * 1'000'000'000'000; // an hour
constexpr int kAxisRows = 1;                     // time axis above the rows
}

QString formatTime(Time ps)
{
    if (ps == 0)
        return QStringLiteral("0");
    const double a = std::abs(double(ps));
    struct U {
        double scale;
        const char* name;
    };
    static const U units[] = {{1e12, "s"}, {1e9, "ms"}, {1e6, "us"}, {1e3, "ns"}, {1, "ps"}};
    for (const U& u : units)
        if (a >= u.scale) {
            const double v = double(ps) / u.scale;
            return QString::number(v, 'g', std::abs(v) >= 100 ? 4 : 3) + QLatin1Char(' ') + QLatin1String(u.name);
        }
    return QString::number(ps) + QStringLiteral(" ps");
}

QString formatTick(Time ps, Time step)
{
    // In the step's unit, so neighbouring ticks always differ:
    // "1 500 020 us" rather than "1.5 s" six times.
    struct U {
        Time scale;
        const char* name;
    };
    static const U units[] = {{1'000'000'000'000, "s"}, {1'000'000'000, "ms"}, {1'000'000, "us"}, {1'000, "ns"}, {1, "ps"}};
    for (const U& u : units)
        if (step >= u.scale || u.scale == 1) {
            const double v = double(ps) / double(u.scale);
            const int decimals = step % u.scale == 0 ? 0 : 1;
            QLocale loc(QLocale::English);
            loc.setNumberOptions(QLocale::DefaultNumberOptions);
            QString num = loc.toString(v, 'f', decimals).replace(QLatin1Char(','), QChar(0x2009)); // thin space
            return num + QLatin1Char(' ') + QLatin1String(u.name);
        }
    return QString::number(ps);
}

WaveformView::WaveformView(QWidget* parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setFocusPolicy(Qt::ClickFocus);
    QFont f = font();
    f.setPointSize(std::max(f.pointSize(), 15)); // readable labels
    setFont(f);
    setAttribute(Qt::WA_OpaquePaintEvent);
}

void WaveformView::setSession(EditorSession* s)
{
    if (s == m_session)
        return;
    disconnect(m_simConn);
    disconnect(m_probeConn);
    m_session = s;
    m_span = 0;
    m_follow = true;
    if (s) {
        m_simConn = connect(s, &EditorSession::simulationChanged, this, [this] {
            syncHeight();
            update();
        });
        m_probeConn = connect(s, &EditorSession::probesChanged, this, [this] {
            syncHeight();
            update();
        });
    }
    syncHeight();
    update();
}

void WaveformView::syncHeight()
{
    auto t = trace();
    const int rows = t ? int(t->channels().size()) : 0;
    setMinimumHeight(rows ? rowHeight() * (rows + kAxisRows) + 6 : 80);
}

std::shared_ptr<chiply::sim::Trace> WaveformView::trace() const
{
    return m_session ? m_session->trace() : nullptr;
}

Time WaveformView::endTime() const
{
    return m_session ? m_session->traceEnd() : 0;
}

Time WaveformView::defaultSpan() const
{
    // Ten periods of the first clock generator, else 1 ms.
    if (m_session && m_session->sim()) {
        const auto clocks = m_session->sim()->simulator().clocks();
        if (!clocks.empty() && clocks.front().hz > 0)
            return std::clamp(Time(10e12 / clocks.front().hz), kMinSpan, kMaxSpan);
    }
    return 1'000'000'000;
}

Time WaveformView::viewStart() const
{
    const Time span = m_span ? m_span : defaultSpan();
    if (!m_follow)
        return m_start;
    auto t = trace();
    const Time first = t ? t->start() : 0;
    return std::max(first, endTime() - span);
}

int WaveformView::rowHeight() const
{
    return fontMetrics().height() * 2;
}

QString WaveformView::rowLabel(int i) const
{
    auto t = trace();
    const TraceSignal& s = t->channels()[size_t(i)];
    return s.scope == "probes" ? QString::fromStdString(s.name) : QString::fromStdString(s.scope + "." + s.name);
}

int WaveformView::labelWidth() const
{
    int w = fontMetrics().horizontalAdvance(QStringLiteral("logic1.D0 = 0"));
    if (auto t = trace())
        for (std::size_t i = 0; i < t->channels().size(); ++i)
            w = std::max(w, fontMetrics().horizontalAdvance(rowLabel(int(i)) + QStringLiteral(" = Z")));
    return w + 24;
}

int WaveformView::rowAt(int y) const
{
    auto t = trace();
    if (!t)
        return -1;
    const int r = y / rowHeight() - kAxisRows;
    return (r >= 0 && size_t(r) < t->channels().size()) ? r : -1;
}

double WaveformView::xOf(Time t, Time start, int plotWidth) const
{
    const Time span = m_span ? m_span : defaultSpan();
    return double(t - start) * plotWidth / double(span);
}

void WaveformView::zoomBy(double factor, double anchorFraction)
{
    const Time span = m_span ? m_span : defaultSpan();
    const Time start = viewStart();
    const Time anchor = start + Time(double(span) * anchorFraction);
    const Time newSpan = std::clamp(Time(double(span) / factor), kMinSpan, kMaxSpan);
    m_span = newSpan;
    // Zooming at the live edge keeps following; anywhere else pins the view.
    if (!(m_follow && anchorFraction >= 0.98)) {
        m_follow = false;
        m_start = anchor - Time(double(newSpan) * anchorFraction);
    }
    update();
}

void WaveformView::fit()
{
    auto t = trace();
    const Time first = t ? t->start() : 0;
    m_span = std::clamp(endTime() - first, kMinSpan, kMaxSpan);
    m_start = first;
    m_follow = false;
    update();
}

void WaveformView::follow()
{
    m_follow = true;
    update();
}

void WaveformView::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    const CanvasColors& c = Theme::instance().canvas();
    const QColor bg = palette().color(QPalette::Base);
    const QColor fg = palette().color(QPalette::Text);
    p.fillRect(rect(), bg);
    auto t = trace();
    if (!t || t->channels().empty()) {
        p.setPen(fg);
        p.drawText(rect().adjusted(16, 8, -16, -8), Qt::AlignCenter | Qt::TextWordWrap,
                   tr("Right-click a wire or pin and choose Probe to watch it here.\n"
                      "Logic analyzer parts are recorded automatically while simulating."));
        return;
    }
    const auto& sig = t->channels();
    const int rh = rowHeight();
    const int lw = labelWidth();
    const int pw = std::max(10, width() - lw - 8);
    const Time span = m_span ? m_span : defaultSpan();
    const Time start = viewStart();
    const Time end = endTime();
    const Time cursorT = m_hoverX >= lw ? start + Time(double(m_hoverX - lw) * double(span) / pw) : -1;

    // Time axis with "nice" tick steps.
    p.setPen(fg);
    const double rough = double(span) / std::max(1, pw / 140);
    const double mag = std::pow(10.0, std::floor(std::log10(std::max(1.0, rough))));
    double step = mag;
    for (double m : {1.0, 2.0, 5.0, 10.0})
        if (mag * m >= rough) {
            step = mag * m;
            break;
        }
    QColor grid = fg;
    grid.setAlphaF(0.18);
    const Time tick = std::max<Time>(1, Time(step));
    for (Time tt = (start / tick) * tick; tt <= start + span; tt += tick) {
        if (tt < start)
            continue;
        const int x = lw + int(xOf(tt, start, pw));
        p.setPen(grid);
        p.drawLine(x, rh - 4, x, height());
        p.setPen(fg);
        p.drawText(QRect(x + 3, 0, 260, rh - 4), Qt::AlignLeft | Qt::AlignVCenter, formatTick(tt, tick));
    }

    QColor hiFill = c.selection;
    hiFill.setAlphaF(0.22);
    const QColor wave = fg;
    const QColor xColor(0xe5, 0x39, 0x35);
    for (std::size_t i = 0; i < sig.size(); ++i) {
        const int top = rh * int(i + kAxisRows);
        const int yHi = top + 6, yLo = top + rh - 6, yMid = top + rh / 2;
        if (i % 2)
            p.fillRect(QRect(0, top, width(), rh), QColor(fg.red(), fg.green(), fg.blue(), 12));
        // Label with the value at the cursor (or the latest value).
        const TraceSignal& s = sig[i];
        const V shown = s.at(cursorT >= 0 ? std::min(cursorT, end) : end);
        p.setPen(fg);
        p.drawText(QRect(8, top, lw - 12, rh), Qt::AlignLeft | Qt::AlignVCenter,
                   rowLabel(int(i)) + QStringLiteral(" = ") + QChar(chiply::sim::toChar(shown)));

        // Walk the changes inside the window; several changes within one
        // pixel draw a full-height bar.
        p.save();
        p.setClipRect(QRect(lw, top, pw + 1, rh));
        auto drawLevel = [&](double x0, double x1, V v) {
            if (x1 <= x0)
                return;
            switch (v) {
            case V::H:
                p.fillRect(QRectF(x0, yHi, x1 - x0, yLo - yHi), hiFill);
                p.setPen(QPen(wave, 2.5));
                p.drawLine(QPointF(x0, yHi), QPointF(x1, yHi));
                break;
            case V::L:
                p.setPen(QPen(wave, 2.5));
                p.drawLine(QPointF(x0, yLo), QPointF(x1, yLo));
                break;
            case V::X:
                p.fillRect(QRectF(x0, yHi, x1 - x0, yLo - yHi), QBrush(xColor, Qt::BDiagPattern));
                p.setPen(QPen(xColor, 2));
                p.drawRect(QRectF(x0, yHi, x1 - x0, yLo - yHi));
                break;
            case V::Z:
                p.setPen(QPen(wave, 2, Qt::DashLine));
                p.drawLine(QPointF(x0, yMid), QPointF(x1, yMid));
                break;
            }
        };
        const auto& sm = s.samples;
        auto it = std::upper_bound(sm.begin(), sm.end(), start,
                                   [](Time x, const std::pair<Time, V>& e) { return x < e.first; });
        V cur = V::Z;
        double x = lw;
        if (it != sm.begin()) {
            cur = std::prev(it)->second;
        } else if (!sm.empty()) { // recording starts inside the window
            cur = sm.front().second;
            x = lw + xOf(sm.front().first, start, pw);
            ++it;
        }
        const double xEnd = lw + std::min(double(pw), xOf(end, start, pw));
        bool busy = false;
        double busyX = 0;
        for (; it != sm.end() && it->first <= start + span && it->first <= end; ++it) {
            const double nx = lw + xOf(it->first, start, pw);
            if (nx - x < 1.0) {
                if (!busy)
                    busyX = x;
                busy = true;
                cur = it->second;
                continue;
            }
            if (busy) {
                p.setPen(QPen(wave, 1));
                p.fillRect(QRectF(busyX, yHi, std::max(1.0, x - busyX + 1), yLo - yHi), wave);
                busy = false;
            }
            drawLevel(x, nx, cur);
            p.setPen(QPen(wave, 2));
            p.drawLine(QPointF(nx, yHi), QPointF(nx, yLo)); // the edge
            x = nx;
            cur = it->second;
        }
        if (busy)
            p.fillRect(QRectF(busyX, yHi, std::max(1.0, x - busyX + 1), yLo - yHi), wave);
        drawLevel(x, xEnd, cur);
        p.restore();
    }

    // Hover cursor.
    if (cursorT >= 0) {
        QColor cc = c.selection;
        p.setPen(QPen(cc, 2));
        p.drawLine(m_hoverX, 0, m_hoverX, height());
        const QString label = formatTime(cursorT);
        const int w = fontMetrics().horizontalAdvance(label) + 12;
        const QRect r(std::min(m_hoverX + 4, width() - w), 0, w, rh - 4);
        p.fillRect(r, bg);
        p.setPen(fg);
        p.drawText(r, Qt::AlignCenter, label);
    }
    if (!m_follow) {
        p.setPen(fg);
        p.drawText(QRect(0, 0, lw - 8, rh - 4), Qt::AlignLeft | Qt::AlignVCenter, tr("End: follow"));
    }
}

void WaveformView::wheelEvent(QWheelEvent* e)
{
    const int lw = labelWidth();
    const int pw = std::max(10, width() - lw - 8);
    const QPoint d = e->angleDelta();
    if ((e->modifiers() & Qt::ShiftModifier) || std::abs(d.x()) > std::abs(d.y())) {
        const int delta = std::abs(d.x()) > std::abs(d.y()) ? d.x() : d.y();
        const Time span = m_span ? m_span : defaultSpan();
        m_start = viewStart() - Time(double(span) * delta / 1200.0);
        m_span = span;
        m_follow = false;
        update();
    } else if (d.y()) {
        const double frac = std::clamp((e->position().x() - lw) / pw, 0.0, 1.0);
        zoomBy(d.y() > 0 ? 1.25 : 0.8, frac);
    }
    e->accept();
}

void WaveformView::mousePressEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton) {
        m_dragging = true;
        m_dragX = int(e->position().x());
        m_dragStart = viewStart();
        setCursor(Qt::ClosedHandCursor);
    }
}

void WaveformView::mouseMoveEvent(QMouseEvent* e)
{
    m_hoverX = int(e->position().x());
    if (m_dragging) {
        const int pw = std::max(10, width() - labelWidth() - 8);
        const Time span = m_span ? m_span : defaultSpan();
        m_span = span;
        m_start = m_dragStart - Time(double(m_hoverX - m_dragX) * double(span) / pw);
        m_follow = false;
    }
    update();
}

void WaveformView::mouseReleaseEvent(QMouseEvent*)
{
    m_dragging = false;
    unsetCursor();
}

void WaveformView::leaveEvent(QEvent*)
{
    m_hoverX = -1;
    update();
}

void WaveformView::keyPressEvent(QKeyEvent* e)
{
    switch (e->key()) {
    case Qt::Key_Plus:
    case Qt::Key_Equal: zoomBy(1.25, m_follow ? 1.0 : 0.5); break;
    case Qt::Key_Minus: zoomBy(0.8, m_follow ? 1.0 : 0.5); break;
    case Qt::Key_F: fit(); break;
    case Qt::Key_End: follow(); break;
    default: QWidget::keyPressEvent(e); return;
    }
    e->accept();
}

void WaveformView::contextMenuEvent(QContextMenuEvent* e)
{
    QMenu menu(this);
    menu.setFont(font());
    const int row = rowAt(e->pos().y());
    auto t = trace();
    QString probe;
    if (row >= 0 && t && t->channels()[size_t(row)].scope == "probes")
        probe = QString::fromStdString(t->channels()[size_t(row)].name);
    QAction* remove = probe.isEmpty() ? nullptr : menu.addAction(tr("Remove Probe %1").arg(probe));
    if (remove)
        menu.addSeparator();
    QAction* fitA = menu.addAction(tr("Fit Whole Recording (F)"));
    QAction* followA = menu.addAction(tr("Follow Live Edge (End)"));
    menu.addSeparator();
    QAction* save = menu.addAction(tr("Save Trace as VCD..."));
    QAction* chosen = menu.exec(e->globalPos());
    if (!chosen)
        return;
    if (chosen == remove && m_session)
        m_session->removeProbe(probe);
    else if (chosen == fitA)
        fit();
    else if (chosen == followA)
        follow();
    else if (chosen == save)
        emit saveRequested();
}
