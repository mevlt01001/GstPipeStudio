// customelements.cpp – see customelements.h.
#include "customelements.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>
#include <algorithm>

#include "contract.h"
#include "factorycatalog.h"

QString CustomElementLibrary::directory() {
    QString env = qEnvironmentVariable("GPS_CUSTOM_ELEMENTS_DIR");
    if (!env.isEmpty()) return env;
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + "/custom_elements";
}

QString CustomElementLibrary::fileFor(const QString& name) {
    QString safe = name;
    safe.replace(QRegularExpression("[^A-Za-z0-9_.-]"), "_");
    return QDir(directory()).filePath(safe + ".conf");
}

void CustomElementLibrary::load(const FactoryCatalog& catalog) {
    list_.clear();
    plans_.clear();
    QDir dir(directory());
    if (!dir.exists()) return;
    for (const QString& f : dir.entryList({"*.conf"}, QDir::Files, QDir::Name)) {
        gps::Graph sub;
        std::string err;
        if (!gps::Graph::load(toStd(dir.filePath(f)), &sub, &err) || sub.elements.empty()) continue;
        CustomElementInfo ci;
        ci.name = sub.name.empty() ? QFileInfo(f).completeBaseName() : fromStd(sub.name);
        ci.elementCount = static_cast<int>(sub.elements.size());
        QStringList factories;
        for (const gps::Element& e : sub.elements) {
            factories << fromStd(e.factory);
            const FactoryEntry* entry = catalog.entry(fromStd(e.factory));
            if (!(entry && entry->isSource) && sub.linksInto(e.name).empty()) ci.hasFreeSink = true;
            if (!(entry && entry->isSink) && sub.linksFromElement(e.name).empty()) ci.hasFreeSrc = true;
        }
        ci.description = factories.join(" → ");
        ci.isSource = !ci.hasFreeSink && ci.hasFreeSrc;
        ci.isSink = ci.hasFreeSink && !ci.hasFreeSrc;
        plans_[ci.name] = sub;
        list_ << ci;
    }
}

const gps::Graph* CustomElementLibrary::plan(const QString& name) const {
    auto it = plans_.find(name);
    return it == plans_.end() ? nullptr : &it->second;
}

bool CustomElementLibrary::save(const QString& name, gps::Graph preset, QString* error) {
    preset.name = toStd(name);
    double left = preset.elements.front().x, top = preset.elements.front().y;
    for (const gps::Element& e : preset.elements) left = std::min(left, e.x), top = std::min(top, e.y);
    for (gps::Element& e : preset.elements) {
        e.x -= left;
        e.y -= top;
        e.group = toStd(name + "#0");
    }
    if (!QDir(directory()).mkpath(".")) {
        *error = "cannot create " + directory();
        return false;
    }
    std::string failure;
    if (preset.save(toStd(fileFor(name)), &failure)) return true;
    *error = fromStd(failure);
    return false;
}

bool CustomElementLibrary::remove(const QString& name) { return QFile::remove(fileFor(name)); }
