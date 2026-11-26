// prompt.cpp – see prompt.h.
#include "prompt.h"

#include <QInputDialog>
#include <QPushButton>

namespace {

// The box of a question, its buttons in answer order. Escape takes a Cancel answer, else a reject one.
QMessageBox* boxOf(QWidget* parent, const Prompt::Question& q, QList<QAbstractButton*>* buttons) {
    auto* box = new QMessageBox(q.icon, q.title, q.text, QMessageBox::NoButton, parent);
    box->setObjectName(q.name);
    box->setInformativeText(q.details);
    QAbstractButton* escape = nullptr;
    for (const Prompt::Answer& a : q.answers) {
        QPushButton* button = a.standard != QMessageBox::NoButton ? box->addButton(a.standard) : box->addButton(a.text, a.role);
        button->setObjectName(a.name);
        *buttons << button;
        if (a.standard == QMessageBox::Cancel || (!escape && a.role == QMessageBox::RejectRole)) escape = button;
    }
    if (QPushButton* chosen = qobject_cast<QPushButton*>(buttons->value(q.byDefault))) box->setDefaultButton(chosen);
    if (escape) box->setEscapeButton(escape);
    return box;
}

}  // namespace

namespace Prompt {

int ask(QWidget* parent, const Question& question) {
    QList<QAbstractButton*> buttons;
    QMessageBox* box = boxOf(parent, question, &buttons);
    box->exec();
    const int answer = static_cast<int>(buttons.indexOf(box->clickedButton()));
    delete box;
    return answer;
}

void askLater(QWidget* parent, const Question& question, std::function<void(int)> answered) {
    auto buttons = std::make_shared<QList<QAbstractButton*>>();
    QMessageBox* box = boxOf(parent, question, buttons.get());
    box->setAttribute(Qt::WA_DeleteOnClose);
    QObject::connect(box, &QMessageBox::finished, box, [box, buttons, answered] {
        answered(static_cast<int>(buttons->indexOf(box->clickedButton())));
    });
    box->open();
}

bool yes(QWidget* parent, const QString& title, const QString& text) {
    return ask(parent, {"question", title, text, QString(),
                        {{QString(), "yes", QMessageBox::YesRole, QMessageBox::Yes}, {QString(), "no", QMessageBox::NoRole, QMessageBox::No}}}) == 0;
}

void error(QWidget* parent, const QString& title, const QString& text) {
    ask(parent, {"error", title, text, QString(), {{QString(), "ok", QMessageBox::AcceptRole, QMessageBox::Ok}}, 0, QMessageBox::Warning});
}

void inform(QWidget* parent, const QString& title, const QString& text) {
    ask(parent, {"information", title, text, QString(), {{QString(), "ok", QMessageBox::AcceptRole, QMessageBox::Ok}}, 0, QMessageBox::Information});
}

std::optional<QString> text(QWidget* parent, const QString& title, const QString& label, const QString& initial) {
    bool ok = false;
    const QString answer = QInputDialog::getText(parent, title, label, QLineEdit::Normal, initial, &ok);
    return ok ? std::optional<QString>(answer) : std::nullopt;
}

std::optional<QString> lines(QWidget* parent, const QString& title, const QString& label) {
    bool ok = false;
    const QString answer = QInputDialog::getMultiLineText(parent, title, label, QString(), &ok);
    return ok ? std::optional<QString>(answer) : std::nullopt;
}

}  // namespace Prompt
