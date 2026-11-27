// fileoperations.h – the one service that acts on files and components for the user: open, rename, delete (to the
// trash), reveal in the file manager, copy the path, export, import and New project. The Explorer, the picker's
// component menu and the File menu all come here. What a rename or a delete means for the open tabs is decided here
// too: a renamed conf moves its manifest entry and its version history and its tab is bound to the new file; a
// component renamed anywhere (here or by its provider) re-binds the tab that edits it.
#pragma once

#include <QList>
#include <QObject>
#include <QPoint>
#include <functional>
#include <memory>

#include "componentprovider.h"
#include "explorerentry.h"
#include "explorersource.h"
#include "supportreport.h"

class AppServices;
class Document;
class MainWindow;
class SaveTarget;
class PluginStudio;

class FileOperations : public QObject {
    Q_OBJECT
public:
    FileOperations(AppServices& services, MainWindow* window);

    void run(ExplorerAction action, const ExplorerEntry& entry);  // what a menu chose
    // The menu of an entry. own carries out the actions its caller does itself (the Explorer renames in place and
    // refreshes its sections): true when it did.
    using Own = std::function<bool(ExplorerAction)>;
    void popup(const QList<ExplorerAction>& actions, const ExplorerEntry& entry, const QPoint& at, const Own& own = {});
    void componentMenu(const ComponentRef& component, const QPoint& at);  // a component's menu, wherever it is listed

    void open(const ExplorerEntry& entry);                                   // a conf or a project in a tab; a component to edit
    QString nameError(const ExplorerEntry& entry, const QString& wanted) const;  // why it cannot be called so, "" when it can
    bool rename(const ExplorerEntry& entry, const QString& wanted);         // a refusal is said
    bool remove(const ExplorerEntry& entry);  // asks first, warning about unsaved tabs; to the trash
    void reveal(const ExplorerEntry& entry);
    void copyPath(const ExplorerEntry& entry);
    void exportComponent(const ExplorerEntry& entry);
    void importInto(const QString& providerKind);
    void newPipeline(const ExplorerEntry& parentEntry);
    void duplicateFile(const ExplorerEntry& entry);
    void runPipeline(const ExplorerEntry& entry);
    void newProject();
    void newPlugin();
    bool closePluginEditors();

    void setUrlOpener(UrlOpener* opener) { opener_ = opener; }  // what Reveal opens folders with (tests)

signals:
    void wrote();  // a file or a component changed through it: what lists them reads them again

private:
    QList<Document*> tabsOf(const ExplorerEntry& entry) const;  // the open tabs of a file, of a folder's files, or of a component
    void rebind(const QString& key, std::unique_ptr<SaveTarget> target);  // the tab of key is bound to target now
    bool renameFile(const ExplorerEntry& entry, const QString& to);
    void fail(const QString& title, const QString& text) const;

    struct LibraryFiles {
        std::function<void()> import;
        std::function<void(const QString&)> exportTo;
        std::function<bool(const QString&)> close;
    };
    AppServices& services_;
    MainWindow* window_;
    PluginStudio* plugins_;
    QHash<QString, LibraryFiles> libraryFiles_;
    DesktopUrlOpener desktop_;
    UrlOpener* opener_ = &desktop_;
};
