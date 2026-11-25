// Controller owns the Pipeline document and coordinates GUI workflows and helper processes.
// Widgets read views and submit edits; GraphEditor owns transactions and history.
// Preview updates are coalesced and allow further edits. Playback, opening and cancellation block edits.
// Call services().catalog().fetchInfo(factory, context, …) before edits that refer to a new factory.
#pragma once

#include <QHash>
#include <QObject>
#include <QPair>
#include <QPointF>
#include <QProcess>
#include <QSet>
#include <QTemporaryDir>
#include <QTimer>
#include <functional>
#include <map>
#include <optional>
#include <limits>
#include <cmath>

#include "appservices.h"
#include "elementedit.h"
#include "contract.h"
#include "elementview.h"
#include "helperclient.h"
#include "canvaslayout.h"
#include "executionbackend.h"
#include "pipeline.h"
#include "slowwatch.h"
#include "supportreport.h"

// A place a free src pad can be linked to inside the plan.
struct ExistingTarget {
    QString element, factory, sinkPad;
    bool request = false;  // true: a new pad of a Request sink template ("Request a pad")
};

// gps-update gets this as --timeout-ms: only a backstop. The user decides about slow runs (SlowWatch).
constexpr int kUpdateBackstopMs = 600000;

class Controller : public QObject {
    Q_OBJECT
public:

    // One per document; the services are the process's, shared with the other documents.
    explicit Controller(AppServices& services, QObject* parent = nullptr);
    AppServices& services() const { return services_; }

    // ---- state
    const gps::Graph& plan() const { return pipeline_.editor().view(); }
    const gps::Pipeline& pipeline() const { return pipeline_; }
    const UpdateResult& result() const { return pipeline_.observations(); }  // the last validation, owned by Pipeline
    bool updating() const { return updating_; }
    bool running() const { return running_; }
    bool busy() const { return updating_ || running_ || monitoring_ || opening_; }
    bool editingBlocked() const { return running_ || monitoring_ || opening_ || cancelling_; }
    bool monitoring() const { return monitoring_; }
    bool dirty() const { return dirty_; }
    QString filePath() const { return path_; }
    bool canSave() const;  // not empty, not busy, validated, last update did not fail
    bool canRun() const;   // canSave and the pipeline is complete
    QString saveBlockedReason() const;  // why Save is off, "" when it is on
    QString runBlockedReason() const;   // why Run is off, "" when it is on
    bool notValidated() const { return cancelled_; }  // the user cancelled the last validation
    bool helperStopped() const;                       // the last update's helper crashed or gave no usable answer
    QString launchCommand() const;  // the gst-launch-1.0 line for the saved file, "" before the first save
    void setUpdateTimeoutMs(int ms) { updateTimeoutMs_ = ms; }

    // ---- slow validation and cancel (UI_RULES 3.1 and 3.2)
    SlowWatch* slowWatch() const { return watch_; }
    bool slowAsked() const { return updating_ && watch_->asking(); }
    bool cancelling() const { return cancelling_; }
    void cancelUpdate();  // SIGTERM, SIGKILL after 2 s; the plan keeps its change and is "not validated"
    void waitMore();      // the user chose Wait: the next question comes 15 s later

    // ---- support
    void setUrlOpener(UrlOpener* opener) { opener_ = opener; }
    SupportInfo supportInfo(const QString& title) const;
    void requestSupport(const QString& title);  // opens the prefilled issue in the browser

    // ---- templates (saved multi-element presets & canvas groups); the library itself is services().library()
    // Making a template: the selection becomes an expanded instance at once, so its border is drawn and the canvas
    // makes room; finish names it, gives it its parameters and writes it to the library; cancel takes it back.
    QString beginTemplate(const QStringList& elements);  // the new instance, "" when refused
    bool finishTemplate(const QString& id, const QString& name, const std::vector<gps::ExposedParam>& params, QString* error);
    void cancelTemplate(const QString& id);
    bool saveTemplate(const QString& templateName, const QStringList& elementNames);  // all three, no parameters, collapsed
    std::vector<gps::ExposedParam> parameterChoices(const QStringList& elements) const;  // what Add parameter offers
    // A template copy's content changed (templateChanged): the user chooses. Keep editing: the library gets the change
    // as the template's next revision when the copy collapses or the file is saved, and the unchanged copies follow.
    void keepEditingTemplate(const QString& id);
    bool isEditingTemplate(const QString& id) const { return editingTemplates_.contains(id); }
    bool saveTemplateAsNew(const QString& id, const QString& name, QString* error);
    QStringList outdatedTemplates() const;           // copies whose library template is newer
    bool updateTemplates(const QStringList& ids);    // they take the library's newer content: one undo step
    QString lastAction() const { return action_; }   // what the last edit did, in words
    // A placed template's parameters: the collapsed box shows these as its properties.
    std::vector<gps::ExposedParam> templateParams(const QString& id) const;
    bool setTemplateParams(const QString& id, const std::map<std::string, std::string>& values);  // name -> value, "" = default

