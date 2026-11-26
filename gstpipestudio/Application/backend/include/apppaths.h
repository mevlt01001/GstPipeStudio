// apppaths.h – the one owner of the per-user folders, by the platform's rules (QStandardPaths: XDG base directories on
// Linux, AppData on Windows, Application Support on macOS). Every folder is created on first use.
// GPS_USER_DIR replaces the data and state bases with <dir>/data and <dir>/state (tests, portable use).
#pragma once

#include <QString>

namespace AppPaths {

QString state();      // what the app remembers: the list of projects, the hot-exit backups

QString templates();  // data/templates: one .conf per template
QString backups();    // state()/backups: the unsaved content of the open tabs (BackupService)

}  // namespace AppPaths
