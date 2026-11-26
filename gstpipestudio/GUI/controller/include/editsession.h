// editsession.h – the one way a document changes. An edit is one transaction on a draft of the plan, or one gps action;
// either way it is one undo step, noted for the support report, and refused while editing is blocked (a run, a file
// opening, a cancel: the one guard). It also keeps the save point: the editor's state at the last save or load, so a
// document is modified exactly while its state differs from it, and undoing back to it clears the mark.
#pragma once

#include <QObject>
#include <QString>
#include <cstdint>
#include <functional>
#include <limits>

#include "pipeline.h"

class EditSession : public QObject {
    Q_OBJECT
public:
    // What one user edit says about itself; its change fills it in.
    struct Edit {
        QString action, linking;    // for the support report
        QString touched;            // the element it concerned: the suspect when the validator dies
        bool presentation = false;  // groups and positions only: committed without any validation
    };

    explicit EditSession(gps::Pipeline& pipeline, QObject* parent = nullptr);

    // The guard: why editing is blocked now, "" when it is not. Every edit, undo and redo asks it.
    void setBlocker(std::function<QString()> why) { blocker_ = std::move(why); }
    QString blockedReason() const { return blocker_ ? blocker_() : QString(); }
    bool blocked() const { return !blockedReason().isEmpty(); }

    // Refused while blocked or when change returns false (nothing happened, the draft is dropped); otherwise one undo
    // step, noted, and committed() says so.
    bool transact(const std::function<bool(gps::Graph&, Edit&)>& change);
    bool perform(const gps::IAction& action, const Edit& edit);  // a gps action as one edit; its refusal is a notice
    bool canUndo() const { return pipeline_.editor().canUndo() && !blocked(); }
    bool canRedo() const { return pipeline_.editor().canRedo() && !blocked(); }
    bool undo();
    bool redo();
    // The whole plan replaced as one undo step (an earlier version, an import); the caller follows it up, so nothing is
    // signalled. False when the editor refused it (said in a notice).
    bool replace(const gps::Graph& plan);

    // ---- the save point
    bool modified() const { return pipeline_.editor().state() != savedState_; }
    void markSaved();     // it matches its file now: saved, opened, reloaded, or new
    // It no longer does, whatever the undo history says (its file was deleted, a backup came back). keepAside: the save
    // point is kept for restoreSavePoint (the deleted file may come back as it was).
    void markModified(bool keepAside = false);
    void restoreSavePoint();  // the save point markModified kept aside, when nothing was saved or loaded since

    // ---- what the support report says the user did last
    void note(const QString& action, const QString& linking = QString());
    QString lastAction() const { return action_; }
    QString lastLinking() const { return linking_; }

signals:
    // An edit, undo or redo is in the plan now; restoring: it was undo or redo (going back is not an edit to follow).
    void committed(const EditSession::Edit& edit, bool restoring);
    void notice(const QString& text);
    void synced();  // markSaved

private:
    bool commitDraft();                             // the draft becomes the plan; a refusal is a notice
    bool commit(const Edit& edit, bool restoring);  // noted, the draft committed, committed() follows
    bool step(bool undo);

    static constexpr std::uint64_t kNeverSaved = std::numeric_limits<std::uint64_t>::max();  // no state matches it
    gps::Pipeline& pipeline_;
    std::function<QString()> blocker_;
    std::uint64_t savedState_ = 0;
    std::uint64_t asideState_ = kNeverSaved;  // the save point markModified kept aside
    QString action_, linking_;
};
