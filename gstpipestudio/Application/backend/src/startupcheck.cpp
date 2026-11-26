// startupcheck.cpp
#include "startupcheck.h"

#include <QRegularExpression>

#include "helperclient.h"

namespace {

constexpr int kAskMs = 20000;

bool older(int major, int minor) {
    return major < kMinGstVersion.major || (major == kMinGstVersion.major && minor < kMinGstVersion.minor);
}

}  // namespace

QString StartupResult::statusText() const {
    QString text = "GStreamer " + version;
    if (nvidia) text += QString(" · NVIDIA elements: %1").arg(nvidiaElements);
    return text;
}

StartupCheck::StartupCheck(QObject* parent) : QObject(parent) {}

void StartupCheck::start() {
    result_ = StartupResult();
    const int run = ++run_;
    auto* client = new HelperClient(this);
    connect(client, &HelperClient::finished, this, [this, client, run](const HelperResult& r) {
        client->deleteLater();
        if (run != run_) return;  // a later start asked again: only its answer counts
        gps::VersionInfo version;
        gps::ErrorReply error;
        bool usable = r.outcome == HelperResult::Finished && r.read(&version) && !version.gstreamer.version.empty();
        r.read(&error);
        onVersion(usable, version, QString::fromStdString(error.error));
    });
    client->start(findHelper("gps-inspect"), {"version"}, kAskMs);
}

void StartupCheck::onVersion(bool usable, const gps::VersionInfo& version, const QString& error) {
    if (!usable) {
        result_.detail = error;
        askSystemTool();
        return;
    }
    StartupResult r;
    r.version = QString::fromStdString(version.gstreamer.version);
    r.state = older(version.gstreamer.major, version.gstreamer.minor) ? StartupResult::TooOld : StartupResult::Ready;
    r.nvidia = version.nvidia.present;
    r.nvidiaElements = version.nvidia.elements;
    done(r);
}

void StartupCheck::askSystemTool() {
    StartupResult r;
    r.detail = result_.detail;
    QString tool = findSystemTool("gst-inspect-1.0");
    if (tool.isEmpty()) {
        r.state = StartupResult::NotInstalled;
        done(r);
        return;
    }
    auto* client = new HelperClient(this);
    connect(client, &HelperClient::finished, this, [this, client, r, run = run_](const HelperResult& answer) mutable {
        client->deleteLater();
        if (run != run_) return;
        if (answer.outcome != HelperResult::Finished || answer.exitCode != 0) {
            r.state = StartupResult::NotInstalled;  // present but unable to run: GStreamer is not usable either
        } else {
            QRegularExpressionMatch m = QRegularExpression("(\\d+)\\.(\\d+)\\.(\\d+)").match(QString::fromUtf8(answer.out));
            r.state = StartupResult::HelperBroken;
            if (m.hasMatch() && older(m.captured(1).toInt(), m.captured(2).toInt())) {
                r.state = StartupResult::TooOld;  // an old GStreamer is why our helper could not run
                r.version = m.captured(0);
            }
        }
        done(r);
    });
    client->start(tool, {"--version"}, kAskMs);
}

void StartupCheck::done(const StartupResult& result) {
    result_ = result;
    emit finished(result_);
}