    // ---- the template library, like a folder of modules: what a project imports
    bool renameTemplate(const QString& from, const QString& to, QString* error);  // the library renames it; copies here follow
    // A library template on the canvas as the document: Save writes its next revision to the library, and its
    // parameters (the plan's exposed list) are set with setTemplateParameters.
    void editLibraryTemplate(const QString& name);
    QString libraryTemplate() const { return libraryTemplate_; }  // the one being edited, "" for a pipeline
    void setTemplateParameters(const std::vector<gps::ExposedParam>& params);
    QStringList addTemplate(const QString& templateName, double x = std::numeric_limits<double>::quiet_NaN(), double y = std::numeric_limits<double>::quiet_NaN());
    QStringList addTemplateAfter(const QString& srcElement, const QString& srcPad, const QString& templateName);
    bool detachTemplate(const QString& groupId);
    bool isTemplateExpanded(const QString& groupId) const { return expandedTemplates_.contains(groupId); }
    void setTemplateExpanded(const QString& groupId, bool expanded);
    QStringList templateMembers(const QString& groupId) const;
    QString templateBox(const QString& elementName) const;  // the collapsed instance it is drawn in, "" when none
    QStringList expandSelection(const QStringList& elementNames) const;

    // ---- what the canvas and the drawer show
    QString displayName(const QString& element) const;  // what the user reads: "filesrc", "filesrc 2"…
    ElementView view(const QString& element) const;
    ElementView templateView(const QString& groupId) const;
    FrameView frameView(const QString& instanceId) const;
    QStringList visibleFrames() const;  // expanded templates whose every enclosing template is expanded too
    QList<LinkView> linkViews() const;
    QString linkSinkIdentity(int link) const;
    std::vector<StructureInfo> padStructures(const QString& element, const QString& identity) const;  // allowed by a src pad
    QString padAllowed(const QString& element, const QString& identity) const;
    QList<ExistingTarget> existingTargets(const QString& element, const QString& identity) const;
    std::vector<PadTemplateInfo> sinkChoices(const ElementInfo& info, const QString& element, const QString& identity) const;
    const PadTemplateInfo* linkSinkTemplate(int link) const;  // template of the sink pad a link ends on

    // ---- edits. Each starts an update.
    bool addElement(const QString& factory, const ElementEdit& edit, double x = std::numeric_limits<double>::quiet_NaN(), double y = std::numeric_limits<double>::quiet_NaN());
    bool addAfter(const QString& element, const QString& pad, const QString& factory, const QString& sinkPad,
                  const ElementEdit& edit);
    bool linkExisting(const QString& element, const QString& pad, const QString& sinkElement, const QString& sinkPad);
    bool editElement(const QString& element, const ElementEdit& edit);
    bool setCaps(const QString& element, const QString& pad, const QString& caps, const gps::Props& sinkPadProps);
    bool cut(const QString& element, const QString& pad);
    bool disconnectLink(const QString& element, const QString& pad);
    bool replace(const QString& element, const QString& pad, const QString& factory, const QString& sinkPad,
                 const ElementEdit& edit);
    bool deleteSource(const QString& element);
    bool deleteElement(const QString& element, bool autoHeal = true);
    bool deleteSelection(const QStringList& elementNames, const QList<QPair<QString, QString>>& links = {}, bool autoHeal = true);
    bool spliceElement(const QString& element, const QString& linkSrcElement, const QString& linkSrcPad);
    bool spliceElements(const QStringList& elements, const QString& linkSrcElement, const QString& linkSrcPad);
    bool insertQueue(const QString& element, const QString& pad);
    bool removeQueue(const QString& queue);
    void setPosition(const QString& element, double x, double y);  // dragging; no update
    void setPositions(const QList<QPair<QString, QPointF>>& positions);
    bool canUndo() const { return pipeline_.editor().canUndo() && !editingBlocked(); }
    bool canRedo() const { return pipeline_.editor().canRedo() && !editingBlocked(); }
    bool undo();
    bool redo();
    gps::Graph copySubplan(const QStringList& elementNames) const;
    QStringList pasteSubplan(const gps::Graph& clip, double targetX = std::numeric_limits<double>::quiet_NaN(), double targetY = std::numeric_limits<double>::quiet_NaN());
    QStringList duplicateElements(const QStringList& names);
    bool connectSelectedElements(const QStringList& names);
    bool disconnectBetweenElements(const QStringList& names);
    // Nice alignment of the selected elements, or of the whole canvas when the list is empty: one undo step when it
    // lands (asynchronously, canvasLayout().aligned()). Leaves execution untouched.
    bool niceAlign(const QStringList& elements = {});
    CanvasLayout& canvasLayout() { return *layout_; }
    const CanvasLayout& canvasLayout() const { return *layout_; }
    gps::Removal previewCut(const QString& element, const QString& pad) const;
    gps::Removal previewReplace(const QString& element, const QString& pad) const;
    gps::Removal previewDeleteSource(const QString& element) const;

