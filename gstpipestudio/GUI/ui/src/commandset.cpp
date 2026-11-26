// commandset.cpp – see commandset.h.
#include "commandset.h"

#include <QAction>
#include <QWidget>

namespace {
// "Save as" of "Save &as…": what a tooltip calls the command.
QString plain(QString text) { return text.remove('&').remove(QChar(0x2026)).trimmed(); }
}  // namespace

CommandSet::CommandSet(QObject* parent) : QObject(parent) {}

void CommandSet::add(const QList<Spec>& specs) {
    for (const Spec& spec : specs) {
        auto* action = new QAction(spec.text, parent());
        action->setObjectName(spec.name);
        action->setShortcuts(spec.keys);
        connect(action, &QAction::triggered, this, spec.run);
        entries_[spec.id].action = action;
        entries_[spec.id].off = spec.off;
    }
}

void CommandSet::setChecked(Command id, std::function<bool()> checked) {
    entries_[id].action->setCheckable(true);
    entries_[id].checked = std::move(checked);
}

void CommandSet::refresh() {
    for (const Entry& e : std::as_const(entries_)) {
        if (e.label) e.action->setText(e.label());
        const QString why = e.off ? e.off() : QString();
        const QString name = plain(e.action->text());
        e.action->setEnabled(why.isEmpty());
        e.action->setToolTip(!why.isEmpty() ? name + " is off. " + why : e.tip.isEmpty() ? name : e.tip);
        if (e.checked) e.action->setChecked(e.checked());
    }
}
