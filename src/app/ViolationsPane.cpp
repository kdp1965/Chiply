#include "ViolationsPane.h"
#include "Text.h"

#include "EditorSession.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QPointer>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

using chiply::drc::Severity;
using chiply::drc::Violation;

namespace {

constexpr int kKeyRole = Qt::UserRole + 1;

// Shape and word, so severities never depend on colour alone.
QString severityText(Severity s)
{
    switch (s) {
    case Severity::Error: return QStringLiteral("✖ error");
    case Severity::Warning: return QStringLiteral("▲ warning");
    case Severity::Info: return QStringLiteral("● info");
    }
    return {};
}

QColor severityColor(Severity s)
{
    switch (s) {
    case Severity::Error: return QColor(0xe5, 0x39, 0x35);
    case Severity::Warning: return QColor(0xf9, 0xa8, 0x25);
    case Severity::Info: return QColor(0x42, 0xa5, 0xf5);
    }
    return {};
}

const Violation* findViolation(const EditorSession* s, const std::string& key)
{
    if (!s)
        return nullptr;
    for (const Violation& v : s->violations())
        if (v.key == key)
            return &v;
    return nullptr;
}

} // namespace

ViolationsPane::ViolationsPane(QWidget* parent)
    : QWidget(parent)
{
    QFont f = font();
    f.setPointSize(std::max(f.pointSize(), 15)); // readable
    setFont(f);

    auto* top = new QHBoxLayout;
    m_full = new QPushButton(tr("Full DRC"), this);
    m_full->setObjectName("fullDrcButton");
    m_full->setToolTip(tr("Check the whole design now"));
    m_live = new QCheckBox(tr("Live"), this);
    m_live->setObjectName("liveDrcBox");
    m_live->setToolTip(tr("Re-check what each edit touched as you work"));
    m_live->setChecked(EditorSession::drcLive());
    m_checks = new QToolButton(this);
    m_checks->setText(tr("Checks"));
    m_checks->setObjectName("drcChecksButton");
    m_checks->setPopupMode(QToolButton::InstantPopup);
    m_checks->setMenu(new QMenu(m_checks));
    m_checks->setToolTip(tr("Turn checks on or off for this design (saved next to it)"));
    top->addWidget(m_full);
    top->addWidget(m_live);
    top->addWidget(m_checks);
    top->addStretch();

    m_counts = new QLabel(this);
    m_counts->setObjectName("drcCounts");
    m_stats = new QLabel(this);
    m_stats->setObjectName("drcStats");
    m_stats->setWordWrap(true); // never wider than the pane
    QFont sf = font();
    sf.setPointSize(std::max(12, sf.pointSize() - 3));
    m_stats->setFont(sf);
    m_filter = new QLineEdit(this);
    m_filter->setPlaceholderText(tr("Filter"));
    m_filter->setClearButtonEnabled(true);

    m_tree = new QTreeWidget(this);
    m_tree->setColumnCount(2);
    m_tree->setHeaderLabels({tr("Severity"), tr("Message")});
    m_tree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_tree->header()->setStretchLastSection(true);
    m_tree->setRootIsDecorated(true);
    m_tree->setUniformRowHeights(false);
    m_tree->setWordWrap(true);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);

    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(6, 6, 6, 6);
    lay->addLayout(top);
    lay->addWidget(m_counts);
    lay->addWidget(m_stats);
    lay->addWidget(m_filter);
    lay->addWidget(m_tree, 1);

    connect(m_full, &QPushButton::clicked, this, [this] {
        if (m_session)
            m_session->runDrc(true);
    });
    connect(m_live, &QCheckBox::toggled, this, [this](bool on) {
        EditorSession::setDrcLive(on);
        if (on && m_session)
            m_session->runDrc(false); // catch up with edits made while off
    });
    connect(m_filter, &QLineEdit::textChanged, this, &ViolationsPane::rebuild);
    connect(m_tree, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem* it) { activate(it); });
    connect(m_tree, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem* it) { activate(it); });
    connect(m_tree, &QTreeWidget::itemCollapsed, this, [this](QTreeWidgetItem* it) {
        if (!m_rebuilding)
            m_collapsed.insert(it->data(0, Qt::UserRole).toString());
    });
    connect(m_tree, &QTreeWidget::itemExpanded, this, [this](QTreeWidgetItem* it) {
        if (!m_rebuilding)
            m_collapsed.remove(it->data(0, Qt::UserRole).toString());
    });
    connect(m_tree, &QTreeWidget::customContextMenuRequested, this, &ViolationsPane::showContextMenu);
    connect(m_checks->menu(), &QMenu::aboutToShow, this, &ViolationsPane::updateChecksMenu);
    m_collapsed.insert(QStringLiteral("__waived__"));
    rebuild();
}

