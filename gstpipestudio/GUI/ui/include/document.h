// document.h – one open document, a tab of the window: its Controller, its canvas with the drawer, its flows and the
// pages it shows in the panels (Log, Validation, Live metrics, Properties, Caps) and in the status bar. The window
// shows one document at a time; everything here belongs to the document and goes with it when its tab closes.
#pragma once

#include <QObject>
#include <QStringList>

class AppServices;
class CanvasScene;
class CanvasView;
class Controller;
class Drawer;
class Flows;
class LogPanel;
class QStackedWidget;
class QWidget;
class SetupPage;
class TelemetryPanel;
class UpdatePanel;
class ValidationIndicator;

class Document : public QObject {
    Q_OBJECT
public:
    // A document over the caller's controller (it stays the caller's), or over one of its own, with the same services,
    // when controller is null.
    Document(AppServices& services, Controller* controller, QWidget* window);
    ~Document() override;

    Controller* controller() const { return controller_; }
    CanvasScene* scene() const { return scene_; }
    CanvasView* view() const { return view_; }
    Drawer* drawer() const { return drawer_; }
    Flows* flows() const { return flows_; }
    SetupPage* setup() const { return setup_; }
    QStackedWidget* centre() const { return centre_; }  // the canvas page, or the setup page while GStreamer is not ready
    LogPanel* log() const { return log_; }
    UpdatePanel* updates() const { return updates_; }
    TelemetryPanel* telemetry() const { return telemetry_; }
    QWidget* propsTab() const { return propsTab_; }
    QWidget* capsTab() const { return capsTab_; }
    ValidationIndicator* indicator() const { return indicator_; }
    QList<QWidget*> panelPages() const;  // what it shows in the panels, in their order

    QString title() const;  // the file's name, "Template: <name>", or "untitled"
    QString path() const;   // the file it is, "" when it has none yet
    bool pristine() const;  // untitled, empty and unchanged: opening a file may use it instead of a new tab
    bool showsFile(const QString& file) const;
    bool showsTemplate(const QString& name) const;

    // A file is opening in it: its tab already reads the file's name, and the view fits it once it is on the canvas.
    void opening(const QString& title) {
        fitNext_ = true;
        opening_ = title;
    }
    // The library has newer templates while another tab is in front: asked when this one comes to the front.
    void deferOutdated(const QStringList& ids) { outdated_ = ids; }
    QStringList takeOutdated() { return std::exchange(outdated_, {}); }

private:
    void rebuilt();

    Controller* controller_ = nullptr;
    CanvasScene* scene_ = nullptr;
    CanvasView* view_ = nullptr;
    Drawer* drawer_ = nullptr;
    Flows* flows_ = nullptr;
    SetupPage* setup_ = nullptr;
    QStackedWidget* centre_ = nullptr;
    LogPanel* log_ = nullptr;
    UpdatePanel* updates_ = nullptr;
    TelemetryPanel* telemetry_ = nullptr;
    QWidget* propsTab_ = nullptr;
    QWidget* capsTab_ = nullptr;
    ValidationIndicator* indicator_ = nullptr;
    QStringList lastNames_;  // element names at the last rebuild: the view refits when they change
    bool fitNext_ = false;
    QString opening_;
    QStringList outdated_;
};
