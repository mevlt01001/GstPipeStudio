// mainwindow.h – toolbar, menus, source panel, canvas, log and update panels, status bar (SPEC 8).
// It only connects widgets to the Controller and to Flows; it holds no pipeline logic.
#pragma once

#include <QMainWindow>

class CanvasScene;
class CanvasView;
class Controller;
class Flows;
class LogPanel;
class QAction;
class QLabel;
class QProgressBar;
class QTabWidget;
class SourcePanel;
class UpdatePanel;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(Controller* controller, QWidget* parent = nullptr);

    CanvasScene* scene() const { return scene_; }
    CanvasView* view() const { return view_; }
    Flows* flows() const { return flows_; }
    SourcePanel* sourcePanel() const { return sources_; }
    UpdatePanel* updatePanel() const { return updates_; }
    LogPanel* logPanel() const { return log_; }
    QTabWidget* bottomTabs() const { return tabs_; }

    void openFile(const QString& path);
    bool saveFile(const QString& path);  // "" = where the plan lives now

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void buildActions();
    void buildPanels();
    void refreshState();
    void onRebuilt();
    bool confirmDiscard();  // true when it is fine to drop the plan on screen
    void saveAs();
    void toggleRun();

    Controller* controller_;
    Flows* flows_;
    CanvasScene* scene_;
    CanvasView* view_;
    SourcePanel* sources_;
    UpdatePanel* updates_;
    LogPanel* log_;
    QTabWidget* tabs_;
    QLabel* state_;
    QLabel* command_;
    QProgressBar* progress_;
    QStringList lastNames_;  // element names at the last rebuild: the view refits when they change
    bool fitNext_ = false;
};
