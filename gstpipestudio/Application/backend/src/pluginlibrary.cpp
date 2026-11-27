#include "pluginlibrary.h"

#include "contract.h"
#include "factorycatalog.h"
#include "pluginpaths.h"
#include "pluginscaffolder.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QPointer>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTemporaryDir>

namespace {

const QString manifestName = "plugin.json";
const QString buildRecordName = ".build-status.json";

bool fail(QString* error, const QString& why) {
    if (error) *error = why;
    return false;
}

QByteArray read(const QString& path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

bool write(const QString& path, const QByteArray& contents, QString* error) {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(contents) != contents.size() || !file.commit())
        return fail(error, path + ": " + file.errorString());
    return true;
}

bool copyTree(const QDir& from, const QDir& to, QString* error) {
    for (const auto& child : from.entryInfoList(QDir::NoDotAndDotDot | QDir::AllEntries)) {
        const auto name = child.fileName();
        if (name == "build") continue;
        if (child.isSymLink()) return fail(error, "Plugin exports and imports require regular files: " + child.filePath());
        const auto target = to.filePath(name);
        if (child.isDir()) {
            if (!QDir().mkpath(target)) return fail(error, "Unable to create " + target);
            if (!copyTree(QDir(child.absoluteFilePath()), QDir(target), error)) return false;
        } else if (!child.isFile() || !QFile::copy(child.absoluteFilePath(), target)) {
            return fail(error, "Unable to copy " + child.filePath());
        }
    }
    return true;
}

bool moduleNameValid(const QString& name) {
    static const QRegularExpression expected("\\Alibgst[a-zA-Z0-9_]+\\.so\\z");
    return expected.match(name).hasMatch();
}

QString buildKey(const QString& root, const QString& id) { return QDir(root).filePath(id); }

}  // namespace

PluginLibrary::PluginLibrary(FactoryCatalog& catalog, IPluginBuilder& builder, QObject* parent)
    : ComponentProvider(parent), catalog_(catalog), builder_(builder) {
    connect(&catalog_, &FactoryCatalog::ready, this, &PluginLibrary::changed);
    reload();
}

const PluginLibrary::Entry* PluginLibrary::find(const QString& id) const {
    const auto entry = entries_.constFind(id);
    return entry == entries_.constEnd() ? nullptr : &entry.value();
}

std::optional<PluginSpec> PluginLibrary::spec(const QString& id) const {
    if (const auto* entry = find(id)) return entry->spec;
    return std::nullopt;
}

QString PluginLibrary::directory(const QString& id) const { return QDir(root_).filePath(id); }

QString PluginLibrary::sourcePath(const QString& id) const {
    const auto* entry = find(id);
    if (!entry || entry->binaryOnly) return {};
    return QDir(directory(id)).filePath(id + ".cpp");
}

QString PluginLibrary::modulePath(const Entry& entry) const { return QDir(root_).filePath(".bin/" + entry.module); }

QByteArray PluginLibrary::fingerprint(const QString& id) const {
    const QDir folder(directory(id));
    QDirIterator iter(folder.path(), QDir::Files | QDir::NoSymLinks, QDirIterator::Subdirectories);
    QStringList files;
    while (iter.hasNext()) {
        const QString relative = folder.relativeFilePath(iter.next());
        if (!relative.startsWith("build/")) files << relative;
    }
    files.sort();
    QCryptographicHash hash(QCryptographicHash::Sha256);
    for (const auto& relative : files) {
        QFile file(folder.filePath(relative));
        if (!file.open(QIODevice::ReadOnly)) return {};
        hash.addData(relative.toUtf8());
        hash.addData(QByteArray::number(file.size()) + '\0');
        if (!hash.addData(&file)) return {};
    }
    return hash.result().toHex();
}

PluginBuildState PluginLibrary::state(const QString& id) const {
    if (building_.contains(buildKey(root_, id))) return PluginBuildState::Building;
    const auto* entry = find(id);
    if (!entry) return PluginBuildState::Untested;
    if (!entry->last.error.isEmpty()) return PluginBuildState::Failed;
    if (!QFileInfo(modulePath(*entry)).isFile()) return PluginBuildState::Untested;
    if (entry->binaryOnly || (!entry->fingerprint.isEmpty() && entry->fingerprint == fingerprint(id))) return PluginBuildState::Built;
    return PluginBuildState::Outdated;
}

