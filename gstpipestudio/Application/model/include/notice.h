#pragma once

#include <QString>

enum class NoticeSeverity { Info, Warning, Error };

struct Notice {
    NoticeSeverity severity = NoticeSeverity::Error;
    QString title;
    QString detail;
    bool showLog = true;

    QString summary() const { return detail.isEmpty() ? title : title + ": " + detail; }
    bool sameAs(const Notice& other) const {
        return severity == other.severity && title == other.title && detail == other.detail;
    }
};
