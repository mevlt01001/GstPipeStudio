// prompt.h – the one way the application asks the user something or tells of a failure. A question is a message box
// named for tests and screen readers, its answers in the order given; Escape takes the Cancel (or reject) answer.
// Save, close, reload, delete, template and library questions all come here; so do the errors of those operations.
#pragma once

#include <QList>
#include <QMessageBox>
#include <QString>
#include <functional>
#include <optional>

class QWidget;

namespace Prompt {

// One button of a question: a text of its own, or a standard button (Cancel, Yes, No).
struct Answer {
    QString text;
    const char* name = "";  // object name of its button
    QMessageBox::ButtonRole role = QMessageBox::AcceptRole;
    QMessageBox::StandardButton standard = QMessageBox::NoButton;
};
inline Answer cancel() { return {QString(), "cancel", QMessageBox::RejectRole, QMessageBox::Cancel}; }

struct Question {
    const char* name = "";   // object name of the box
    QString title, text, details;  // details: the smaller text under the question
    QList<Answer> answers;
    int byDefault = 0;  // the answer Enter takes
    QMessageBox::Icon icon = QMessageBox::Question;
};

// Modal: the index of the answer chosen, -1 when the box was closed another way.
int ask(QWidget* parent, const Question& question);
// Window-modal, the event loop goes on (saves, runs, the watcher) while it waits; answered gets the index, or -1.
void askLater(QWidget* parent, const Question& question, std::function<void(int)> answered);
// Yes / No, Yes the default.
bool yes(QWidget* parent, const QString& title, const QString& text);
void error(QWidget* parent, const QString& title, const QString& text);
void inform(QWidget* parent, const QString& title, const QString& text);
// A line of text (a name) or several (a pasted command); nothing when the user cancelled.
std::optional<QString> text(QWidget* parent, const QString& title, const QString& label, const QString& initial = QString());
std::optional<QString> lines(QWidget* parent, const QString& title, const QString& label);

}  // namespace Prompt
