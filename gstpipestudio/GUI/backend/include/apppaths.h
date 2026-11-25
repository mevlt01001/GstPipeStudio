// apppaths.h – the one owner of the per-user folders, by the platform's rules (QStandardPaths: XDG base directories on
// Linux, AppData on Windows, Application Support on macOS). Every folder is created on first use.
// GPS_USER_DIR replaces the three bases with <dir>/config, <dir>/data and <dir>/state (tests, portable use).
#pragma once

#include <QString>

namespace AppPaths {

QString config();     // settings
QString data();       // what the user makes and keeps: the template library
QString state();      // what the app remembers: the list of projects

QString templates();  // data()/templates: one .conf per template

}  // namespace AppPaths
