// explorerentry.h – one row of the Explorer: what it is, which section it is in, the file or component it stands
// for and its state. The sections make entries, the Explorer draws them (ExplorerLook) and FileOperations acts on them.
// What the editor has open (EditorState) is laid over the states the sections know from the disk.
#pragma once

#include <QHash>
#include <QList>
#include <QMetaType>
#include <QString>

struct ExplorerEntry {
    enum class Kind {
        Section,    // a section's header
        Hint,       // a muted line: what an empty section will show
        Folder,
        File,
        Conf,       // a pipeline file: it opens in a tab
        Project,    // a project folder of the Projects section; path is its conf
        Component,  // a component of a provider (a template); path is its id
    };
    // From the disk: Ok, Outdated (uses older template revisions than the library), Missing (listed, not on disk).
    // From the editor: Open, Unsaved, Deleted (its tab's file was deleted on disk).
    enum class State { Ok, Open, Unsaved, Outdated, Missing, Deleted };
    enum class Validation { None, Valid, Invalid };

    Kind kind = Kind::Hint;
    QString origin;  // the section it is in (ExplorerSource::id)
    QString path;    // a file or folder: its absolute path; a project: its conf; a component: its id
    QString name;    // what the row reads
    QString detail;  // more, for its tooltip: a component's description, the templates an outdated file holds
    State state = State::Ok;
    Validation validation = Validation::None;
    QList<ExplorerEntry> children;  // a folder's

    QString id() const { return origin + "|" + (kind == Kind::Project ? location() : path); }  // which row it is, across rebuilds
    QString key() const;       // the session key of the tab it opens in (SaveTarget), "" when it opens in none
    QString location() const;  // the file or folder on disk: a project's folder; "" for a component
    bool isFolder() const { return kind == Kind::Folder || kind == Kind::Project; }
    bool onDisk() const { return isFolder() || kind == Kind::File || kind == Kind::Conf; }
    bool operator==(const ExplorerEntry& other) const;
};
Q_DECLARE_METATYPE(ExplorerEntry)

// What the editor shows: the front tab, its project and every open tab's state, by session key.
struct EditorState {
    QString front;        // the front tab's session key
    QString projectConf;  // the front tab's project conf, "" when it is none
    QHash<QString, ExplorerEntry::State> open;  // Open, Unsaved or Deleted
    QHash<QString, ExplorerEntry::Validation> validation;
    int unsaved = 0;      // tabs with unsaved work, untitled ones too

    QStringList openFiles() const;  // the paths of the open project confs
    bool operator==(const EditorState& other) const;
};
