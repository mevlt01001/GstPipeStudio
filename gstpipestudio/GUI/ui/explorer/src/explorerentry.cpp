// explorerentry.cpp – see explorerentry.h.
#include "explorerentry.h"

#include <QFileInfo>

#include "savetarget.h"

// A conf opens as a project conf, a component by its provider's kind ("template:Tail" is a LibraryTemplateTarget).
QString ExplorerEntry::key() const {
    switch (kind) {
    case Kind::Conf:
    case Kind::Project: return ProjectConfTarget::keyFor(path);
    case Kind::Component: return SaveTarget::key(origin.toUtf8().constData(), path);
    default: return QString();
    }
}

QString ExplorerEntry::location() const {
    if (kind == Kind::Project) return QFileInfo(path).absolutePath();
    return onDisk() ? path : QString();
}

bool ExplorerEntry::operator==(const ExplorerEntry& other) const {
    return kind == other.kind && origin == other.origin && path == other.path && name == other.name && detail == other.detail &&
           state == other.state && children == other.children;
}

QStringList EditorState::openFiles() const {
    QStringList out;
    for (auto it = open.cbegin(); it != open.cend(); ++it) {
        if (SaveTarget::kindOf(it.key()) == ProjectConfTarget::kKind) out << SaveTarget::valueOf(it.key());
    }
    out.sort();  // a hash has no order
    return out;
}

bool EditorState::operator==(const EditorState& other) const {
    return front == other.front && projectConf == other.projectConf && open == other.open && unsaved == other.unsaved;
}