void ViolationsPane::setSession(EditorSession* s)
{
    if (s == m_session)
        return;
    disconnect(m_conn);
    m_session = s;
    if (s)
        m_conn = connect(s, &EditorSession::drcChanged, this, &ViolationsPane::rebuild);
    rebuild();
}

QString ViolationsPane::countsText() const
{
    if (!m_session)
        return {};
    const int e = m_session->unwaivedCount(Severity::Error);
    const int w = m_session->unwaivedCount(Severity::Warning);
    const int i = m_session->unwaivedCount(Severity::Info);
    QString t = QStringLiteral("\u2716 ") + countOf(e, "error", "errors") + QStringLiteral("   \u25B2 ") + countOf(w, "warning", "warnings");
    if (i)
        t += QStringLiteral("   ") + QStringLiteral("\u25CF ") + countOf(i, "info", "info");
    return t;
}

void ViolationsPane::rebuild()
{
    m_rebuilding = true;
    const QTreeWidgetItem* cur = m_tree->currentItem();
    const QString curKey = cur ? cur->data(0, kKeyRole).toString() : QString();
    m_tree->clear();
    m_full->setEnabled(m_session != nullptr);
    if (!m_session) {
        m_counts->clear();
        m_stats->clear();
        m_rebuilding = false;
        return;
    }
    m_counts->setText(countsText());
    const chiply::drc::Stats& st = m_session->drcStats();
    m_stats->setText(st.full ? tr("Full DRC: %1 parts, %2 ms").arg(st.partsTotal).arg(st.ms, 0, 'f', 1)
                             : tr("Checked %1 of %2 parts, %3 nets after the last edit (%4 ms)")
                                   .arg(st.partsChecked)
                                   .arg(st.partsTotal)
                                   .arg(st.netsChecked)
                                   .arg(st.ms, 0, 'f', 1));

    const QString filter = m_filter->text().trimmed();
    std::map<std::string, QTreeWidgetItem*> groups;
    QTreeWidgetItem* waived = nullptr;
    QTreeWidgetItem* select = nullptr;
    int waivedCount = 0;
    std::map<QTreeWidgetItem*, int> counts;
    for (const Violation& v : m_session->violations()) {
        const QString msg = QString::fromStdString(v.message);
        if (!filter.isEmpty() && !msg.contains(filter, Qt::CaseInsensitive)
            && !QString::fromStdString(v.check).contains(filter, Qt::CaseInsensitive))
            continue;
        QTreeWidgetItem* parent;
        if (m_session->isWaived(v.key)) {
            if (!waived) {
                waived = new QTreeWidgetItem();
                waived->setData(0, kKeyRole, QString());
                waived->setData(0, Qt::UserRole, QStringLiteral("__waived__"));
            }
            parent = waived;
            ++waivedCount;
        } else {
            auto it = groups.find(v.check);
            if (it == groups.end()) {
                auto* g = new QTreeWidgetItem(m_tree);
                const chiply::drc::CheckInfo* info = chiply::drc::findCheck(v.check);
                g->setData(0, Qt::UserRole, QString::fromStdString(v.check));
                g->setText(0, QString::fromStdString(info ? info->title : v.check));
                g->setToolTip(0, QString::fromStdString(info ? info->description : ""));
                g->setFirstColumnSpanned(true);
                QFont gf = g->font(0);
                gf.setBold(true);
                g->setFont(0, gf);
                it = groups.emplace(v.check, g).first;
            }
            parent = it->second;
        }
        ++counts[parent];
        auto* row = new QTreeWidgetItem(parent);
        row->setText(0, severityText(v.severity));
        row->setForeground(0, severityColor(v.severity));
        QString text = msg;
        if (m_session->isWaived(v.key)) {
            const QString reason = m_session->waiverReason(v.key);
            if (!reason.isEmpty())
                text += tr("  (waived: %1)").arg(reason);
        }
        row->setText(1, text);
        row->setToolTip(1, text);
        row->setData(0, kKeyRole, QString::fromStdString(v.key));
        if (QString::fromStdString(v.key) == curKey)
            select = row;
    }
    for (auto& [check, g] : groups) {
        g->setText(0, g->text(0) + QStringLiteral(" (%1)").arg(counts[g]));
        g->setExpanded(!m_collapsed.contains(QString::fromStdString(check)));
    }
    if (waived) {
        waived->setText(0, tr("Waived (%1)").arg(waivedCount));
        waived->setFirstColumnSpanned(true);
        m_tree->addTopLevelItem(waived);
        waived->setExpanded(!m_collapsed.contains(QStringLiteral("__waived__")));
    }
    if (select)
        m_tree->setCurrentItem(select);
    m_rebuilding = false;
}

