// factorycatalog.cpp – see factorycatalog.h.
#include "factorycatalog.h"

#include <QPointer>
#include <memory>
#include <algorithm>

#include "helperclient.h"

namespace {
// done, called only while context exists.
template <typename Callback>
Callback whileAlive(QObject* context, Callback done) {
    return [context = QPointer<QObject>(context), done = std::move(done)](auto&&... args) {
        if (context) done(std::forward<decltype(args)>(args)...);
    };
}
}  // namespace

HelperClient* FactoryCatalog::ask(const QStringList& args, std::function<void(const HelperResult&)> done) {
    return HelperClient::run(this, "gps-inspect", args, 30000, [this](const QString& line) { emit logLine(line); }, std::move(done));
}

void FactoryCatalog::load() { reloadList(); }

void FactoryCatalog::reloadList(std::function<void(bool)> done) {
    const auto request = ++listRequest_;
    const auto revision = registryRevision_;
    ask({"list"}, [this, request, revision, done = std::move(done)](const HelperResult& r) mutable {
        if (request != listRequest_) {
            if (done) done(false);
            return;
        }
        if (revision != registryRevision_) {
            reloadList(std::move(done));
            return;
        }
        gps::FactoryList list;
        if (!r.read(&list)) {
            emit notice("Unable to read the element list: gps-inspect did not run or gave an unusable answer.");
            if (done) done(false);
            return;
        }
        entries_ = std::move(list.elements);
        compatible_.clear();
        loaded_ = true;
        emit logLine(QString("Element list: %1 factories").arg(entries_.size()));
        emit ready();
        if (done) done(true);
    });
}

const ElementInfo* FactoryCatalog::info(const QString& factory) const {
    auto found = infos_.find(factory);
    return found == infos_.end() ? nullptr : &found->second;
}

const gps::FactoryDefinition* FactoryCatalog::definition(const QString& factory) const {
    auto found = definitions_.find(factory);
    return found == definitions_.end() ? nullptr : &found->second;
}

void FactoryCatalog::forEachInfo(const std::function<void(const QString&, const ElementInfo&)>& visit) const {
    for (const auto& [factory, info] : infos_) visit(factory, info);
}

const FactoryEntry* FactoryCatalog::entry(const QString& factory) const {
    if (const ElementInfo* i = info(factory)) return &i->entry;
    const std::string name = toStd(factory);
    for (const FactoryEntry& e : entries_) {
        if (e.factory == name) return &e;
    }
    return nullptr;
}

void FactoryCatalog::fetchInfo(const QString& factory, QObject* context, InfoCallback done) {
    if (const ElementInfo* cached = info(factory)) {
        done(cached);
        return;
    }
    pending_[factory].append(whileAlive(context, std::move(done)));
    if (infoRequests_.contains(factory)) return;  // a call for this factory is already on its way

    requestInfo(factory, false, {});
}

void FactoryCatalog::reloadFactory(const QString& factory, std::function<void(bool)> done) {
    requestInfo(factory, true, std::move(done));
}

void FactoryCatalog::requestInfo(const QString& factory, bool reload, std::function<void(bool)> done) {
    const auto request = ++infoRequest_;
    infoRequests_.insert(factory, request);
    ask({"element", factory}, [this, factory, reload, request, done = std::move(done)](const HelperResult& r) {
        if (request != infoRequests_.value(factory)) {
            if (done) done(false);
            return;
        }
        infoRequests_.remove(factory);
        ElementInfo answer;
        const auto name = toStd(factory);
        const bool ok = r.read(&answer) && answer.entry.factory == name;
        if (ok) {
            definitions_[factory] = gps::definitionOf(answer);
            infos_[factory] = std::move(answer);
            if (reload) {
                ++registryRevision_;
                compatible_.clear();
                const auto row = std::lower_bound(entries_.begin(), entries_.end(), name,
                    [](const FactoryEntry& entry, const std::string& key) { return entry.factory < key; });
                if (row == entries_.end() || row->factory != name) entries_.insert(row, infos_.at(factory).entry);
                else *row = infos_.at(factory).entry;
            }
            emit infoArrived(factory);
            if (reload) emit factoryReloaded(factory);
        } else {
            emit notice(QString("Unable to read the information of element '%1'.").arg(factory));
        }
        const auto waiting = pending_.take(factory);
        for (const auto& cb : waiting) cb(info(factory));
        if (done) done(ok);
    });
}

void FactoryCatalog::fetchInfos(const QStringList& factories, QObject* context, std::function<void(bool)> done) {
    if (factories.isEmpty()) {
        done(true);
        return;
    }
    auto remaining = std::make_shared<int>(factories.size());
    auto failed = std::make_shared<bool>(false);
    for (const QString& f : factories) {
        fetchInfo(f, context, [remaining, failed, done](const ElementInfo* i) {
            if (!i) *failed = true;
            if (--*remaining == 0) done(!*failed);
        });
    }
}

QStringList FactoryCatalog::missing(const gps::Graph& graph) const {
    QStringList out;
    for (const std::string& factory : graph.factories()) {
        if (!info(fromStd(factory))) out << fromStd(factory);
    }
    return out;
}

void FactoryCatalog::fetchCompatible(const QString& caps, QObject* context, NamesCallback done) {
    auto cached = compatible_.constFind(caps);
    if (cached != compatible_.constEnd()) {
        done(cached.value());
        return;
    }
    QStringList args = {"compatible"};
    if (!caps.isEmpty()) args << "--caps" << caps;
    const auto revision = registryRevision_;
    ask(args, [this, caps, revision, context = QPointer<QObject>(context), done = std::move(done)](const HelperResult& r) {
        if (!context) return;
        if (revision != registryRevision_) {
            fetchCompatible(caps, context, done);
            return;
        }
        QStringList names;
        gps::CompatibleFactories answer;
        if (r.read(&answer)) {
            names = fromStd(answer.factories);
            compatible_.insert(caps, names);
        } else {
            emit notice("Unable to query compatible elements from gps-inspect.");
        }
        done(names);
    });
}

gps::TraitsFn FactoryCatalog::traits() const {
    return [this](const std::string& factory) {
        gps::FactoryTraits t;
        if (const FactoryEntry* e = entry(fromStd(factory))) t.isSource = e->isSource;
        return t;
    };
}
