// mainwindow.h – toolbar, menus, the document tabs over the canvas, the Explorer and the panels, status bar (UI_RULES 2).
// Every open file is a Document, a tab (DocumentTabs); the commands (CommandSet), menus and panels act on the one in
// front. It only connects widgets to the Controllers and to Flows; it holds no pipeline logic.
#pragma once

#include <QMainWindow>
#include <functional>
#include <memory>

#include "commandset.h"
#include "componentprovider.h"
#include "documentregistry.h"

class AppServices;
class CanvasScene;
class CanvasView;
class ContextMenus;
class Controller;
class Document;
class DocumentTabs;
class DocumentSessions;
class Drawer;
class Explorer;
class FileOperations;
class Flows;
class LogPanel;
class QLabel;
class QTabBar;
class Panels;
class SaveFlow;
class SetupPage;
class UpdatePanel;
class ValidationIndicator;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(DocumentSessions& sessions, QWidget* parent = nullptr);
    // A caller can lend the first controller to inspect a window over an existing document.
    explicit MainWindow(Controller* controller, QWidget* parent = nullptr);
    ~MainWindow() override;

    // ---- the tabs: one Document each; the one in front is what the commands, menus and panels act on
    Document* front() const;
    QList<Document*> documents() const;  // in tab order
    DocumentTabs* documentTabs() const { return tabs_; }
    QTabBar* tabs() const;
    Document* newDocument();                 // an untitled tab, in front
    bool closeDocument(Document* document);  // asks to save unsaved changes (SaveFlow); false when the user cancels
    void activate(Document* document);       // brings its tab to the front
    void restoreSession();  // the tabs open when the window last closed, and the ones a crash left (hot exit)
    // A file opens in a tab of its own; a file already open comes to the front.
    void openFile(const QString& path);
    // A library template on a canvas tab of its own: Save writes its next revision, the other tabs are offered it.
    Document* openTemplate(const QString& name);
    Document* openComponent(const ComponentRef& component);
    void openCapsTab(const QString& srcElement, const QString& srcIdentity);

    // ---- the front document's parts
    CanvasScene* scene() const;
    CanvasView* view() const;
    Flows* flows() const;
    Drawer* drawer() const;
    UpdatePanel* updatePanel() const;
    LogPanel* logPanel() const;
    QWidget* propertiesTab() const;
    QWidget* capsTab() const;
    SetupPage* setupPage() const;
    ValidationIndicator* indicator() const;
    bool showingSetup() const;

    // ---- the window's own parts
    CommandSet* commands() const { return commands_; }
    ContextMenus* contextMenus() const { return menus_; }
    Panels* panels() const { return panels_; }
    Explorer* explorer() const { return explorer_; }
    FileOperations* fileOperations() const { return files_; }  // every file operation the user asks for
    SaveFlow* saveFlow() const { return saveFlow_; }
    DocumentRegistry* registry() const { return registry_; }  // the open tabs' files, watched on disk

protected:
    void closeEvent(QCloseEvent* event) override;  // hot exit: asks nothing, every tab comes back on the next start

private:
    MainWindow(AppServices& services, DocumentSessions* sessions, Controller* first, QWidget* parent);
    Controller* controller() const;  // the front document's
    void buildCommands();
    void buildMenus();
    void buildStatusBar();
    void added(Document* document);  // its pages in the panels, its signals wired
    void wire(Document* document);
    void activated(Document* document);
    // The state the window shows (commands, Explorer, status bar, title), worked out once per turn of the event loop
    // however many changes asked for it.
    void scheduleRefresh();
    void refreshState();
    void showExplorer();  // what the editor shows, for the Explorer and its title
    void showStatus();
    void onEnvironment();
    void onChangedOnDisk(Document* document, DiskChange why);
    void askReload(Document* document, DiskChange why);
    // then, once the document no longer runs or validates; while it waits, a second ask is dropped.
    void whenIdle(Document* document, const std::function<void()>& then);
    void onLibraryChanged();  // a template a tab edits may have a newer revision, saved from another tab
    void askTemplateChange(const QString& id);
    void askTemplatesOutdated(const QStringList& ids);
    Document* openComponent(const ComponentRef& component, bool dismissDrawer);
    void showVersionHistory();
    void importLaunch();
    void focusSearch();
    void showAbout();
    // Why commands are off: GStreamer is not ready, a step is running, the canvas cannot be edited now, or nothing fit is
    // selected.
    QString environmentReason() const;
    QString busyReason() const;
    QString editReason() const;
    QString addReason() const;
    QString selectionReason(int least = 1) const;
    QString copyTitle() const;  // the template copy a context menu is about, as the user reads it

    AppServices& services_;
    std::unique_ptr<DocumentSessions> ownedSessions_;
    SaveFlow* saveFlow_;      // every save the user asks for, and every question about unsaved changes
    DocumentRegistry* registry_;
    FileOperations* files_;
    DocumentTabs* tabs_;
    CommandSet* commands_;
    ContextMenus* menus_;
    Panels* panels_ = nullptr;
    Explorer* explorer_ = nullptr;
    QLabel* state_ = nullptr;
    QLabel* environment_ = nullptr;
    QLabel* command_ = nullptr;
    bool built_ = false;         // menus and actions exist: the state can be shown
    bool refreshDue_ = false;    // a refresh is on its way
};