QTreeWidgetItem* ViolationsPane::itemFor(const std::string& key) const
{
    const QString k = QString::fromStdString(key);
    for (QTreeWidgetItemIterator it(m_tree); *it; ++it)
        if ((*it)->data(0, kKeyRole).toString() == k)
            return *it;
    return nullptr;
}

void ViolationsPane::activate(QTreeWidgetItem* item)
{
    if (!item || !m_session)
        return;
    const std::string key = item->data(0, kKeyRole).toString().toStdString();
    if (const Violation* v = findViolation(m_session, key))
        m_session->showViolation(*v);
}

void ViolationsPane::step(int dir)
{
    std::vector<QTreeWidgetItem*> rows;
    for (QTreeWidgetItemIterator it(m_tree); *it; ++it)
        if (!(*it)->data(0, kKeyRole).toString().isEmpty())
            rows.push_back(*it);
    if (rows.empty())
        return;
    auto pos = std::find(rows.begin(), rows.end(), m_tree->currentItem());
    std::size_t i;
    if (pos == rows.end())
        i = dir > 0 ? 0 : rows.size() - 1;
    else
        i = (std::size_t(pos - rows.begin()) + rows.size() + std::size_t(dir)) % rows.size();
    QTreeWidgetItem* r = rows[i];
    if (r->parent() && !r->parent()->isExpanded())
        r->parent()->setExpanded(true);
    m_tree->setCurrentItem(r);
    m_tree->scrollToItem(r);
    activate(r);
}

void ViolationsPane::next() { step(1); }
void ViolationsPane::previous() { step(-1); }

void ViolationsPane::updateChecksMenu()
{
    QMenu* m = m_checks->menu();
    m->clear();
    m->setFont(font());
    for (const chiply::drc::CheckInfo& c : chiply::drc::checks()) {
        QAction* a = m->addAction(QString::fromStdString(c.title) + QStringLiteral("  (") + chiply::drc::severityName(c.severity)
                                  + QLatin1Char(')'));
        a->setCheckable(true);
        a->setChecked(m_session && m_session->drc().enabled(c.id));
        a->setToolTip(QString::fromStdString(c.description));
        a->setEnabled(m_session != nullptr);
        const std::string id = c.id;
        connect(a, &QAction::toggled, this, [this, id](bool on) {
            if (m_session)
                m_session->setCheckEnabled(id, on);
        });
    }
    m->setToolTipsVisible(true);
}

void ViolationsPane::showContextMenu(const QPoint& pos)
{
    QTreeWidgetItem* item = m_tree->itemAt(pos);
    const std::string key = item ? item->data(0, kKeyRole).toString().toStdString() : std::string();
    const Violation* v = findViolation(m_session, key);
    auto* menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    menu->setFont(font());
    QPointer<EditorSession> sp(m_session);
    if (v) {
        if (m_session->isWaived(key)) {
            connect(menu->addAction(tr("Remove Waiver")), &QAction::triggered, this, [sp, key] {
                if (sp)
                    sp->unwaive(key);
            });
        } else {
            connect(menu->addAction(tr("Waive This Violation...")), &QAction::triggered, this, [this, sp, key] {
                auto* dlg = new QInputDialog(this);
                dlg->setAttribute(Qt::WA_DeleteOnClose);
                dlg->setWindowTitle(tr("Waive Violation"));
                dlg->setLabelText(tr("Reason (optional, saved with the design):"));
                dlg->setInputMode(QInputDialog::TextInput);
                dlg->setFont(font());
                connect(dlg, &QInputDialog::textValueSelected, this, [sp, key](const QString& reason) {
                    if (sp)
                        sp->waive(key, reason);
                });
                dlg->open();
            });
        }
        const chiply::drc::CheckInfo* info = chiply::drc::findCheck(v->check);
        const std::string check = v->check;
        connect(menu->addAction(tr("Disable Check \"%1\"").arg(QString::fromStdString(info ? info->title : v->check))),
                &QAction::triggered, this, [sp, check] {
                    if (sp)
                        sp->setCheckEnabled(check, false);
                });
        menu->addSeparator();
        const QString message = QString::fromStdString(v->message);
        connect(menu->addAction(tr("Copy Message")), &QAction::triggered, this, [message] { QApplication::clipboard()->setText(message); });
    }
    connect(menu->addAction(tr("Copy All as Text")), &QAction::triggered, this, [sp] {
        if (!sp)
            return;
        QString all;
        for (const Violation& x : sp->violations())
            all += QStringLiteral("%1: [%2] %3%4\n")
                       .arg(QString::fromLatin1(chiply::drc::severityName(x.severity)), QString::fromStdString(x.check),
                            QString::fromStdString(x.message), sp->isWaived(x.key) ? tr(" (waived)") : QString());
        QApplication::clipboard()->setText(all);
    });
    menu->popup(m_tree->viewport()->mapToGlobal(pos)); // (no nested event loops: Qt for WebAssembly)
}
