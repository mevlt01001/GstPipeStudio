// controller.h – all GUI logic, no widgets (SPEC 8, UI_RULES 3). It holds the Plan, the element-info cache from gps-inspect,
// the last UpdateResult and the helper processes. Widgets read from it and call its edit functions.
//
// Every edit changes the Plan with a gps_plan rule and then starts one update in a helper process. Edits are refused
// (they return false) while an update or a run is in progress. The factories an edit names must be in the cache:
// call fetchInfo() first (the drawer pages need the info anyway). A plan on screen always has all its factories cached.
#pragma once

#include <QHash>
#include <QObject>
#include <QPointF>
#include <QTemporaryDir>
#include <functional>
#include <map>

#include "elementedit.h"
#include "elementinfo.h"
#include "elementview.h"
#include "helperclient.h"
#include "plan.h"
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
    const gps::Plan& plan() const { return plan_; }
    const UpdateResult& result() const { return result_; }
    bool updating() const { return updating_; }
    bool running() const { return running_; }
    bool busy() const { return updating_ || running_ || opening_; }
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

    // ---- what the canvas and the drawer show
    ElementView view(const QString& element) const;
    QList<LinkView> linkViews() const;
    QString linkSinkIdentity(int link) const;
    QList<StructureInfo> padStructures(const QString& element, const QString& identity) const;  // allowed by a src pad
    QString padAllowed(const QString& element, const QString& identity) const;
    QList<ExistingTarget> existingTargets(const QString& element, const QString& identity) const;
    QList<PadTemplateInfo> sinkChoices(const ElementInfo& info, const QString& element, const QString& identity) const;
    const PadTemplateInfo* linkSinkTemplate(int link) const;  // template of the sink pad a link ends on

    // ---- edits. Each starts an update.
    bool addSource(const QString& factory, const ElementEdit& edit);
    bool addAfter(const QString& element, const QString& pad, const QString& factory, const QString& sinkPad,
                  const ElementEdit& edit);
    bool linkExisting(const QString& element, const QString& pad, const QString& sinkElement, const QString& sinkPad);
    bool editElement(const QString& element, const ElementEdit& edit);
    bool setCaps(const QString& element, const QString& pad, const QString& caps, const gps::Props& sinkPadProps);
    bool cut(const QString& element, const QString& pad);
    bool replace(const QString& element, const QString& pad, const QString& factory, const QString& sinkPad,
                 const ElementEdit& edit);
    bool deleteSource(const QString& element);
    bool insertQueue(const QString& element, const QString& pad);
    bool removeQueue(const QString& queue);
    void setPosition(const QString& element, double x, double y);  // dragging; no update
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

signals:
    void changed();                         // anything the views show has changed
    void updateFinished(bool ok);
    void logLine(const QString& line);      // helper stderr and events
    void notice(const QString& text);       // something the user must be told (helper missing, open failed ...)
    void catalogReady();
    void environmentChecked();
    void runFinished(const QString& status);

private:
    void log(const QString& line);
    void note(const QString& action, const QString& linking = QString());  // what the support report says we did
    QString padText(const QString& element, const QString& identity) const;
    HelperClient* runHelper(const QString& name, const QStringList& args, int killAfterMs, std::function<void(const HelperResult&)> done);
    void fetchInfos(const QStringList& factories, std::function<void(bool)> done);
    bool beginEdit() const;
    void edited(const QString& touchedElement);  // dirty + update
    void startUpdate();
    void finishUpdate(const HelperResult& r);
    void applyEdit(const std::string& element, const ElementEdit& edit);
    void arrange();
    void makeRoomForQueue(const std::string& source, const std::string& queue);
    QPointF freeSpot() const;
    QString elementNote(const QString& element, bool* isError) const;

    gps::Plan plan_;
    UpdateResult result_;
    QString path_;
    bool dirty_ = false;
    bool updating_ = false;
    bool running_ = false;
    bool opening_ = false;  // a file is being opened: its elements' info is on its way
    int updateTimeoutMs_ = kUpdateBackstopMs;
    QString touched_;  // the element the last edit concerned; the suspect when the helper dies

    QTemporaryDir tmp_;
    QList<ListEntry> catalog_;
    bool catalogLoaded_ = false;
    std::map<QString, ElementInfo> infos_;  // node based: pointers handed out stay valid
    QHash<QString, QList<InfoCallback>> pendingInfo_;
    QHash<QString, QStringList> compatible_;
    HelperClient* runClient_ = nullptr;
    HelperClient* updateClient_ = nullptr;
    SlowWatch* watch_;
    bool cancelled_ = false;
    bool cancelling_ = false;

    StartupCheck* startup_;
    DesktopUrlOpener desktopOpener_;
    UrlOpener* opener_ = &desktopOpener_;
    QString action_, linking_;
    QStringList logTail_;
};
