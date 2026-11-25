// projectfile.cpp – see projectfile.h. The .gpsproj is an INI file: [project] name, and one section per conf whose keys
// are the templates it uses and whose values are their revisions.
#include "projectfile.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QTextStream>
#include <algorithm>

#include "apppaths.h"
#include "contract.h"

namespace {
QString registryFile() { return QDir(AppPaths::state()).filePath("projects"); }
}

QString ProjectFile::pathForConf(const QString& conf) {
    const QDir folder = QFileInfo(conf).absoluteDir();
    return folder.filePath(folder.dirName() + ".gpsproj");
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

void ProjectFile::record(const QString& conf, const gps::Graph& content) {
    QMap<QString, int>& uses = confs_[QFileInfo(conf).fileName()];
    uses.clear();
    for (const gps::TemplateInstance& t : content.instances) {
        const QString name = fromStd(t.templateName);
        uses[name] = uses.contains(name) ? std::min(uses[name], t.revision) : t.revision;
    }
}

bool ProjectFile::save(QString* error) const {
    QFile::remove(path_);
    QSettings ini(path_, QSettings::IniFormat);
    ini.setValue("project/name", QFileInfo(path_).completeBaseName());
    ini.setValue("project/format", 1);
    for (auto conf = confs_.cbegin(); conf != confs_.cend(); ++conf) {
        ini.beginGroup("conf " + conf.key());
        ini.setValue("file", conf.key());
        // Template names may hold any character; INI keys may not: they are written in hex.
        for (auto t = conf.value().cbegin(); t != conf.value().cend(); ++t) ini.setValue(QString(t.key().toUtf8().toHex()), t.value());
        ini.endGroup();
    }
    ini.sync();
    if (ini.status() != QSettings::NoError) {
        if (error) *error = "cannot write " + path_;
        return false;
    }
    return true;
}

namespace ProjectRegistry {

void remember(const QString& projectPath) {
    QStringList all = projects();
    const QString path = QFileInfo(projectPath).absoluteFilePath();
    if (all.contains(path)) return;
    all << path;
    QFile file(registryFile());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;
    QTextStream out(&file);
    for (const QString& p : all) out << p << "\n";
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
