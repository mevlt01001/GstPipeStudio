// documentregistry.h – the files of the open tabs, watched on disk (one QFileSystemWatcher, over the files and their
// folders, so a file replaced by a rename is still seen). It remembers the content each document last read or wrote,
// so the document's own saves never count; a change made by anything else is said once per new content, and a deleted
// file marks its document deleted. What the window does about it is MainWindow's.
#pragma once

#include <QByteArray>
#include <QFileSystemWatcher>
#include <QHash>
#include <QObject>
#include <QTimer>

class Document;

// Why a tab's file changed: another program wrote it, or the template library wrote a newer revision of the template
// the tab edits (another tab saved it).
enum class DiskChange { External, Library };

class DocumentRegistry : public QObject {
    Q_OBJECT
public:
    static constexpr int kSettleMs = 150;  // a burst of watcher signals is checked once

    explicit DocumentRegistry(QObject* parent = nullptr);

    void track(Document* document);  // from now until it is destroyed, whatever file it is bound to
    bool changedSinceRead(Document* document) const;  // its file on disk is not what it read or wrote (Save asks)
    void accept(Document* document);  // the user keeps the document's content over the file on disk (Keep mine)
    // The application itself wrote the document's file elsewhere (the library, for another tab): it is not said as a
    // change by another program. True when the file is not what the document read.
    bool catchUp(Document* document);

signals:
    void changedOnDisk(Document* document);  // once per new content; a busy tab is asked later, by the window
    void deletedOnDisk(Document* document);

private:
    struct Entry {
        QString path;     // the file it is bound to, "" when none
        QByteArray read;  // the content it last read or wrote, as a hash; empty: there was no file
        QByteArray seen;  // the content last said, so a change is said once
    };
    void remember(Document* document);  // the document holds its file as it is on disk now
    void check();
    void rewatch();
    static QByteArray stamp(const QString& path);  // the file's content as a hash, empty when there is no file

    QFileSystemWatcher watcher_;
    QTimer settle_;
    QHash<Document*, Entry> entries_;
};
