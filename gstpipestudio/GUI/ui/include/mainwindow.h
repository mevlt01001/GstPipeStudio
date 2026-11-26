// mainwindow.h – toolbar, menus, the document tabs over the canvas, the Explorer and the panels, status bar (UI_RULES 2).
// Every open file is a Document, a tab; the menus and panels act on the one in front. It only connects widgets to the
// Controllers and to Flows; it holds no pipeline logic.
#pragma once

#include <QMainWindow>
#include <memory>

#include "componentprovider.h"

class AppServices;
class CanvasScene;
class CanvasView;
class Controller;
class Document;
class DocumentRegistry;
class Drawer;
class Explorer;
class FileOperations;
class Flows;
class LogPanel;
class QAction;
class QLabel;
class QMenu;
class QStackedWidget;
class QTabBar;
class Panels;
class SaveFlow;
class SaveTarget;
struct Backup;
class SetupPage;
class UpdatePanel;
class TelemetryPanel;
class ValidationIndicator;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    // The first tab shows the caller's controller; tabs opened later make their own, with its services.
    explicit MainWindow(Controller* controller, QWidget* parent = nullptr);
    ~MainWindow() override;

    CanvasScene* scene() const { return scene_; }
    CanvasView* view() const { return view_; }
    Flows* flows() const { return flows_; }
    Drawer* drawer() const { return drawer_; }
    UpdatePanel* updatePanel() const { return updates_; }
    LogPanel* logPanel() const { return log_; }
    Panels* panels() const { return panels_; }
    Explorer* explorer() const { return explorer_; }
    FileOperations* fileOperations() const { return files_; }  // every file operation the user asks for
    SaveFlow* saveFlow() const { return saveFlow_; }
    DocumentRegistry* registry() const { return registry_; }  // the open tabs' files, watched on disk
    QWidget* propertiesTab() const { return propsTab_; }
    QWidget* capsTab() const { return capsTab_; }
    SetupPage* setupPage() const { return setup_; }
    ValidationIndicator* indicator() const { return indicator_; }
    bool showingSetup() const;

    // ---- the tabs: one Document each, the one in front is what the menus and panels act on
    QTabBar* tabs() const { return tabs_; }
    Document* document() const { return doc_; }
    QList<Document*> documents() const;  // in tab order
    Document* newDocument();                    // an untitled tab, in front
    Document* blankTab();                       // where a file opens: the front tab when it is pristine, else a new one
    bool closeDocument(Document* document);     // asks to save unsaved changes (SaveFlow); false when the user cancels
    void activate(Document* document);          // brings its tab to the front
    // The tabs open when the window last closed, and the ones a crash left: each with its unsaved changes (hot exit).
    void restoreSession();

    // A file opens in a tab of its own; a file already open comes to the front.
    void openFile(const QString& path);
    // The tab of a target, opened as above; with a backup (its id), the backup's content on it, modified.
    Document* open(std::unique_ptr<SaveTarget> target, const QString& backup = QString());
    // A library template on a canvas tab of its own: Save writes its next revision, the other tabs are offered it.
    Document* openTemplate(const QString& name);
    Document* openComponent(const ComponentRef& component);  // a template: openTemplate; nothing else opens yet
    void openPropertiesTab(const QString& element);
    void resetPropertiesTab();
    void openCapsTab(const QString& srcElement, const QString& srcIdentity);
    void resetCapsTab();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;  // a middle click closes a tab
    void closeEvent(QCloseEvent* event) override;  // hot exit: asks nothing, every tab comes back on the next start

private:
    Document* addDocument(Controller* controller);
    void wire(Document* document);
    void refreshTab(Document* document);
    void showSaveAll();
    Document* docAt(int index) const;
    int indexOf(Document* document) const;
    void saveSession();
    Document* reopen(const Backup& tab);  // a tab of restoreSession; null when there is nothing to reopen
    void onChangedOnDisk(Document* document);
    void askReload(Document* document);
    void showExplorer();      // what the editor shows, for the Explorer and its title

    void showVersionHistory();
    void askTemplateChange(const QString& id);
    void askTemplatesOutdated(const QStringList& ids);
    void buildActions();
    void showViewPreferences();
    void buildPanels();
    void refreshState();
    void onEnvironment();
    void toggleRun();
    void showAbout();
    void showCanvasMenu(const QPoint& screenPos);
    void onElementMenuRequested(const QString& element, const QPoint& screenPos);
    void showTemplateMenu(const QString& instanceId, const QPoint& screenPos);  // right click on an expanded template's border
    void showSelectionMenu(const QString& clickedElement, const QPoint& screenPos);
    void addEditTemplate(QMenu* menu, const QString& instanceId);
    void saveSelectedAsTemplate();
    void connectSelected();
    void deleteSelected();
    void duplicateSelected();
    void selectAll();
    void copySelected();
    void cutSelected();
    void pasteClipboard();
    void setReason(QAction* action, const QString& text, const QString& reason);

    AppServices& services_;  // the process's, shared by every tab
    SaveFlow* saveFlow_;      // every save the user asks for, and every question about unsaved changes
    DocumentRegistry* registry_;
    FileOperations* files_;

    // The front document's parts: the menus, panels and status bar act on these.
    Controller* controller_ = nullptr;
    Flows* flows_;
    CanvasScene* scene_;
    CanvasView* view_;
    Drawer* drawer_;
    QStackedWidget* centre_;
    SetupPage* setup_;
    UpdatePanel* updates_;
    TelemetryPanel* telemetry_;
    Explorer* explorer_ = nullptr;
    LogPanel* log_;
    QWidget* propsTab_ = nullptr;
    QWidget* capsTab_ = nullptr;
    Panels* panels_ = nullptr;
    QLabel* state_;
    QLabel* environment_;
    QLabel* command_;
    ValidationIndicator* indicator_;

    QTabBar* tabs_;
    QStackedWidget* pages_;
    QList<Document*> docs_;
    Document* doc_ = nullptr;
    bool built_ = false;  // menus and actions exist: the state can be shown
};
