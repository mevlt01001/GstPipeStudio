// appservices.h – what every document of the process shares, owned by GstPipeStudio: the GStreamer check, the factory
// catalog (one gps-inspect for all tabs), the component providers (the template library, the plugins), the view
// preferences and the hot-exit backups. Every
// Controller and the window get it; a new tab costs no probe and no helper process. What is remembered between starts
// is Settings.
#pragma once

#include <QObject>
#include <memory>

#include "backupservice.h"
#include "documenttiming.h"
#include "environmentservice.h"
#include "factorycatalog.h"
#include "pluginlibrary.h"
#include "templatelibrary.h"
#include "viewpreferences.h"

class AppServices : public QObject {
    Q_OBJECT
public:
    explicit AppServices(QObject* parent = nullptr) : AppServices(DocumentTiming::fromEnvironment(), parent) {}
    explicit AppServices(DocumentTiming timing, QObject* parent = nullptr);
    AppServices(DocumentTiming timing, std::unique_ptr<IPluginBuilder> builder, QObject* parent = nullptr);

    const DocumentTiming& timing() const { return timing_; }

    EnvironmentService& environment() { return environment_; }
    FactoryCatalog& catalog() { return catalog_; }
    TemplateLibrary& library() { return library_; }
    PluginLibrary& plugins() { return plugins_; }
    // Everything that can be placed besides a single element, one section each in the picker and the Explorer.
    QList<ComponentProvider*> providers() { return {&library_, &plugins_}; }
    ComponentProvider* provider(const QString& kind) { return providerOf(providers(), kind); }
    ViewPreferences& view() { return view_; }
    BackupService& backups() { return backups_; }

private:
    DocumentTiming timing_;
    FactoryCatalog catalog_;
    EnvironmentService environment_{catalog_};
    TemplateLibrary library_{catalog_};
    std::unique_ptr<IPluginBuilder> pluginBuilder_;
    PluginLibrary plugins_;
    ViewPreferences view_;
    BackupService backups_;
};
