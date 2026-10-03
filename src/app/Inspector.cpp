#include "Inspector.h"

#include "EditorSession.h"
#include "Theme.h"
#include "core/PartLibrary.h"

#include <QComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QTableWidget>
#include <QHeaderView>
#include <QVBoxLayout>
#include <QHBoxLayout>

namespace {
// Wokwi's wire colors (number/letter keys in Wokwi's editor).
const char* const kWireColors[] = {"black", "brown", "red", "orange", "gold", "green", "blue", "violet",
                                   "gray", "white", "cyan", "limegreen", "magenta", "purple", "yellow"};
}

Inspector::Inspector(QWidget* parent)
    : QWidget(parent)
{
    QFont f = font();
    f.setPointSize(15);
    setFont(f);
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    m_scroll = new QScrollArea(this);
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* inner = new QWidget(m_scroll);
    m_lay = new QVBoxLayout(inner);
    m_lay->setContentsMargins(10, 10, 10, 10);
    m_scroll->setWidget(inner);
    outer->addWidget(m_scroll);
    setFixedWidth(kWidth);
    rebuild();
}

void Inspector::setSession(EditorSession* s)
{
    if (m_s)
        disconnect(m_s, nullptr, this, nullptr);
    m_s = s;
    if (m_s) {
        connect(m_s, &EditorSession::selectionChanged, this, &Inspector::rebuild);
        connect(m_s, &EditorSession::documentChanged, this, &Inspector::rebuild);
        connect(m_s, &QObject::destroyed, this, [this] { m_s = nullptr; rebuild(); });
    }
    rebuild();
}

void Inspector::focusName()
{
    if (m_name) {
        m_name->setFocus();
        m_name->selectAll();
    }
}

void Inspector::clear()
{
    m_name = nullptr;
    if (m_body) {
        m_lay->removeWidget(m_body);
        m_body->deleteLater();
        m_body = nullptr;
    }
}

