// editsession.cpp – see editsession.h.
#include "editsession.h"

#include <utility>

EditSession::EditSession(gps::Pipeline& pipeline, QObject* parent) : QObject(parent), pipeline_(pipeline) {}

bool EditSession::transact(const std::function<bool(gps::Graph&, Edit&)>& change) {
    if (blocked()) return false;
    Edit edit;
    if (!change(pipeline_.editor().begin(), edit)) {
        pipeline_.editor().rollback();
        return false;
    }
    return commit(edit, false);
}

bool EditSession::perform(const gps::IAction& action, const Edit& edit) {
    if (blocked()) return false;
    std::string error;
    if (!pipeline_.editor().execute(action, &error)) {
        if (!error.empty()) emit notice(QString::fromStdString(error));
        return false;
    }
    return commit(edit, false);
}

bool EditSession::replace(const gps::Graph& plan) {
    pipeline_.editor().begin() = plan;
    return commitDraft();
}

bool EditSession::undo() { return step(true); }

bool EditSession::redo() { return step(false); }

bool EditSession::step(bool undo) {
    if (!(undo ? canUndo() : canRedo())) return false;
    if (!(undo ? pipeline_.editor().undo() : pipeline_.editor().redo())) return false;
    return commit({undo ? "Undo" : "Redo"}, true);
}

// A commit that changed nothing is no failure; one the editor refused says why.
bool EditSession::commitDraft() {
    if (!pipeline_.editor().editing()) return true;
    std::string error;
    const bool committed = pipeline_.editor().commit(&error);
    if (!error.empty()) emit notice(QString::fromStdString(error));
    return committed || error.empty();
}

// An edit that says nothing of itself (boxes moved) leaves the last note as it is.
bool EditSession::commit(const Edit& edit, bool restoring) {
    if (!edit.action.isEmpty()) note(edit.action, edit.linking);
    if (!commitDraft()) return false;
    emit committed(edit, restoring);
    return true;
}

void EditSession::markSaved() {
    savedState_ = pipeline_.editor().state();
    asideState_ = kNeverSaved;
    emit synced();
}

void EditSession::markModified(bool keepAside) {
    if (savedState_ != kNeverSaved || !keepAside) asideState_ = keepAside ? savedState_ : kNeverSaved;
    savedState_ = kNeverSaved;
}

void EditSession::restoreSavePoint() {
    if (savedState_ == kNeverSaved) savedState_ = std::exchange(asideState_, kNeverSaved);
}

void EditSession::note(const QString& action, const QString& linking) {
    action_ = action;
    linking_ = linking;
}