QString PluginLibrary::statusText(const QString& id) const {
    static const QStringList statuses{"Needs build", "Building…", "Ready", "Needs rebuild", "Build failed"};
    return statuses.at(static_cast<int>(state(id)));
}

PluginBuildResult PluginLibrary::lastBuild(const QString& id) const {
    if (const auto* entry = find(id)) return entry->last;
    return {};
}

QList<ComponentInfo> PluginLibrary::components() const {
    QList<ComponentInfo> result;
    for (const auto& entry : entries_) {
        const QString id = entry.spec.id;
        const bool input = entry.spec.hasPort(PluginDirection::Sink);
        const bool output = entry.spec.hasPort(PluginDirection::Src);
        ComponentInfo info;
        info.ref = {kind(), id};
        info.title = entry.spec.longName;
        info.description = entry.spec.description;
        const auto stateNow = state(id);
        info.hint = "plugin · " + statusText(id).toLower();
        info.role = !input ? gps::Role::Source : output ? gps::Role::Filter : gps::Role::Sink;
        info.takesInput = input;
        info.givesOutput = output;
        if (stateNow == PluginBuildState::Built) info.status = ComponentStatus::Ready;
        if (stateNow == PluginBuildState::Outdated || stateNow == PluginBuildState::Building) info.status = ComponentStatus::Pending;
        if (stateNow == PluginBuildState::Failed) info.status = ComponentStatus::Failed;
        result << info;
    }
    return result;
}

QString PluginLibrary::placementError(const QString& id) const {
    const auto* entry = find(id);
    if (!entry) return "The plugin is no longer in the library.";
    if (!QFileInfo(modulePath(*entry)).isFile()) return "Build " + id + " before adding it to a pipeline.";
    if (!catalog_.entry(id)) return "The factory " + id + " is unavailable. Refresh the library or rebuild the plugin.";
    return {};
}

gps::Graph PluginLibrary::peek(const QString& id) const {
    gps::Graph graph;
    place(graph, id, 0, 0);
    return graph;
}

std::optional<gps::NodeRef> PluginLibrary::place(gps::Graph& graph, const QString& id, double x, double y) const {
    if (!placementError(id).isEmpty()) return std::nullopt;
    return gps::NodeRef::element(graph.addElement(toStd(id), {}, x, y));
}

QString PluginLibrary::newIdError(const QString& id) const {
    if (const auto why = PluginSpec::idError(id); !why.isEmpty()) return why;
    if (!catalog_.loaded()) return "Wait for the GStreamer factory catalog to load.";
    if (find(id) || QFileInfo::exists(directory(id)) || catalog_.entry(id))
        return "The factory ID " + id + " is already in use.";
    return {};
}

void PluginLibrary::reload() {
    const auto oldRoot = root_;
    const auto previous = entries_;
    root_ = PluginPaths::root();
    entries_.clear();
    const QDir folder(root_);
    for (const auto& name : folder.entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks, QDir::Name)) {
        const QDir plugin(folder.filePath(name));
        const auto manifest = QJsonDocument::fromJson(read(plugin.filePath(manifestName))).object();
        Entry entry;
        if (!PluginSpec::fromJson(manifest, &entry.spec, nullptr) || entry.spec.id != name) continue;
        entry.module = manifest.value("module").toString("libgst" + name + ".so");
        entry.binaryOnly = manifest.value("binaryOnly").toBool();
        if (!moduleNameValid(entry.module)) continue;
        const auto status = QJsonDocument::fromJson(read(plugin.filePath(buildRecordName))).object();
        entry.fingerprint = status.value("fingerprint").toString().toLatin1();
        entry.last.error = status.value("error").toString();
        entry.last.ok = entry.last.error.isEmpty() && !entry.fingerprint.isEmpty();
        entry.last.output = status.value("output").toString();
        entry.last.diagnostics = DiagnosticParser::parse(entry.last.output);
        entries_.insert(name, std::move(entry));
    }
    for (auto iter = previous.constBegin(); iter != previous.constEnd(); ++iter) {
        if (oldRoot != root_ || !find(iter.key()) || !QFileInfo::exists(modulePath(iter.value()))) catalog_.forgetFactory(iter.key());
    }
    emit changed();
    if (!entries_.isEmpty() || !previous.isEmpty()) catalog_.reloadList();
}

