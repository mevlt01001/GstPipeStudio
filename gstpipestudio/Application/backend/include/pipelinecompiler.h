#pragma once

#include <QString>
#include <functional>

#include "graph.h"

class PluginLibrary;
class QObject;

struct PipelineExportOptions {
    QString moduleName;
    QString outputDir;
    QString version = "0.1.0";
    QString description;
    bool copyRuntimeHeaders = true;
};

struct PipelineExportResult {
    bool ok = false;
    QString error;
    QString output;
    QString soPath;
    QString integrationDocPath;
    QString exampleHostPath;
};

class PipelineCompiler {
public:
    using OutputFn = std::function<void(const QString&)>;
    using DoneFn = std::function<void(PipelineExportResult)>;

    static QString validateModuleName(const QString& name);
    static QString sanitizeModuleName(const QString& name);

    static PipelineExportResult compile(const gps::Graph& graph, const PipelineExportOptions& opts,
                                        const PluginLibrary* plugins = nullptr);

    static void compileAsync(const gps::Graph& graph, const PipelineExportOptions& opts, QObject* context,
                             OutputFn output, DoneFn done, const PluginLibrary* plugins = nullptr);
};
