#pragma once
// The "+" dialog: searchable list of part types grouped by category.
#include <QDialog>

class QLineEdit;
class QListWidget;

class PartPalette : public QDialog {
    Q_OBJECT
public:
    explicit PartPalette(QWidget* parent = nullptr);
    // The chosen part type after exec() == Accepted.
    QString chosenType() const { return m_chosen; }

protected:
    bool eventFilter(QObject* o, QEvent* e) override;

private:
    void refill(const QString& filter);
    void accept() override;

    QLineEdit* m_search;
    QListWidget* m_list;
    QString m_chosen;
};
