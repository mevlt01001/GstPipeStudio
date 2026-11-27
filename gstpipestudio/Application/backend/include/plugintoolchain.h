#pragma once

#include <QList>
#include <QString>
#include <functional>
#include <vector>

#include "catalog.h"

class QObject;

struct BuildDiagnostic {
    QString file, severity, message;
    int line = 0;
    int column = 0;
};

class DiagnosticParser {
public:
    static QList<BuildDiagnostic> parse(const QString& output);
};

struct PluginBuildResult {
    bool ok = false;
    QString error, output, module;
    QList<BuildDiagnostic> diagnostics;
};

struct PluginBuildRequest {
    QString id, directory;
};

class IPluginBuilder {
public:
    using Output = std::function<void(const QString&)>;
    using Done = std::function<void(PluginBuildResult)>;
    virtual ~IPluginBuilder() = default;
    virtual void build(const PluginBuildRequest& request, QObject* context, Output output, Done done) = 0;
};

class CMakePluginBuilder final : public IPluginBuilder {
public:
    struct Options {
        QString sdkOverride, sdkBuildDirectory;
    };

    explicit CMakePluginBuilder(Options options);
    void build(const PluginBuildRequest& request, QObject* context, Output output, Done done) override;

private:
    QString sdkDirectory() const;
    Options options_;
};

namespace PluginModules {

using ProbeDone = std::function<void(std::vector<gps::FactoryEntry>, const QString&)>;
void inspect(const QString& module, QObject* context, ProbeDone done);
bool publish(const QString& source, const QString& destination, QString* error);

}  // namespace PluginModules
