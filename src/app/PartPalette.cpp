#include "PartPalette.h"

#include "EditorSession.h"
#include "Theme.h"
#include "core/PartLibrary.h"

#include <QDialogButtonBox>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QVBoxLayout>

#include <map>

PartPalette::PartPalette(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Add Part"));
    QFont big = font();
    big.setPointSize(std::max(15, Theme::instance().hoverTextSize() - 2));
    setFont(big);
    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(tr("Search parts (e.g. and, flop, mux, tiny)"));
    m_search->setClearButtonEnabled(true);
    m_list = new QListWidget(this);
    m_list->setUniformItemSizes(true);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    auto* lay = new QVBoxLayout(this);
    lay->addWidget(m_search);
    lay->addWidget(m_list, 1);
    lay->addWidget(buttons);
    resize(520, 640);

    connect(m_search, &QLineEdit::textChanged, this, &PartPalette::refill);
    connect(m_list, &QListWidget::itemActivated, this, [this] { accept(); });
    connect(buttons, &QDialogButtonBox::accepted, this, &PartPalette::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    // Up/Down in the search box move through the list.
    m_search->installEventFilter(this);
    refill(QString());
    m_search->setFocus();
}

void PartPalette::refill(const QString& filter)
{
    m_list->clear();
    // Category order roughly as in Wokwi's Logic-first palette.
    static const QStringList order{"Logic", "Chiply cells", "Sheets", "Custom", "Tiny Tapeout", "Input", "Output", "Power", "Annotation", "Passive", "Misc", "Boards"};
    std::map<int, std::vector<const chiply::PartDef*>> groups;
    const bool extensions = EditorSession::extensionsEnabled();
    for (const chiply::PartDef& d : chiply::PartLibrary::builtin().parts()) {
        if (d.hidden)
            continue; // other memory sizes: change the size in the Inspector
        if (!extensions && chiply::isExtensionType(d.type))
            continue; // Wokwi mode: only Wokwi's parts (PLAN.md 7.1)
        const QString hay = QString::fromStdString(d.label + " " + d.type + " " + d.category + " " + d.prefix);
        if (!filter.isEmpty() && !hay.contains(filter, Qt::CaseInsensitive))
            continue;
        int rank = int(order.indexOf(QString::fromStdString(d.category)));
        groups[rank < 0 ? 99 : rank].push_back(&d);
    }
    QListWidgetItem* first = nullptr;
    for (const auto& [rank, defs] : groups) {
        auto* header = new QListWidgetItem(QString::fromStdString(defs.front()->category).toUpper(), m_list);
        header->setFlags(Qt::NoItemFlags);
        QFont hf = header->font();
        hf.setBold(true);
        header->setFont(hf);
        for (const chiply::PartDef* d : defs) {
            auto* it = new QListWidgetItem(QStringLiteral("    ") + QString::fromStdString(d->label), m_list);
            it->setData(Qt::UserRole, QString::fromStdString(d->type));
            it->setToolTip(QString::fromStdString(d->type));
            if (!first)
                first = it;
        }
    }
    if (first)
        m_list->setCurrentItem(first);
}

void PartPalette::accept()
{
    QListWidgetItem* it = m_list->currentItem();
    if (!it || it->data(Qt::UserRole).toString().isEmpty())
        return;
    m_chosen = it->data(Qt::UserRole).toString();
    QDialog::accept();
}

bool PartPalette::eventFilter(QObject* o, QEvent* e)
{
    if (o == m_search && e->type() == QEvent::KeyPress) {
        auto* k = static_cast<QKeyEvent*>(e);
        if (k->key() == Qt::Key_Down || k->key() == Qt::Key_Up) {
            int row = m_list->currentRow();
            const int step = k->key() == Qt::Key_Down ? 1 : -1;
            for (int r = row + step; r >= 0 && r < m_list->count(); r += step) {
                if (m_list->item(r)->flags() & Qt::ItemIsEnabled) {
                    m_list->setCurrentRow(r);
                    break;
                }
            }
            return true;
        }
    }
    return QDialog::eventFilter(o, e);
}