void Inspector::rebuild()
{
    clear();
    m_body = new QWidget(m_scroll->widget());
    auto* v = new QVBoxLayout(m_body);
    v->setContentsMargins(0, 0, 0, 0);
    m_lay->addWidget(m_body);
    auto heading = [&](const QString& t) {
        auto* l = new QLabel(t, m_body);
        QFont hf = l->font();
        hf.setBold(true);
        hf.setPointSize(hf.pointSize() + 2);
        l->setFont(hf);
        v->addWidget(l);
    };
    if (!m_s) {
        v->addStretch();
        return;
    }
    const std::vector<std::string> parts = m_s->selectedPartIds();
    const std::vector<int> wires = m_s->selectedWireIndices();
    const chiply::Document& doc = m_s->doc();

    if (parts.size() == 1 && wires.empty()) {
        const chiply::Part* p = doc.findPart(parts.front());
        if (!p) {
            v->addStretch();
            return;
        }
        const chiply::PartDef* def = chiply::PartLibrary::builtin().find(p->type);
        heading(def ? QString::fromStdString(def->label) : QString::fromStdString(p->type));
        auto* form = new QFormLayout;
        v->addLayout(form);

        m_name = new QLineEdit(QString::fromStdString(p->id), m_body);
        m_name->setToolTip(tr("Instance name, used in the Verilog export. Enter to apply."));
        auto* err = new QLabel(m_body);
        err->setWordWrap(true);
        err->setStyleSheet("color: #e53935;");
        err->hide();
        const std::string oldId = p->id;
        connect(m_name, &QLineEdit::returnPressed, this, [this, oldId, err] {
            const QString msg = m_s->renamePart(oldId, m_name->text().trimmed().toStdString());
            if (!msg.isEmpty()) {
                err->setText(msg);
                err->show();
            }
        });
        form->addRow(tr("Name"), m_name);
        form->addRow(err);
        auto* type = new QLabel(QString::fromStdString(p->type), m_body);
        type->setTextInteractionFlags(Qt::TextSelectableByMouse);
        form->addRow(tr("Type"), type);
        form->addRow(tr("Position"), new QLabel(QString("x %1, y %2, %3°").arg(p->left).arg(p->top).arg(p->rotate), m_body));

        // Attributes: everything in the part plus the library defaults.
        chiply::Json attrs = def ? def->attrs : chiply::Json::object();
        for (auto it = p->attrs.begin(); it != p->attrs.end(); ++it)
            attrs[it.key()] = it.value();
        if (!attrs.empty()) {
            heading(tr("Attributes"));
            auto* af = new QFormLayout;
            v->addLayout(af);
            for (auto it = attrs.begin(); it != attrs.end(); ++it) {
                const std::string key = it.key();
                const std::string val = it.value().is_string() ? it.value().get<std::string>() : it.value().dump();
                auto* e = new QLineEdit(QString::fromStdString(val), m_body);
                connect(e, &QLineEdit::editingFinished, this, [this, e, id = p->id, key] {
                    m_s->setPartAttr(id, key, e->text().toStdString());
                });
                af->addRow(QString::fromStdString(key), e);
            }
        }
        if (def && !def->pins.empty()) {
            heading(tr("Pins"));
            auto* t = new QTableWidget(int(def->pins.size()), 2, m_body);
            t->setHorizontalHeaderLabels({tr("Pin"), tr("Direction")});
            t->verticalHeader()->hide();
            t->horizontalHeader()->setStretchLastSection(true);
            t->setEditTriggers(QAbstractItemView::NoEditTriggers);
            for (int i = 0; i < int(def->pins.size()); ++i) {
                const chiply::PinDef& pin = def->pins[size_t(i)];
                t->setItem(i, 0, new QTableWidgetItem(QString::fromStdString(pin.name)));
                QString dir = chiply::pinDirName(pin.dir);
                if (pin.clock)
                    dir += tr(", clock");
                t->setItem(i, 1, new QTableWidgetItem(dir));
            }
            t->resizeColumnToContents(0);
            v->addWidget(t, 1);
        }
    } else if (parts.empty() && wires.size() == 1) {
        const chiply::Wire& w = doc.wires[size_t(wires.front())];
        heading(tr("Wire"));
        auto* form = new QFormLayout;
        v->addLayout(form);
        form->addRow(tr("From"), new QLabel(QString::fromStdString(w.from.str()), m_body));
        form->addRow(tr("To"), new QLabel(QString::fromStdString(w.to.str()), m_body));
        auto* color = new QComboBox(m_body);
        for (const char* c : kWireColors)
            color->addItem(c);
        if (color->findText(QString::fromStdString(w.color)) < 0)
            color->addItem(QString::fromStdString(w.color));
        color->setCurrentText(QString::fromStdString(w.color));
        const int idx = wires.front();
        connect(color, &QComboBox::currentTextChanged, this, [this, idx](const QString& c) {
            m_s->setWireColor(idx, c.toStdString());
        });
        form->addRow(tr("Color"), color);
        QString path;
        for (const std::string& h : chiply::formatWirePath(w.path))
            path += QString::fromStdString(h) + " ";
        auto* pl = new QLabel(path.isEmpty() ? tr("(direct)") : path, m_body);
        pl->setWordWrap(true);
        form->addRow(tr("Path"), pl);
    } else if (!parts.empty() || !wires.empty()) {
        heading(tr("%1 parts, %2 wires selected").arg(parts.size()).arg(wires.size()));
        auto* row = new QHBoxLayout;
        auto* rot = new QPushButton(tr("Rotate"), m_body);
        auto* dup = new QPushButton(tr("Duplicate"), m_body);
        auto* del = new QPushButton(tr("Delete"), m_body);
        connect(rot, &QPushButton::clicked, this, [this] { m_s->rotateSelection(); });
        connect(dup, &QPushButton::clicked, this, [this] { m_s->duplicateSelection(); });
        connect(del, &QPushButton::clicked, this, [this] { m_s->deleteSelection(); });
        row->addWidget(rot);
        row->addWidget(dup);
        row->addWidget(del);
        v->addLayout(row);
    } else {
        auto* l = new QLabel(tr("Nothing selected.\n\nClick a part or wire, or drag on empty canvas to select several.\n\nA: add a part"), m_body);
        l->setWordWrap(true);
        v->addWidget(l);
    }
    v->addStretch();
}
