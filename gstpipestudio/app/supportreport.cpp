// supportreport.cpp
#include "supportreport.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QSysInfo>

namespace {

constexpr const char* kIssueUrl = "https://github.com/mevlt01001/GstPipeStudio/issues/new";
constexpr const char* kAppVersion = "0.1.0";
constexpr int kMaxLineLength = 300;

QString driverVersion() {
    QFile file("/proc/driver/nvidia/version");
    if (!file.open(QIODevice::ReadOnly)) return QString();
    QRegularExpressionMatch m = QRegularExpression("(\\d+\\.\\d+(?:\\.\\d+)?)").match(QString::fromUtf8(file.readLine()));
    return m.hasMatch() ? m.captured(1) : QString();
}

QString clip(const QString& text, int max) { return text.size() <= max ? text : text.left(max - 1) + "…"; }

QString bodyWith(const SupportInfo& info, const QStringList& log) {
    auto hide = [&](const QString& t) { return SupportReport::hidePrivate(t, info); };
    QStringList out;
    out << "**What happened**" << hide(clip(info.action.isEmpty() ? "(not recorded)" : info.action, kMaxLineLength)) << "";
    out << "**Details**";
    out << "- GstPipeStudio: " + info.appVersion;
    out << "- OS: " + info.os + " (" + info.arch + ")";
    out << "- Qt: " + info.qt;
    QString gst = info.gstreamer.isEmpty() ? QString("not checked") : info.gstreamer;
    if (info.nvidiaElements >= 0) gst += QString(" · NVIDIA elements: %1").arg(info.nvidiaElements);
    out << "- GStreamer: " + gst;
    if (!info.driver.isEmpty()) out << "- NVIDIA driver: " + info.driver;
    if (!info.linking.isEmpty()) out << "- Linking: " + hide(clip(info.linking, kMaxLineLength));
    if (!info.validation.isEmpty()) out << "- Last validation: " + hide(clip(info.validation, kMaxLineLength));
    if (!log.isEmpty()) {
        out << "" << "**Log (last lines)**" << "```";
        for (const QString& line : log) out << hide(clip(line, kMaxLineLength));
        out << "```";
    }
    return out.join("\n");
}

QUrl build(const QString& title, const QString& body) {
    return QUrl::fromEncoded(QByteArray(kIssueUrl) + "?title=" + QUrl::toPercentEncoding(title) + "&body=" + QUrl::toPercentEncoding(body));
}

}  // namespace

bool DesktopUrlOpener::open(const QUrl& url) { return QDesktopServices::openUrl(url); }

SupportInfo SupportReport::systemInfo() {
    SupportInfo i;
    i.appVersion = kAppVersion;
    i.os = QSysInfo::prettyProductName();
    i.arch = QSysInfo::currentCpuArchitecture();
    i.qt = qVersion();
    i.driver = driverVersion();
    i.home = QDir::homePath();
    i.user = qEnvironmentVariable("USER", qEnvironmentVariable("LOGNAME"));
    i.host = QSysInfo::machineHostName();
    return i;
}

// The home directory becomes ~, and so does any other /home/<name> or /Users/<name> (a container or sudo has another HOME);
// a user or host name that shows up anywhere else is replaced as a whole word.
QString SupportReport::hidePrivate(const QString& text, const SupportInfo& info) {
    QString out = text;
    if (info.home.size() > 1) out.replace(info.home, "~");
    out.replace(QRegularExpression("/(?:home|Users)/[^/\\s\"'`)]+"), "~");
    auto word = [&out](const QString& name, const QString& with) {
        if (name.size() >= 3) out.replace(QRegularExpression("\\b" + QRegularExpression::escape(name) + "\\b"), with);
    };
    word(info.user, "<user>");
    word(info.host, "<host>");
    return out;
}

QString SupportReport::body(const SupportInfo& info) { return bodyWith(info, info.logTail); }

QString SupportReport::title(const SupportInfo& info) {
    return hidePrivate(clip(info.title.isEmpty() ? "Problem report" : info.title, 120), info);
}

QString SupportReport::text(const SupportInfo& info) { return title(info) + "\n\n" + body(info); }

QString SupportReport::issueUrl() { return kIssueUrl; }

QUrl SupportReport::url(const SupportInfo& info) {
    QString title = SupportReport::title(info);
    QStringList log = info.logTail;
    QUrl url = build(title, bodyWith(info, log));
    while (url.toEncoded().size() > kMaxSupportUrlLength && !log.isEmpty()) {
        log.removeFirst();  // the newest lines matter most: the oldest go first
        url = build(title, bodyWith(info, log));
    }
    // Still too long without any log: the free texts are cut harder.
    for (int max = kMaxLineLength / 2; url.toEncoded().size() > kMaxSupportUrlLength && max >= 20; max /= 2) {
        SupportInfo cut = info;
        cut.action = clip(info.action, max);
        cut.linking = clip(info.linking, max);
        cut.validation = clip(info.validation, max);
        url = build(title, bodyWith(cut, {}));
    }
    return url;
}