bool PluginLibrary::writeManifest(const QString& at, const Entry& entry, QString* error) const {
    auto manifest = entry.spec.toJson();
    if (entry.binaryOnly) {
        manifest["binaryOnly"] = true;
        manifest["module"] = entry.module;
    }
    return write(QDir(at).filePath(manifestName), QJsonDocument(manifest).toJson(), error);
}

void PluginLibrary::writeBuildRecord(const QString& id) {
    const auto& entry = entries_.constFind(id).value();
    const QJsonObject record{{"fingerprint", QString::fromLatin1(entry.fingerprint)}, {"error", entry.last.error},
                             {"output", entry.last.output.right(256 * 1024)}};
    QString error;
    if (!write(QDir(directory(id)).filePath(buildRecordName), QJsonDocument(record).toJson(), &error)) emit notice(error);
}

bool PluginLibrary::installEntry(const Entry& entry, const QString& sourceDirectory, QString* error) {
    if (const auto why = entry.spec.error(); !why.isEmpty()) return fail(error, why);
    const auto& id = entry.spec.id;
    if (const auto why = newIdError(id); !why.isEmpty()) return fail(error, why);
    if (!QDir().mkpath(root_)) return fail(error, "Unable to create " + root_);
    QTemporaryDir staging(QDir(root_).filePath(".new-XXXXXX"));
    if (!staging.isValid()) return fail(error, staging.errorString());
    const QDir folder(staging.path());
    if (!sourceDirectory.isEmpty()) {
        if (!copyTree(QDir(sourceDirectory), folder, error)) return false;
    } else if (!entry.binaryOnly) {
        if (!write(folder.filePath(id + ".cpp"), PluginScaffolder::source(entry.spec).toUtf8(), error) ||
            !write(folder.filePath("CMakeLists.txt"), PluginScaffolder::cmake(entry.spec).toUtf8(), error)) return false;
    }
    if (!writeManifest(folder.path(), entry, error)) return false;
    if (!QDir().rename(folder.path(), directory(id))) return fail(error, "Unable to install the plugin folder " + directory(id));
    entries_.insert(id, entry);
    emit changed();
    return true;
}

bool PluginLibrary::createPlugin(const PluginSpec& spec, QString* error) {
    Entry entry;
    entry.spec = spec;
    entry.module = "libgst" + spec.id + ".so";
    return installEntry(entry, {}, error);
}

bool PluginLibrary::saveSource(const QString& id, const QString& text, QString* error) {
    const auto source = sourcePath(id);
    if (source.isEmpty()) return fail(error, "This plugin has no editable source.");
    if (!write(source, text.toUtf8(), error)) return false;
    emit changed();
    return true;
}

bool PluginLibrary::updateSpec(const QString& id, const PluginSpec& spec, QString* error) {
    auto* entry = find(id);
    if (!entry || entry->binaryOnly) return fail(error, "This plugin has no editable specification.");
    if (building_.contains(buildKey(root_, id))) return fail(error, "Wait for the current build to finish.");
    if (const auto why = spec.error(); !why.isEmpty()) return fail(error, why);
    if (id != spec.id || entry->spec.resolvedArchetype() != spec.resolvedArchetype())
        return fail(error, "The factory ID and resolved C++ base class are fixed after creation.");
    const auto source = sourcePath(id);
    const auto cmake = QDir(directory(id)).filePath("CMakeLists.txt");
    const auto oldSource = read(source);
    const auto oldCMake = read(cmake);
    auto nextSource = QString::fromUtf8(oldSource);
    auto nextCMake = QString::fromUtf8(oldCMake);
    if (!PluginScaffolder::updateSpecRegions(spec, &nextSource, error) ||
        !PluginScaffolder::updateCMakeRegion(spec, &nextCMake, error)) return false;
    Entry revised = *entry;
    revised.spec = spec;
    if (!write(source, nextSource.toUtf8(), error)) return false;
    if (!write(cmake, nextCMake.toUtf8(), error) || !writeManifest(directory(id), revised, error)) {
        QString rollbackError;
        if (!write(source, oldSource, &rollbackError)) emit notice(rollbackError);
        if (!write(cmake, oldCMake, &rollbackError)) emit notice(rollbackError);
        return false;
    }
    entries_.insert(id, std::move(revised));
    emit changed();
    return true;
}

