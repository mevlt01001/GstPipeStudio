#pragma once
#include <QString>
namespace BuildInfo {
inline QString version() {
    return QStringLiteral(GPS_APP_VERSION);
}
inline QString revision() {
    return QStringLiteral(GPS_APP_REVISION).left(12);
}
inline QString channel() {
    return QStringLiteral(GPS_APP_CHANNEL);
}
inline bool development() {
    return channel() != "Release";
}
inline QString title() {
    return "GstPipeStudio" + (development() ? " · Development" : QString());
}
inline QString settingsName() {
    return development() ? "gstpipestudio-development" : "gstpipestudio";
}
} // namespace BuildInfo
