// supportreport.h – "Request support" (UI_RULES 3.3): builds a prefilled GitHub issue and opens it in the browser.
// No widgets. The user reads the text, adds detail and presses submit on GitHub.
#pragma once

#include <QString>
#include <QStringList>
#include <QUrl>

constexpr int kMaxSupportUrlLength = 6000;  // the whole percent-encoded URL

struct SupportInfo {
    QString title;       // what went wrong, in a few words
    QString action;      // what the user was doing
    QString linking;     // pads being linked: "a.src (videotestsrc) -> b.sink (videoconvert)"
    QString validation;  // last validation status and message
    QStringList logTail; // newest last
    // The machine: filled by SupportReport::systemInfo().
    QString appVersion, os, arch, qt, gstreamer, driver;
    int nvidiaElements = -1;  // -1: unknown
    // What must never leave the machine: removed from every text.
    QString home, user, host;
};

class UrlOpener {
public:
    virtual ~UrlOpener() = default;
    virtual bool open(const QUrl& url) = 0;
};

class DesktopUrlOpener : public UrlOpener {
public:
    bool open(const QUrl& url) override;  // QDesktopServices::openUrl
};

class SupportReport {
public:
    static SupportInfo systemInfo();  // app, OS, CPU, Qt, NVIDIA driver, home / user / host to hide
    static QString body(const SupportInfo& info);  // Markdown, privacy applied, all log lines
    static QString title(const SupportInfo& info);
    static QString text(const SupportInfo& info);  // title and body, for a person without a browser
    static QString issueUrl();
    static QUrl url(const SupportInfo& info);      // within kMaxSupportUrlLength: the oldest log lines go first
    static bool open(const SupportInfo& info, UrlOpener& opener) { return opener.open(url(info)); }

    static QString hidePrivate(const QString& text, const SupportInfo& info);
};
