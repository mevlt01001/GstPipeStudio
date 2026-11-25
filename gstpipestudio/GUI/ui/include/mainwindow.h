// mainwindow.h – toolbar, menus, the document tabs over the canvas, the Explorer and the panels, status bar (UI_RULES 2).
// Every open file is a Document, a tab; the menus and panels act on the one in front. It only connects widgets to the
// Controllers and to Flows; it holds no pipeline logic.
#pragma once

#include <QMainWindow>

class AppServices;
class CanvasScene;
class CanvasView;
class Controller;
class Document;
class Drawer;
class Explorer;
class Flows;
class LogPanel;
class QAction;
class QLabel;
class QMenu;
class QStackedWidget;
class QTabBar;
class Panels;
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
    QWidget* propertiesTab() const { return propsTab_; }
    QWidget* capsTab() const { return capsTab_; }
    SetupPage* setupPage() const { return setup_; }
    ValidationIndicator* indicator() const { return indicator_; }
    bool showingSetup() const;

    // ---- the tabs: one Document each, the one in front is what the menus and panels act on
    QTabBar* tabs() const { return tabs_; }
    Document* document() const { return doc_; }
    QList<Document*> documents() const { return docs_; }
    Document* newDocument();                    // an untitled tab, in front
    bool closeDocument(Document* document);     // asks to save unsaved changes; false when the user cancels
    void activate(Document* document);          // brings its tab to the front
    void restoreSession();                      // the tabs open when the window last closed

    // A file opens in a tab of its own; a file already open comes to the front.
    void openFile(const QString& path);
    // A library template on a canvas tab of its own: Save writes its next revision, the other tabs are offered it.
    Document* openTemplate(const QString& name);
    bool saveFile(const QString& path);  // "" = where the plan lives now
    void openPropertiesTab(const QString& element);
    void resetPropertiesTab();
    void openCapsTab(const QString& srcElement, const QString& srcIdentity);
    void resetCapsTab();

protected:
    void closeEvent(QCloseEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;  // a middle click closes a tab

private:
    Document* addDocument(Controller* controller);
    void wire(Document* document);
    void refreshTab(Document* document);
    Document* docAt(int index) const;
    int indexOf(Document* document) const;
    Document* target();                 // where a file opens: the front tab when it is pristine, else a new one
    bool confirmClose(Document* document);  // Save, Don't save or Cancel, as editors ask
    bool save();                        // Save of the front tab; a never saved canvas becomes a project first
    void saveSession();

    void showVersionHistory();
    void askTemplateChange(const QString& id);
    void askTemplatesOutdated(const QStringList& ids);
    void buildActions();
    void showViewPreferences();
    void buildPanels();
    void refreshState();
    void onEnvironment();
    bool confirmDiscard();  // true when it is fine to drop the plan on screen
    void saveAs();
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
    Explorer* explorer_;
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
