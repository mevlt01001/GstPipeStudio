// projectfile.cpp – see projectfile.h. The .gpsproj is an INI file: [project] name, and one section per conf whose keys
// are the templates it uses and whose values are their revisions.
#include "projectfile.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QTemporaryDir>
#include <algorithm>

#include "apppaths.h"
#include "contract.h"
#include "files.h"

namespace {
QString registryFile() { return QDir(AppPaths::state()).filePath("projects"); }
}

QString ProjectFile::pathForConf(const QString& conf) {
    const QDir folder = QFileInfo(conf).absoluteDir();
    return folder.filePath(folder.dirName() + ".gpsproj");
}

QString ProjectFile::mainConf(const QString& folder) {
    const QDir dir(folder);
    return dir.filePath(dir.dirName() + ".conf");
}

QString ProjectFile::templatesFolder(const QString& conf) { return fromStd(gps::Graph::templatesFolder(toStd(conf))); }

void ProjectFile::opened(const QString& conf) {
    if (const QString manifest = pathForConf(conf); QFile::exists(manifest)) ProjectRegistry::remember(manifest);
}

ProjectFile ProjectFile::load(const QString& path) {
    ProjectFile p;
    p.path_ = path;
    if (!QFile::exists(path)) return p;
    QSettings ini(path, QSettings::IniFormat);
    for (const QString& group : ini.childGroups()) {
        if (!group.startsWith("conf ")) continue;
        ini.beginGroup(group);
        QMap<QString, int>& uses = p.confs_[group.mid(5)];
        for (const QString& key : ini.childKeys()) {
            if (key != "file") uses[QString::fromUtf8(QByteArray::fromHex(key.toLatin1()))] = ini.value(key).toInt();
        }
        ini.endGroup();
    }
    return p;
}

QMap<QString, int> ProjectFile::templates() const {
    QMap<QString, int> all;
    for (const auto& uses : confs_) {
        for (auto it = uses.cbegin(); it != uses.cend(); ++it) all[it.key()] = all.contains(it.key()) ? std::min(all[it.key()], it.value()) : it.value();
    }
    return all;
}

QMap<QString, int> ProjectFile::templatesOf(const QString& conf) const { return confs_.value(QFileInfo(conf).fileName()); }

bool ProjectFile::rename(const QString& conf, const QString& to) {
    const QString from = QFileInfo(conf).fileName();
    if (!confs_.contains(from)) return false;
    confs_[QFileInfo(to).fileName()] = confs_.take(from);
    return true;
}

bool ProjectFile::forget(const QString& conf) { return confs_.remove(QFileInfo(conf).fileName()) > 0; }

void ProjectFile::record(const QString& conf, const gps::Graph& content) {
    QMap<QString, int>& uses = confs_[QFileInfo(conf).fileName()];
    uses.clear();
    for (const gps::TemplateInstance& t : content.instances) {
        const QString name = fromStd(t.templateName);
        uses[name] = uses.contains(name) ? std::min(uses[name], t.revision) : t.revision;
    }
}

// QSettings owns the INI format, the reader's and the writer's: it drafts the manifest in a temporary folder, and the
// draft is written through the one writer.
bool ProjectFile::save(QString* error) const {
    QTemporaryDir drafts;
    const QString draft = drafts.filePath("manifest");
    {
        QSettings ini(draft, QSettings::IniFormat);
        ini.setValue("project/name", QFileInfo(path_).completeBaseName());
        ini.setValue("project/format", 1);
        for (auto conf = confs_.cbegin(); conf != confs_.cend(); ++conf) {
            ini.beginGroup("conf " + conf.key());
            ini.setValue("file", conf.key());
            // Template names may hold any character; INI keys may not: they are written in hex.
            for (auto t = conf.value().cbegin(); t != conf.value().cend(); ++t) ini.setValue(QString(t.key().toUtf8().toHex()), t.value());
            ini.endGroup();
        }
    }
    QFile text(draft);
    std::string failure = "cannot write " + toStd(path_);
    if (drafts.isValid() && text.open(QIODevice::ReadOnly) && gps::files::writeAtomic(toStd(path_), text.readAll().toStdString(), &failure))
        return true;
    if (error) *error = fromStd(failure);
    return false;
}

namespace ProjectRegistry {

bool remember(const QString& projectPath, QString* error) {
    QStringList all = projects();
    const QString path = QFileInfo(projectPath).absoluteFilePath();
    if (all.contains(path)) return true;
    all << path;
    std::string failure;
    if (gps::files::writeAtomic(toStd(registryFile()), toStd(all.join("\n") + "\n"), &failure)) return true;
    if (error) *error = fromStd(failure);
    return false;
}

QStringList projects() {
    QStringList out;
    QFile file(registryFile());
    if (!file.open(QIODevice::ReadOnly)) return out;
    for (const QString& line : QString::fromUtf8(file.readAll()).split('\n', Qt::SkipEmptyParts)) {
        if (QFile::exists(line) && !out.contains(line)) out << line;  // a moved or deleted project drops out
    }
    return out;
}

QStringList usingTemplate(const QString& templateName) {
    QStringList out;
    for (const QString& project : projects()) {
        if (ProjectFile::load(project).templates().contains(templateName)) out << project;
    }
    return out;
}

}  // namespace ProjectRegistry
