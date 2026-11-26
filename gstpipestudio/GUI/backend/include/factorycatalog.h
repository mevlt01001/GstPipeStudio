// factorycatalog.h – what this installation's GStreamer offers, asked from gps-inspect once and kept for the session:
// the factory list, each factory's full information on first use, and the factories that can follow some caps.
#pragma once

#include <QHash>
#include <QObject>
#include <QStringList>
#include <functional>
#include <map>
#include <vector>

#include "contract.h"
#include "catalog.h"
#include "graph.h"

class HelperClient;
struct HelperResult;

class FactoryCatalog : public QObject {
    Q_OBJECT
public:
    using InfoCallback = std::function<void(const ElementInfo*)>;
    using NamesCallback = std::function<void(const QStringList&)>;

    explicit FactoryCatalog(QObject* parent = nullptr) : QObject(parent) {}

    void load();  // the factory list; ready() when it arrived
    bool loaded() const { return loaded_; }
    const std::vector<FactoryEntry>& entries() const { return entries_; }

    const ElementInfo* info(const QString& factory) const;  // nullptr until fetchInfo() brought it
    const gps::FactoryDefinition* definition(const QString& factory) const;  // its pad templates, as the Pipeline reads them
    void forEachInfo(const std::function<void(const QString&, const ElementInfo&)>& visit) const;  // every one fetched so far
    // The factory's row: from its fetched information, else from the list; nullptr when neither knows it.
    const FactoryEntry* entry(const QString& factory) const;
    // The questions to gps-inspect. The catalog serves every tab, so each answer goes to done only while context (the
    // asking tab's object) still exists: a tab closed meanwhile is not called back.
    void fetchInfo(const QString& factory, QObject* context, InfoCallback done);  // done at once when cached; nullptr on failure
    void fetchInfos(const QStringList& factories, QObject* context, std::function<void(bool)> done);
    QStringList missing(const gps::Graph& graph) const;  // the factories it uses that have no information yet
    void fetchMissing(const gps::Graph& graph, QObject* context, std::function<void(bool)> done) {
        fetchInfos(missing(graph), context, std::move(done));
    }
    void fetchCompatible(const QString& caps, QObject* context, NamesCallback done);  // ranked factories that can follow these caps
    gps::TraitsFn traits() const;

signals:
    void ready();
    void infoArrived(const QString& factory);  // before the callbacks that waited for it run
    void notice(const QString& text);
    void logLine(const QString& line);

private:
    HelperClient* ask(const QStringList& args, std::function<void(const HelperResult&)> done);

    std::vector<FactoryEntry> entries_;
    bool loaded_ = false;
    std::map<QString, ElementInfo> infos_;  // node based: pointers handed out stay valid
    std::map<QString, gps::FactoryDefinition> definitions_;  // one for every info
    QHash<QString, QList<InfoCallback>> pending_;
    QHash<QString, QStringList> compatible_;
};
