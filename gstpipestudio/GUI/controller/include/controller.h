// controller.h – one document: its plan (gps::Pipeline), what it is saved to (SaveTarget), and the sessions that work on
// it. The Controller is their facade: widgets read views and submit edits here, and it sequences what one session's
// work means for the others (an edit is validated, an opened file is laid out, a template copy is committed on save).
//   EditSession        transactions and gps actions, undo and redo, the save point, the edit-blocked guard
//   ValidationSession  validation by gps-update: the pause, the slow watch, cancel, the result
//   RunSession         run and monitor, on a temporary copy
//   TemplateInstances  expanded, edited and modified copies, commit, newer revisions, parameters
//   DocumentViews      what the canvas and the drawer show; names and the outline kept per revision
// Validation allows further edits; running, opening a file and cancelling a validation block them.
// Call services().catalog().fetchInfo(factory, context, …) before edits that refer to a new factory.
#pragma once

#include <QObject>
#include <QPair>
#include <QPointF>
#include <QTemporaryDir>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <optional>

#include "appservices.h"
#include "canvaslayout.h"
#include "contract.h"
#include "documentviews.h"
#include "editsession.h"
#include "elementedit.h"
#include "pipeline.h"
#include "runsession.h"
#include "savetarget.h"
#include "slowwatch.h"
#include "supportreport.h"
#include "templateinstances.h"
#include "validationsession.h"

class Controller : public QObject {
    Q_OBJECT
public:
    static constexpr double kFree = std::numeric_limits<double>::quiet_NaN();  // no position given: a free spot

    // One per document; the services are the process's, shared with the other documents.
    explicit Controller(AppServices& services, QObject* parent = nullptr);
    AppServices& services() const { return services_; }

    // ---- state
    const gps::Graph& plan() const { return pipeline_.editor().view(); }
    const gps::Pipeline& pipeline() const { return pipeline_; }
    gps::NodeShape shape() const { return views_.shape(); }
    const UpdateResult& result() const { return pipeline_.observations(); }  // the last validation, owned by Pipeline
    bool updating() const { return validation_.updating(); }
    bool running() const { return runs_.running(); }
    bool monitoring() const { return runs_.monitoring(); }
    bool busy() const { return !busyReason().isEmpty(); }
    QString busyReason() const;  // why a step is running (editing is blocked, or validation runs), "" when none is
    bool editingBlocked() const { return edits_.blocked(); }
    bool canSave() const { return saveBlockedReason().isEmpty(); }  // Save always writes, unless a file opens or it runs
    bool canRun() const { return runBlockedReason().isEmpty(); }    // validated, no problem, and complete
    QString saveBlockedReason() const { return edits_.blockedReason(); }  // why Save (and editing) is off, "" when on
    QString runBlockedReason() const;   // why Run (and Monitor) is off, "" when it is on
    bool notValidated() const { return validation_.cancelled(); }  // the user cancelled the last validation
    bool helperStopped() const;  // the last validation's helper crashed or gave no usable answer
    QString launchCommand() const;  // the gst-launch-1.0 line for the saved file, "" before the first save

    // ---- what the document is saved to, and whether it holds changes it has not saved
    const SaveTarget& target() const { return *target_; }
    void setTarget(std::unique_ptr<SaveTarget> target);  // binds the document to another target; the content stays
    bool modified() const { return edits_.modified(); }  // changed since its last save or load
    void markModified();  // it no longer matches its target's file (the file was deleted), whatever the undo history says
    bool hasUnsavedWork() const { return modified() && !plan().empty(); }  // what the window asks about
    void setUpdateTimeoutMs(int ms) { validation_.setTimeoutMs(ms); }

    // ---- slow validation and cancel (UI_RULES 3.1 and 3.2)
    SlowWatch* slowWatch() const { return validation_.slowWatch(); }
    bool slowAsked() const { return validation_.slowAsked(); }
    bool cancelling() const { return validation_.cancelling(); }
    void cancelUpdate() { validation_.cancel(); }
    void waitMore() { validation_.waitMore(); }

    // ---- support
    void setUrlOpener(UrlOpener* opener) { opener_ = opener; }
    void requestSupport(const QString& title);  // opens the prefilled issue in the browser
    QString lastAction() const { return edits_.lastAction(); }  // what the last edit did, in words

