#pragma once

#include <QProcessEnvironment>
#include <QString>

namespace PluginPaths {

QString root();
QString binaries();
// Empty when there are no managed binaries. Otherwise includes the inherited GStreamer search path.
QString searchPath(const QProcessEnvironment& environment);

}  // namespace PluginPaths
