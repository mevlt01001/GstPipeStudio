// Controller owns the Pipeline document and coordinates GUI workflows and helper processes.
// Widgets read views and submit edits; GraphEditor owns transactions and history.
// Preview updates are coalesced and allow further edits. Playback, opening and cancellation block edits.
// Call fetchInfo() before edits that refer to a new factory.
#pragma once

#include <QHash>
#include <QObject>
#include <QPair>
#include <QPointF>
#include <QProcess>
#include <QJsonObject>
#include <QSet>
#include <QTemporaryDir>
#include <QTimer>
#include <functional>
#include <map>
#include <limits>
#include <cmath>

#include "elementedit.h"
#include "elementinfo.h"
#include "elementview.h"
#include "helperclient.h"
#include "executionbackend.h"
#include "pipeline.h"
#include "slowwatch.h"
#include "startupcheck.h"
#include "supportreport.h"
#include "updateresult.h"

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
    using InfoCallback = std::function<void(const ElementInfo*)>;
    using NamesCallback = std::function<void(const QStringList&)>;

    explicit Controller(QObject* parent = nullptr);

    // ---- state
    const gps::Graph& plan() const { return pipeline_.editor().view(); }
    const gps::Pipeline& pipeline() const { return pipeline_; }
    const UpdateResult& result() const { return result_; }
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

    // ---- first run and support
    void checkEnvironment();
    const StartupResult& environment() const { return startup_->result(); }
    bool environmentBlocked() const { return startup_->result().bad(); }
    void setUrlOpener(UrlOpener* opener) { opener_ = opener; }
    SupportInfo supportInfo(const QString& title) const;
    void requestSupport(const QString& title);  // opens the prefilled issue in the browser

    // ---- catalog (gps-inspect)
    void loadCatalog();
    bool catalogLoaded() const { return catalogLoaded_; }
    const QList<ListEntry>& catalog() const { return catalog_; }
    const ElementInfo* info(const QString& factory) const;
    void fetchInfo(const QString& factory, InfoCallback done);  // calls done at once when cached; nullptr on failure
    void fetchCompatible(const QString& caps, NamesCallback done);  // ranked factories that can follow these caps
    gps::TraitsFn traits() const;

    // ---- custom elements (saved multi-element presets & canvas groups)
    static QString customElementsDir();
    void loadCustomElements();
    const QList<CustomElementInfo>& customElements() const { return customElements_; }
    const gps::Graph* customElementPlan(const QString& name) const;
    bool saveCustomElement(const QString& customName, const QStringList& elementNames, const QString& description = QString());
    bool removeCustomElement(const QString& customName);
    QStringList addCustomElement(const QString& customName, double x = std::numeric_limits<double>::quiet_NaN(), double y = std::numeric_limits<double>::quiet_NaN());
    QStringList addCustomElementAfter(const QString& srcElement, const QString& srcPad, const QString& customName);
    bool groupSelectedElements(const QStringList& elementNames, const QString& groupTitle);
    bool ungroupElements(const QString& groupId);
    bool isGroupExpanded(const QString& groupId) const { return expandedGroups_.contains(groupId); }
    void setGroupExpanded(const QString& groupId, bool expanded);
    QStringList groupMembers(const QString& groupId) const;
    QString elementGroup(const QString& elementName) const;
    QStringList expandSelectionWithGroups(const QStringList& elementNames) const;

    // ---- what the canvas and the drawer show
    ElementView view(const QString& element) const;
    ElementView groupView(const QString& groupId) const;
    QList<LinkView> linkViews() const;
    QString linkSinkIdentity(int link) const;
    QList<StructureInfo> padStructures(const QString& element, const QString& identity) const;  // allowed by a src pad
    QString padAllowed(const QString& element, const QString& identity) const;
    QList<ExistingTarget> existingTargets(const QString& element, const QString& identity) const;
    QList<PadTemplateInfo> sinkChoices(const ElementInfo& info, const QString& element, const QString& identity) const;
    const PadTemplateInfo* linkSinkTemplate(int link) const;  // template of the sink pad a link ends on

    // ---- edits. Each starts an update.
    bool addSource(const QString& factory, const ElementEdit& edit);
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
    bool alignElementsHorizontally(const QStringList& names);
    bool alignElementsVertically(const QStringList& names);
    bool arrangePipeline(); // one undoable presentation action; leaves execution untouched
    gps::Removal previewCut(const QString& element, const QString& pad) const;
    gps::Removal previewReplace(const QString& element, const QString& pad) const;
    gps::Removal previewDeleteSource(const QString& element) const;

    // ---- files and runs
    void newPlan();
    void open(const QString& path);  // asynchronous: the plan is replaced when all its factories are known
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
    void catalogReady();
    void environmentChecked();
    void runFinished(const QString& status);
    void telemetryReceived(const QJsonObject& sample);

private:
    void log(const QString& line);
    void note(const QString& action, const QString& linking = QString());  // what the support report says we did
    QString padText(const QString& element, const QString& identity) const;
    HelperClient* runHelper(const QString& name, const QStringList& args, int killAfterMs, std::function<void(const HelperResult&)> done);
    void fetchInfos(const QStringList& factories, std::function<void(bool)> done);
    bool beginEdit() const;
    void beginTransaction();
    gps::Graph& editGraph() { return pipeline_.editor().draft(); }
    bool commitTransaction();
    void edited(const QString& touchedElement, bool validate = true);  // dirty + optional update
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

    gps::Pipeline pipeline_;
    HelperExecutionBackend backend_;
    QTimer updateTimer_;
    gps::ExecutionCoordinator execution_{pipeline_, backend_};
    UpdateResult result_;
    QString path_;
    bool dirty_ = false;
    bool updating_ = false;
    bool running_ = false;
    bool monitoring_ = false;
    bool opening_ = false;  // a file is being opened: its elements' info is on its way
    int updateTimeoutMs_ = kUpdateBackstopMs;
    QString touched_;  // the element the last edit concerned; the suspect when the helper dies

    QTemporaryDir tmp_;
    QList<ListEntry> catalog_;
    bool catalogLoaded_ = false;
    QList<CustomElementInfo> customElements_;
    std::map<QString, gps::Graph> customPlans_;
    QSet<QString> expandedGroups_;
    int nextGroupId_ = 1;
    std::map<QString, ElementInfo> infos_;  // node based: pointers handed out stay valid
    QHash<QString, QList<InfoCallback>> pendingInfo_;
    QHash<QString, QStringList> compatible_;
    HelperClient* runClient_ = nullptr;
    QProcess* monitorClient_ = nullptr;
    QByteArray monitorOutput_;
    SlowWatch* watch_;
    bool cancelled_ = false;
    bool cancelling_ = false;

    StartupCheck* startup_;
    DesktopUrlOpener desktopOpener_;
    UrlOpener* opener_ = &desktopOpener_;
    QString action_, linking_;
    QStringList logTail_;
};
