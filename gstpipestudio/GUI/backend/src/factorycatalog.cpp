// factorycatalog.cpp – see factorycatalog.h.
#include "factorycatalog.h"

#include <memory>

#include "helperclient.h"

HelperClient* FactoryCatalog::ask(const QStringList& args, std::function<void(const HelperResult&)> done) {
    return HelperClient::run(this, "gps-inspect", args, 30000, [this](const QString& line) { emit logLine(line); }, std::move(done));
}

void FactoryCatalog::load() {
    ask({"list"}, [this](const HelperResult& r) {
        gps::FactoryList list;
        if (!r.read(&list)) {
            emit notice("Unable to read the element list: gps-inspect did not run or gave an unusable answer.");
            return;
        }
        entries_ = std::move(list.elements);
        loaded_ = true;
        emit logLine(QString("Element list: %1 factories").arg(entries_.size()));
        emit ready();
    });
}

const ElementInfo* FactoryCatalog::info(const QString& factory) const {
    auto found = infos_.find(factory);
    return found == infos_.end() ? nullptr : &found->second;
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

void FactoryCatalog::fetchInfo(const QString& factory, InfoCallback done) {
    if (const ElementInfo* cached = info(factory)) {
        done(cached);
        return;
    }
    pending_[factory].append(done);
    if (pending_[factory].size() > 1) return;  // a call for this factory is already on its way

    ask({"element", factory}, [this, factory](const HelperResult& r) {
        ElementInfo answer;
        if (r.read(&answer) && answer.entry.factory == toStd(factory)) {
            infos_[factory] = std::move(answer);
            emit infoArrived(factory);
        } else {
            emit notice(QString("Unable to read the information of element '%1'.").arg(factory));
        }
        QList<InfoCallback> waiting = pending_.take(factory);
        for (const InfoCallback& cb : waiting) cb(info(factory));
    });
}

void FactoryCatalog::fetchInfos(const QStringList& factories, std::function<void(bool)> done) {
    if (factories.isEmpty()) {
        done(true);
        return;
    }
    auto remaining = std::make_shared<int>(factories.size());
    auto failed = std::make_shared<bool>(false);
    for (const QString& f : factories) {
        fetchInfo(f, [remaining, failed, done](const ElementInfo* i) {
            if (!i) *failed = true;
            if (--*remaining == 0) done(!*failed);
        });
    }
}

void FactoryCatalog::fetchCompatible(const QString& caps, NamesCallback done) {
    auto cached = compatible_.constFind(caps);
    if (cached != compatible_.constEnd()) {
        done(cached.value());
        return;
    }
    QStringList args = {"compatible"};
    if (!caps.isEmpty()) args << "--caps" << caps;
    ask(args, [this, caps, done](const HelperResult& r) {
        QStringList names;
        gps::CompatibleFactories answer;
        if (r.read(&answer)) {
            names = fromStd(answer.factories);
            compatible_.insert(caps, names);
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