void PluginLibrary::buildPlugin(const QString& id, QObject* context, IPluginBuilder::Done done) {
    auto complete = [guard = QPointer<QObject>(context), done = std::move(done)](PluginBuildResult result) {
        if (guard && done) done(std::move(result));
    };
    const auto root = root_;
    const auto key = buildKey(root, id);
    if (sourcePath(id).isEmpty() || building_.contains(key)) {
        PluginBuildResult result;
        result.error = sourcePath(id).isEmpty() ? "This plugin has no buildable source." : "This plugin is already building.";
        complete(std::move(result));
        return;
    }
    const auto revision = fingerprint(id);
    building_.insert(key);
    emit changed();
    builder_.build({id, directory(id)}, this, [this, id](const QString& line) { emit buildOutputLine(id, line); },
        [this, id, root, revision, complete](PluginBuildResult result) {
            buildFinished(id, root, revision, std::move(result), complete);
        });
}

void PluginLibrary::buildFinished(const QString& id, const QString& root, const QByteArray& revision, PluginBuildResult result,
                                  const IPluginBuilder::Done& done) {
    auto finish = [this, id, root, revision, done](PluginBuildResult result) mutable {
        building_.remove(buildKey(root, id));
        if (root == root_ && find(id)) {
            auto& entry = entries_[id];
            if (result.ok) entry.fingerprint = revision;
            entry.last = result;
            writeBuildRecord(id);
            emit changed();
        }
        done(std::move(result));
    };
    if (root != root_ || !find(id) || revision.isEmpty() || fingerprint(id) != revision) {
        result.ok = false;
        result.error = "Plugin files changed during the build. Build again to install the latest source.";
    }
    if (result.ok) result.ok = PluginModules::publish(result.module, modulePath(*find(id)), &result.error);
    if (!result.ok) return finish(std::move(result));
    catalog_.reloadFactory(id, [finish, result = std::move(result)](bool ok) mutable {
        result.ok = ok;
        if (!ok) result.error = "The factory was verified but could not be refreshed in the active GStreamer registry.";
        finish(std::move(result));
    });
}

QList<ComponentAction> PluginLibrary::actions(const QString& id) const {
    const auto* entry = find(id);
    if (!entry) return {};
    QList<ComponentAction> result{ComponentAction::Rename, ComponentAction::Export, ComponentAction::Delete};
    if (!entry->binaryOnly) result.prepend(ComponentAction::Edit);
    return result;
}

QString PluginLibrary::nameError(const QString& id, const QString& wanted) const {
    if (!find(id)) return "The plugin is no longer in the library.";
    if (wanted.trimmed().isEmpty()) return "Enter a display name.";
    return {};
}

bool PluginLibrary::rename(const QString& id, const QString& name, QString* newId, QString* error) {
    if (const auto why = nameError(id, name); !why.isEmpty()) return fail(error, why);
    auto updated = *find(id);
    updated.spec.longName = name.trimmed();
    if (updated.binaryOnly) {
        if (!writeManifest(directory(id), updated, error)) return false;
        entries_.insert(id, std::move(updated));
        emit changed();
    } else if (!updateSpec(id, updated.spec, error)) {
        return false;
    }
    if (newId) *newId = id;
    return true;
}

bool PluginLibrary::remove(const QString& id) {
    const auto* entry = find(id);
    if (!entry || building_.contains(buildKey(root_, id))) return false;
    const auto binary = modulePath(*entry);
    const auto parked = QDir(directory(id)).filePath(entry->module);
    const bool hasBinary = QFileInfo::exists(binary);
    if (hasBinary && !QFile::rename(binary, parked)) return false;
    if (!QFile::moveToTrash(directory(id))) {
        if (hasBinary && !QFile::rename(parked, binary)) emit notice("Unable to restore " + binary + " from " + parked);
        return false;
    }
    entries_.remove(id);
    catalog_.forgetFactory(id);
    catalog_.reloadList();
    emit changed();
    return true;
}