    // ---- files and runs
    void newPlan();
    void newProject(const QString& confPath);  // an empty canvas whose Save writes there (its folder exists)
    void open(const QString& path, bool imported = false);  // asynchronous: the plan is replaced when all its factories are known
    void restoreVersion(const QString& version);  // an earlier version of the open file, as an unsaved change to it
    bool importLaunch(const QString& description); // brief native probe, then opens an unsaved canvas graph
    bool save(const QString& path, QString* error);  // path "" saves where the plan was opened or saved
    bool refresh();                  // run the update again
    bool run();
    void stopRun();
    bool startMonitoring();
    void stopMonitoring();

signals:
    void changed();                         // anything the views show has changed
    void updateFinished(bool ok);
    void logLine(const QString& line);      // helper stderr and events
    void notice(const QString& text);       // something the user must be told (helper missing, open failed ...)
    void runFinished(const QString& status);
    void telemetryReceived(const gps::TelemetrySample& sample);
    void templateChanged(const QString& id);              // its content no longer matches its template: what now?
    void templatesOutdated(const QStringList& ids);       // the library holds newer versions of copies on this canvas

private:
    void log(const QString& line);
    void note(const QString& action, const QString& linking = QString());  // what the support report says we did
    QString padText(const QString& element, const QString& identity) const;
    std::function<void(const QString&)> logger();  // log() as a callback
    bool move(const QList<QPair<QString, QPointF>>& placements);  // one undoable presentation change
    // What one user edit says about itself; its change fills it in.
    struct Edit {
        QString action, linking;       // for the support report
        QString touched;               // the element it concerned: the suspect when the validator dies
        bool presentation = false;     // groups and positions only: committed without any validation
    };
    // The one path of an edit: refused while editing is blocked or when change returns false (nothing happened);
    // otherwise one undo step, noted, and validated when it changed what runs (the Pipeline tells).
    bool transact(const std::function<bool(gps::Graph&, Edit&)>& change);
    void load(const QString& source, const QString& documentPath, bool dirty, const QString& action, const QString& libraryTemplate = {});
    void checkTemplates(bool ask);
    void offerNewerTemplates();  // templatesOutdated soon, when the library holds newer versions of copies on this canvas
    void offerNow();
    bool commitTemplateEdit(const QString& id);
    bool onCanvas(const QString& instanceId, QString* error) const;  // false, with the reason, when it is gone
    bool infosReady(const gps::Graph& clip, std::function<void()> retry);
    static QPointF topLeft(const gps::Graph& clip);
    QString runFile(const QString& name);
    gps::Graph& editGraph() { return pipeline_.editor().draft(); }
    bool commitTransaction();
    void edited(const QString& touchedElement);  // dirty, and validated when the last change was an Execution change
    void alignAfterEdit();
    void alignOpened();
    void startUpdate();
    void dispatchUpdate();
    void finishUpdate(const gps::ExecutionResult& r);
    void applyEdit(const std::string& element, const ElementEdit& edit);
    void makeRoomForQueue(const std::string& source, const std::string& queue);
    QPointF freeSpot() const;
    void placeNewElement(const std::string& id);
    QString elementNote(const QString& element, bool* isError) const;
    void subgraphExternalPads(const QStringList& members, QList<PadView>* outSinks, QList<PadView>* outSrcs) const;
    bool linkOrderedInternal(const QStringList& expanded, QString* lastTouched = nullptr);

    // What GStreamer offers (gps-inspect), shared by every document; each fetched factory is registered with the Pipeline.
    FactoryCatalog& catalog() const { return services_.catalog(); }
    void registerFactory(const QString& factory);  // its definition, from the catalog, for the Pipeline

    AppServices& services_;
    gps::Pipeline pipeline_{"pipeline", [this](const std::string& factory) { return catalog().traits()(factory); }};
    HelperExecutionBackend backend_;
    QTimer updateTimer_;
    gps::ExecutionCoordinator execution_{pipeline_, backend_};
    QString path_;
    bool dirty_ = false;
    bool updating_ = false;
    bool running_ = false;
    bool monitoring_ = false;
    bool opening_ = false;  // a file is being opened: its elements' info is on its way
    int updateTimeoutMs_ = kUpdateBackstopMs;
    QString touched_;  // the element the last edit concerned; the suspect when the helper dies

    QTemporaryDir tmp_;
    CanvasLayout* layout_;
    bool alignWhenValidated_ = false;
    QString libraryTemplate_;     // the library template the canvas is, while it is edited
    bool restoring_ = false;     // undo or redo is running
    bool alignQueued_ = false;   // automatic alignment follows the current burst of edits  // an opened file is laid out once its first validation tells every pad
    QSet<QString> editingTemplates_;   // copies whose changes go to their template (Update template was chosen)
    QSet<QString> knownModified_;      // copies already known to differ from their template
    QSet<QString> expandedTemplates_;
    HelperClient* runClient_ = nullptr;
    QProcess* monitorClient_ = nullptr;
    QByteArray monitorOutput_;
    SlowWatch* watch_;
    bool cancelled_ = false;
    bool cancelling_ = false;

    QTimer offerTimer_;  // coalesces the templatesOutdated offers of one burst of library changes
    QHash<QString, int> offered_;  // copy id -> the library revision it was last offered
    DesktopUrlOpener desktopOpener_;
    UrlOpener* opener_ = &desktopOpener_;
    QString action_, linking_;
    QStringList logTail_;
};
