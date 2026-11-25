// templatelibrary.cpp – see templatelibrary.h.
#include "templatelibrary.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>
#include <algorithm>

#include "apppaths.h"
#include "contract.h"
#include "factorycatalog.h"
#include "templates.h"
#include "versionhistory.h"

// Until 0.3 the library lived in the config folder as custom_elements/; its files move once.
QString TemplateLibrary::directory() {
    const QString library = AppPaths::templates();
    QDir old(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + "/custom_elements");
    if (qEnvironmentVariableIsEmpty("GPS_USER_DIR") && old.exists()) {
        for (const QString& f : old.entryList({"*.conf"}, QDir::Files)) QFile::rename(old.filePath(f), QDir(library).filePath(f));
        old.rmdir(".");
    }
    return library;
}

QString TemplateLibrary::fileFor(const QString& name) {
    QString safe = name;
    safe.replace(QRegularExpression("[^A-Za-z0-9_.-]"), "_");
    return QDir(directory()).filePath(safe + ".conf");
}

void TemplateLibrary::load(const FactoryCatalog& catalog) {
    list_.clear();
    plans_.clear();
    QDir dir(directory());
    if (!dir.exists()) return;
    for (const QString& f : dir.entryList({"*.conf"}, QDir::Files, QDir::Name)) {
        gps::Graph sub;
        std::string err;
        if (!gps::Graph::load(toStd(dir.filePath(f)), &sub, &err) || sub.elements.empty()) continue;
        // Library files before 0.4 tagged every element with "<name>#0": that is the template itself, not one inside it.
        for (const gps::TemplateInstance& t : std::vector<gps::TemplateInstance>(sub.instances)) {
            if (t.parent.empty() && t.templateName == sub.name) gps::templates::detach(sub, t.id);
        }
        TemplateInfo ci;
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

const gps::Graph* TemplateLibrary::plan(const QString& name) const {
    auto it = plans_.find(name);
    return it == plans_.end() ? nullptr : &it->second;
}

bool TemplateLibrary::save(const QString& name, gps::Graph preset, QString* error) {
    preset.name = toStd(name);
    VersionHistory::keep(fileFor(name), QByteArray::fromStdString(preset.toConf()));  // a template keeps its history too
    std::string failure;
    if (preset.save(toStd(fileFor(name)), &failure)) return true;
    *error = fromStd(failure);
    return false;
}

bool TemplateLibrary::remove(const QString& name) { return QFile::remove(fileFor(name)); }
