// explorer.h – the Explorer panel, as IDEs have it: one tree of sections (ExplorerSource), each row an ExplorerEntry
// drawn by ExplorerLook. It follows the editor: the front tab's file is selected and revealed, open files are bold,
// unsaved ones carry a dot, and the title counts the unsaved tabs. Double click or Enter opens, F2 renames in place
// with live validation, Del moves to the trash, and the right click offers what the row's section supports; every
// one of these is a FileOperations call. Watched folders are read again 150 ms after they settle, and a section that
// did not change is not rebuilt, so expanded folders and the selection stay.
#pragma once

#include <QFileSystemWatcher>
#include <QList>
#include <QTimer>
#include <QWidget>

#include "explorerentry.h"

class ExplorerSource;
class FileOperations;
class QTreeWidget;
class QTreeWidgetItem;

class Explorer : public QWidget {
    Q_OBJECT
public:
    static constexpr int kSettleMs = 150;

    Explorer(const QList<ExplorerSource*>& sections, FileOperations& operations, QWidget* parent = nullptr);

    void present(const EditorState& editor);  // what the editor shows now; the window tells after every change
    void refresh();                           // every section read again now
    QString title() const;                    // the panel's title: "Explorer", or "Explorer · 2 unsaved"
    QTreeWidget* tree() const { return tree_; }
    ExplorerEntry entryOf(const QTreeWidgetItem* item) const;  // a row's entry, with the editor's state on it
    QTreeWidgetItem* itemOf(const QString& entryId) const;     // the row of an entry (ExplorerEntry::id), null if none

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;  // Del in the tree

private:
    struct Section {
        ExplorerSource* source;
        QTreeWidgetItem* header;
        ExplorerEntry head;              // its header as last built
        QList<ExplorerEntry> entries;    // its rows as last built
        bool built = false;
    };
    void reload(Section& section);  // asks the section; rebuilds its rows only when they changed
    void fill(QTreeWidgetItem* parent, const Section& section, const QList<ExplorerEntry>& entries, const QStringList& expanded);
    void decorate(QTreeWidgetItem* item);  // its look, with the editor's state, and its children's
    void reveal(const QString& key);       // the row of a tab's session key selected and scrolled to
    void rewatch();
    void showMenu(const QPoint& at);
    void startRename(const QString& entryId);
    Section* sectionOf(const QTreeWidgetItem* item);
    ExplorerEntry sourceEntry(const QTreeWidgetItem* item) const;  // as its section made it

    QList<Section> sections_;
    FileOperations& operations_;
    QTreeWidget* tree_;
    QFileSystemWatcher watcher_;
    QTimer settle_;
    EditorState editor_;
};