    // ---- template copies (TemplateInstances); the library itself is services().library()
    QString beginTemplate(const QStringList& elements);  // the new copy, expanded so the canvas makes room; "" when refused
    bool finishTemplate(const QString& id, const QString& name, const std::vector<gps::ExposedParam>& params, QString* error) {
        return templates_.finish(id, name, params, error);
    }
    void cancelTemplate(const QString& id) { templates_.detach(id); }
    bool saveTemplate(const QString& templateName, const QStringList& elementNames);  // all three, no parameters, collapsed
    std::vector<gps::ExposedParam> parameterChoices(const QStringList& elements) const { return templates_.parameterChoices(elements); }
    void keepEditingTemplate(const QString& id) { templates_.keepEditing(id); }
    bool saveTemplateAsNew(const QString& id, const QString& name, QString* error) { return templates_.saveAsNew(id, name, error); }
    QStringList outdatedTemplates() const { return templates_.outdated(); }
    bool updateTemplates(const QStringList& ids) { return templates_.update(ids); }
    std::vector<gps::ExposedParam> templateParams(const QString& id) const { return templates_.params(id); }
    bool setTemplateParams(const QString& id, const std::map<std::string, std::string>& values) { return templates_.setParams(id, values); }
    // A library template's parameters (the plan's exposed list) while it is the document (a LibraryTemplateTarget).
    void setTemplateParameters(const std::vector<gps::ExposedParam>& params) { templates_.setTemplateParameters(params); }
    bool detachTemplate(const QString& id) { return templates_.detach(id); }
    bool isTemplateExpanded(const QString& id) const { return templates_.expanded(id); }
    void setTemplateExpanded(const QString& id, bool expanded);  // the box grows into its frame, or the frame into its box
    // A component (a template, later a plugin) on the canvas, at (x, y) or a free spot; or fed by a free src pad.
    // The elements it placed, none when it was refused.
    QStringList place(const ComponentRef& component, double x = kFree, double y = kFree);
    QStringList placeAfter(const QString& srcElement, const QString& srcPad, const ComponentRef& component);

    // ---- what the canvas and the drawer show (DocumentViews)
    Outline outline() const { return views_.outline(); }
    gps::Names names() const { return views_.names(); }
    QString padName(const gps::PadRef& pad) const { return views_.padName(pad); }
    ElementView view(const QString& element) const { return views_.view(element); }
    ElementView templateView(const QString& id) const { return views_.templateView(id); }
    ElementView boxView(const QString& box) const { return views_.boxView(box); }
    FrameView frameView(const QString& id) const { return views_.frameView(id); }
    QList<LinkView> linkViews() const { return views_.linkViews(); }
    std::vector<StructureInfo> padStructures(const QString& element, const QString& identity) const { return views_.padStructures(element, identity); }
    QString padAllowed(const QString& element, const QString& identity) const { return views_.padAllowed(element, identity); }
    QList<ExistingTarget> existingTargets(const QString& element, const QString& identity) const { return views_.existingTargets(element, identity); }
    std::vector<PadTemplateInfo> sinkChoices(const ElementInfo& info, const QString& element, const QString& identity) const {
        return views_.sinkChoices(info, element, identity);
    }
    const PadTemplateInfo* linkSinkTemplate(int link) const { return views_.linkSinkTemplate(link); }

    // ---- edits (EditSession). Each one that changes what runs starts a validation.
    bool addElement(const QString& factory, const ElementEdit& edit, double x = kFree, double y = kFree);
    bool addAfter(const QString& element, const QString& pad, const QString& factory, const QString& sinkPad,
                  const ElementEdit& edit);
    bool linkExisting(const QString& element, const QString& pad, const QString& sinkElement, const QString& sinkPad);
    bool editElement(const QString& element, const ElementEdit& edit);
    bool setCaps(const QString& element, const QString& pad, const QString& caps, const gps::Props& sinkPadProps);
    bool cut(const QString& element, const QString& pad);
    bool disconnectLink(const QString& element, const QString& pad);
    bool replace(const QString& element, const QString& pad, const QString& factory, const QString& sinkPad,
                 const ElementEdit& edit);
    bool deleteSource(const QString& element) { return deleteSelection({element}, {}, false); }
    bool deleteElement(const QString& element, bool autoHeal = true) { return deleteSelection({element}, {}, autoHeal); }
    bool deleteSelection(const QStringList& elementNames, const QList<gps::LinkRef>& links = {}, bool autoHeal = true);
    bool spliceElement(const QString& element, const QString& linkSrcElement, const QString& linkSrcPad) {
        return spliceElements({element}, linkSrcElement, linkSrcPad);
    }
    bool spliceElements(const QStringList& elements, const QString& linkSrcElement, const QString& linkSrcPad);
    bool insertQueue(const QString& element, const QString& pad);
    bool removeQueue(const QString& queue);
    void setPosition(const QString& element, double x, double y) { setPositions({{element, QPointF(x, y)}}); }  // no validation
    void setPositions(const QList<QPair<QString, QPointF>>& positions);
    bool canUndo() const { return edits_.canUndo(); }
    bool canRedo() const { return edits_.canRedo(); }
    bool undo() { return edits_.undo(); }
    bool redo() { return edits_.redo(); }
    gps::Graph copySubplan(const QStringList& elementNames) const;
    QStringList pasteSubplan(const gps::Graph& clip, double targetX = kFree, double targetY = kFree);
    QStringList duplicateElements(const QStringList& names);
    bool connectSelectedElements(const QStringList& names);
    bool disconnectBetweenElements(const QStringList& names);
    // Nice alignment of the selected elements, or of the whole canvas when the list is empty: one undo step when it
    // lands (asynchronously, canvasLayout().aligned()). Leaves execution untouched.
    bool niceAlign(const QStringList& elements = {});
    CanvasLayout& canvasLayout() { return *layout_; }
    const CanvasLayout& canvasLayout() const { return *layout_; }

