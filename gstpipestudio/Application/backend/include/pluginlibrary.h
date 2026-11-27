#pragma once

#include <QMap>
#include <QSet>

#include "componentprovider.h"
#include "pluginspec.h"
#include "plugintoolchain.h"

class FactoryCatalog;

enum class PluginBuildState { Untested, Building, Built, Outdated, Failed };

class PluginLibrary : public ComponentProvider {
    Q_OBJECT
public:
    PluginLibrary(FactoryCatalog& catalog, IPluginBuilder& builder, QObject* parent = nullptr);

    QString kind() const override { return "plugin"; }
    QString title() const override { return "Plugins"; }
    QString emptyHint() const override { return "Custom elements will appear here"; }
    QList<ComponentInfo> components() const override;
    gps::Graph peek(const QString& id) const override;
    QString placementError(const QString& id) const override;
    std::optional<gps::NodeRef> place(gps::Graph& graph, const QString& id, double x, double y) const override;
    QList<ComponentAction> actions(const QString& id) const override;
    bool canCreate() const override { return true; }
    bool canImport() const override { return true; }
    QString nameError(const QString& id, const QString& wanted) const override;
    bool rename(const QString& id, const QString& name, QString* newId, QString* error) override;
    bool remove(const QString& id) override;
    QString removalNote(const QString& id) const override;
    bool exportTo(const QString& id, const QString& destination, QString* error) const override;
    QString importFrom(const QString& path, QString* error) override;
    void importModule(const QString& path, QObject* context, std::function<void(const QString&, const QString&)> done);
    void reload() override;

    std::optional<PluginSpec> spec(const QString& id) const;
    QString newIdError(const QString& id) const;
    QString directory(const QString& id) const;
    QString sourcePath(const QString& id) const;
    QString moduleFileName(const QString& id) const;
    QString publishedModule(const QString& id) const;
    PluginBuildState state(const QString& id) const;
    QString statusText(const QString& id) const;
    PluginBuildResult lastBuild(const QString& id) const;
    bool createPlugin(const PluginSpec& spec, QString* error);
    bool updateSpec(const QString& id, const PluginSpec& spec, QString* error);
    bool saveSource(const QString& id, const QString& text, QString* error);
    void buildPlugin(const QString& id, QObject* context, IPluginBuilder::Done done);

signals:
    void buildOutputLine(const QString& id, const QString& line);
    void notice(const QString& message);

private:
    struct Entry {
        PluginSpec spec;
        QString module;
        bool binaryOnly = false;
        QByteArray fingerprint;
        PluginBuildResult last;
    };

    const Entry* find(const QString& id) const;
    QString modulePath(const Entry& entry) const;
    QStringList moduleMembers(const Entry& entry) const;
    QByteArray fingerprint(const QString& id) const;
    bool writeManifest(const QString& at, const Entry& entry, QString* error) const;
    void writeBuildRecord(const QString& id);
    bool installEntry(const Entry& entry, const QString& sourceDirectory, QString* error);
    void buildFinished(const QString& id, const QString& root, const QByteArray& revision, PluginBuildResult result,
                       const IPluginBuilder::Done& done);
    FactoryCatalog& catalog_;
    IPluginBuilder& builder_;
    QString root_;
    QMap<QString, Entry> entries_;
    QSet<QString> building_;
};
