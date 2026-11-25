// environmentservice.h – the GStreamer check of the process (StartupCheck): run when the window opens and again on
// Retry, never per tab. When GStreamer is ready it starts loading the shared factory catalog.
#pragma once

#include <QObject>

#include "startupcheck.h"

class FactoryCatalog;

class EnvironmentService : public QObject {
    Q_OBJECT
public:
    explicit EnvironmentService(FactoryCatalog& catalog, QObject* parent = nullptr);

    void check();  // asynchronous; checked() follows
    const StartupResult& result() const { return check_.result(); }
    bool blocked() const { return result().bad(); }  // GStreamer is not usable: the setup page shows instead of the canvas

signals:
    void checked(const StartupResult& result);

private:
    FactoryCatalog& catalog_;
    StartupCheck check_;
};
