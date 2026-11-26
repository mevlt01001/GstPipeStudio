// appservices.h – what every document of the process shares, built once in main(): the GStreamer check, the factory
// catalog (one gps-inspect for all tabs), the template library, the view preferences and the hot-exit backups. Every
// Controller and the window get it; a new tab costs no probe and no helper process. What is remembered between starts
// is Settings.
#pragma once

#include <QObject>

#include "backupservice.h"
#include "environmentservice.h"
#include "factorycatalog.h"
#include "templatelibrary.h"
#include "viewpreferences.h"

class AppServices : public QObject {
    Q_OBJECT
public:
    explicit AppServices(QObject* parent = nullptr) : QObject(parent) {}

    EnvironmentService& environment() { return environment_; }
    FactoryCatalog& catalog() { return catalog_; }
    TemplateLibrary& library() { return library_; }
    ViewPreferences& view() { return view_; }
    BackupService& backups() { return backups_; }

private:
    FactoryCatalog catalog_;
    EnvironmentService environment_{catalog_};
    TemplateLibrary library_{catalog_};
    ViewPreferences view_;
    BackupService backups_;
};
