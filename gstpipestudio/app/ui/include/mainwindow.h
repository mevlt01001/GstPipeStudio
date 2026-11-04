// mainwindow.h – toolbar, menus, canvas with the left drawer, log and validation panels, status bar (UI_RULES 2).
// It only connects widgets to the Controller and to Flows; it holds no pipeline logic.
#pragma once

#include <QMainWindow>

#include "startupcheck.h"

class CanvasScene;
class CanvasView;
class Controller;
class Drawer;
class Flows;
class LogPanel;
class QAction;
class QLabel;
class QStackedWidget;
class QTabWidget;
class SetupPage;
class UpdatePanel;
class ValidationIndicator;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(Controller* controller, QWidget* parent = nullptr);

    CanvasScene* scene() const { return scene_; }
    CanvasView* view() const { return view_; }
    Flows* flows() const { return flows_; }
    Drawer* drawer() const { return drawer_; }
    UpdatePanel* updatePanel() const { return updates_; }
    LogPanel* logPanel() const { return log_; }
    QTabWidget* bottomTabs() const { return tabs_; }
    SetupPage* setupPage() const { return setup_; }
    ValidationIndicator* indicator() const { return indicator_; }
    bool showingSetup() const;

    void openFile(const QString& path);
    bool saveFile(const QString& path);  // "" = where the plan lives now
    void openCapsTab(const QString& srcElement, const QString& srcIdentity);

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void buildActions();
    void buildPanels();
    void refreshState();
    void onRebuilt();
    void onEnvironment();
    bool confirmDiscard();  // true when it is fine to drop the plan on screen
    void saveAs();
    void toggleRun();
    void showAbout();
    void deleteSelected();
    void copySelected();
    void cutSelected();
    void pasteClipboard();
    void setReason(QAction* action, const QString& text, const QString& reason);

    Controller* controller_;
    Flows* flows_;
    CanvasScene* scene_;
    CanvasView* view_;
    Drawer* drawer_;
    QStackedWidget* centre_;
    SetupPage* setup_;
    UpdatePanel* updates_;
    LogPanel* log_;
    QWidget* capsTab_ = nullptr;
    QTabWidget* tabs_;
    QLabel* state_;
    QLabel* environment_;
    QLabel* command_;
    ValidationIndicator* indicator_;
    QStringList lastNames_;  // element names at the last rebuild: the view refits when they change
    bool fitNext_ = false;
};
