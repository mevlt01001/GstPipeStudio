#include "pluginpaths.h"

#include "apppaths.h"
#include <QDir>

QString PluginPaths::root() { return AppPaths::plugins(); }
QString PluginPaths::binaries() { return QDir(root()).filePath(".bin"); }

QString PluginPaths::searchPath(const QProcessEnvironment& environment) {
    const QDir directory(binaries());
    if (directory.entryList({"libgst*.so"}, QDir::Files).isEmpty()) return {};
    QStringList paths{directory.absolutePath()};
    // GStreamer uses the unversioned variable only when the versioned one is unset.
    const auto inherited = environment.value("GST_PLUGIN_PATH_1_0", environment.value("GST_PLUGIN_PATH"));
    paths.append(inherited.split(QDir::listSeparator(), Qt::SkipEmptyParts));
    paths.removeDuplicates();
    return paths.join(QDir::listSeparator());
}