    // ---- files and runs. Opening is asynchronous: the plan is replaced when all its factories are known.
    void newPlan(std::unique_ptr<SaveTarget> target);  // an empty canvas bound to target: untitled, or a new project
    // The document bound to target, read from the target's file; or from backup (a hot-exit backup), modified.
    void open(std::unique_ptr<SaveTarget> target, const QString& backup = QString());
    void restoreVersion(const QString& version);  // an earlier version of the document, as one undoable change to it
    void reload();                                // the target's file again, as it is on disk now, unmodified
    void revert();                                // reload, when there are changes to drop
    bool canRevert() const;                       // modified, not busy, and the target's file is on disk
    bool importLaunch(const QString& description); // brief native probe, then opens an unsaved canvas graph
    SaveResult save() { return write(target_); }   // to the target
    SaveResult saveAs(std::unique_ptr<SaveTarget> target) { return write(std::move(target)); }  // written, bound to it
    bool refresh();  // validate again
    bool run() { return canRun() && runs_.run(plan()); }
    void stopRun() { runs_.stop(); }
    bool startMonitoring() { return canRun() && runs_.startMonitoring(plan()); }
    void stopMonitoring() { runs_.stopMonitoring(); }
    void stopAll() {  // the run and the monitor, whichever goes on: its tab closes
        stopRun();
        stopMonitoring();
    }

signals:
    void changed();                         // anything the views show has changed
    void synced();                          // it matches its target's file now: opened, reloaded, saved, or new
    void updateFinished(bool ok);
    void logLine(const QString& line);      // helper stderr and events
    void notice(const QString& text);       // something the user must be told (helper missing, open failed ...)
    void runFinished(const QString& status);
    void telemetryReceived(const gps::TelemetrySample& sample);
    void templateChanged(const QString& id);              // its content no longer matches its template: what now?
    void templatesOutdated(const QStringList& ids);       // the library holds newer versions of copies on this canvas

private:
    using Edit = EditSession::Edit;
    SupportInfo supportInfo(const QString& title) const;  // what the support report tells
    void log(const QString& line);
    std::function<void(const QString&)> logger();  // log() as a callback
    QString blockedReason() const;  // the edit-blocked guard's answer
    void committed(const Edit& edit, bool restoring);  // what an edit means for the other sessions
    void validated(bool ok);
    // How a plan read from a file becomes the document: it is the document's file (Open: unmodified, the undo history
    // starts again), its unsaved content from a backup (Recover: the same, but modified), or one undoable change of the
    // document (Replace).
    enum class Load { Open, Recover, Replace };
    // target: what the document is bound to once it is open; null keeps the one it has.
    void load(const QString& source, std::shared_ptr<SaveTarget> target, Load how, const QString& action);
    SaveResult write(std::shared_ptr<SaveTarget> target);  // the one save path of save and saveAs
    bool infosReady(const gps::Graph& clip, std::function<void()> retry);
    QStringList land(const ComponentRef& component, const QPointF& at, const std::optional<gps::PadRef>& after,
                     const std::function<void()>& retry);
    void alignAfterEdit();
    void alignOpened();
    void applyEdit(gps::Graph& g, const std::string& element, const ElementEdit& edit);
    void makeRoomForQueue(gps::Graph& g, const std::string& source, const std::string& queue);
    void placeNewElement(gps::Graph& g, const std::string& id);
    bool linkInOrder(gps::Graph& g, const QStringList& expanded, QString* lastTouched);

    // What GStreamer offers (gps-inspect), shared by every document; each fetched factory is registered with the Pipeline.
    FactoryCatalog& catalog() const { return services_.catalog(); }
    void registerFactory(const QString& factory);  // its definition, from the catalog, for the Pipeline

    AppServices& services_;
    gps::Pipeline pipeline_{"pipeline", [this](const std::string& factory) { return catalog().traits()(factory); }};
    std::shared_ptr<SaveTarget> target_ = std::make_shared<UntitledTarget>(1);
    EditSession edits_{pipeline_};
    ValidationSession validation_{pipeline_, services_.catalog()};
    RunSession runs_;
    CanvasLayout* layout_;
    TemplateInstances templates_;
    DocumentViews views_;
    bool opening_ = false;  // a file is being opened: its elements' info is on its way
    bool alignWhenValidated_ = false;  // an opened file is laid out once its first validation tells every pad
    bool alignQueued_ = false;         // automatic alignment follows the current burst of edits
    QTemporaryDir tmp_;                // where an imported pipeline is written
    DesktopUrlOpener desktopOpener_;
    UrlOpener* opener_ = &desktopOpener_;
    QStringList logTail_;
};
