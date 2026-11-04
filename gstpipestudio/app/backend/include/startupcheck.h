// startupcheck.h – the first-run check (UI_RULES 3.5). No widgets. It asks `gps-inspect version`; when that does not
// give a usable answer it asks `gst-inspect-1.0 --version` to tell "not installed" from "the helper is broken".
// The app never installs anything.
#pragma once

#include <QObject>
#include <QString>

// The oldest GStreamer the app supports: the one place to change it.
struct GstVersion {
    int major, minor;
};
constexpr GstVersion kMinGstVersion{1, 16};

struct StartupResult {
    enum State { Checking, Ready, NotInstalled, TooOld, HelperBroken };

    State state = Checking;
    QString version;           // "1.20.3" for Ready and TooOld, when known
    bool nvidia = false;       // Ready: the NVIDIA elements are present
    int nvidiaElements = 0;
    QString detail;            // HelperBroken: what the helper said

    bool bad() const { return state == NotInstalled || state == TooOld || state == HelperBroken; }
    QString statusText() const;  // "GStreamer 1.20.3 · NVIDIA elements: 57"
};

class StartupCheck : public QObject {
    Q_OBJECT
public:
    explicit StartupCheck(QObject* parent = nullptr);

    void start();  // asynchronous; finished() comes once per start
    const StartupResult& result() const { return result_; }

signals:
    void finished(const StartupResult& result);

private:
    void onVersion(bool usable, const QJsonObject& json, const QString& error);
    void askSystemTool();
    void done(const StartupResult& result);

    StartupResult result_;
};