QString PluginLibrary::removalNote(const QString&) const {
    return "Pipelines using this factory will need the plugin reinstalled before the next validation or run.";
}

bool PluginLibrary::exportTo(const QString& id, const QString& destination, QString* error) const {
    const auto* entry = find(id);
    if (!entry) return fail(error, "The plugin is no longer in the library.");
    if (QFileInfo::exists(destination)) return fail(error, "The export destination already exists.");
    if (destination.endsWith(".so") || entry->binaryOnly) {
        if (QFileInfo(destination).fileName() != entry->module)
            return fail(error, "Use the original GStreamer module name " + entry->module + ".");
        return QFile::copy(modulePath(*entry), destination) || fail(error, "Build the plugin before exporting its module.");
    }
    QTemporaryDir staging(QFileInfo(destination).dir().filePath(".export-XXXXXX"));
    if (!staging.isValid()) return fail(error, staging.errorString());
    if (!copyTree(QDir(directory(id)), QDir(staging.path()), error)) return false;
    return QDir().rename(staging.path(), destination) || fail(error, "Unable to install export at " + destination);
}

QString PluginLibrary::importFrom(const QString& path, QString* error) {
    const QFileInfo from(path);
    const QDir folder(from.isDir() ? from.absoluteFilePath() : from.absolutePath());
    const auto manifest = QJsonDocument::fromJson(read(folder.filePath(manifestName))).object();
    Entry entry;
    if (!PluginSpec::fromJson(manifest, &entry.spec, error)) return {};
    entry.module = "libgst" + entry.spec.id + ".so";
    if (manifest.value("binaryOnly").toBool() || !QFileInfo::exists(folder.filePath(entry.spec.id + ".cpp")) ||
        !QFileInfo::exists(folder.filePath("CMakeLists.txt"))) {
        fail(error, "Choose a plugin source folder with its manifest, CMakeLists.txt and C++ file.");
        return {};
    }
    return installEntry(entry, folder.path(), error) ? entry.spec.id : QString();
}

void PluginLibrary::importModule(const QString& path, QObject* context, std::function<void(const QString&, const QString&)> done) {
    const auto module = QFileInfo(path).fileName();
    if (!moduleNameValid(module)) {
        done({}, "Choose a GStreamer module named libgst<name>.so.");
        return;
    }
    PluginModules::inspect(path, this, [this, root = root_, path, module, guard = QPointer<QObject>(context), done = std::move(done)]
                           (const std::vector<gps::FactoryEntry>& entries, const QString& probeError) {
        if (!guard) return;
        if (root_ != root) return done({}, "The plugin library changed during import.");
        if (!probeError.isEmpty()) return done({}, probeError);
        if (entries.size() != 1) return done({}, "Import a module that exposes one element factory.");
        const auto& factory = entries.front();
        Entry entry;
        entry.spec.id = fromStd(factory.factory);
        entry.spec.longName = fromStd(factory.longName);
        entry.spec.klass = fromStd(factory.klass);
        entry.spec.archetype = PluginArchetype::GeneralElement;
        entry.spec.ports.clear();
        if (!factory.isSource) entry.spec.ports << PluginPortSpec{"sink", PluginDirection::Sink, PluginPresence::Always, PluginCaps::Any, {}};
        if (!factory.isSink) entry.spec.ports << PluginPortSpec{"src", PluginDirection::Src, PluginPresence::Always, PluginCaps::Any, {}};
        entry.module = module;
        entry.binaryOnly = true;
        const auto id = entry.spec.id;
        if (QFileInfo::exists(modulePath(entry))) return done({}, "A module named " + module + " already exists.");
        QString error;
        if (!installEntry(entry, {}, &error)) return done({}, error);
        if (!PluginModules::publish(path, modulePath(entry), &error)) {
            QDir(directory(id)).removeRecursively();  // the directory was just created by this import
            entries_.remove(id);
            emit changed();
            return done({}, error);
        }
        catalog_.reloadFactory(id, [guard, this, id, done](bool ok) {
            emit changed();
            if (guard) done(ok ? id : QString(), ok ? QString() : "The imported factory could not be refreshed.");
        });
    });
}
