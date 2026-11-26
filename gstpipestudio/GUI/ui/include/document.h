// document.h – one open document, a tab of the window: its Controller, its canvas with the drawer, its flows and the
// pages it shows in the panels (Log, Validation, Live metrics, Properties, Caps) and in the status bar. The window
// shows one document at a time; everything here belongs to the document and goes with it when its tab closes.
#pragma once

#include <QObject>
#include <QStringList>
#include <optional>

#include "documentregistry.h"

class CanvasScene;
class CanvasView;
class Controller;
class DocumentBackup;
class DocumentSession;
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
    Document(DocumentSession& session, QWidget* window);
    ~Document() override;

    DocumentSession& session() const { return session_; }
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
    DocumentBackup* backup() const { return backup_; }  // its hot-exit backup
    QList<QWidget*> panelPages() const;  // what it shows in the panels, in their order

    // ---- the canvas selection, as the Edit menu and the context menus act on it
    QStringList selection() const;  // the selected elements, each collapsed template whole
    bool hasSelectedItems() const;  // elements or links
    void selectAll();
    void copySelection() const;     // to the clipboard, as conf text
    void cutSelection();
    void paste();                   // the clipboard's conf text, selected once it is on the canvas
    void duplicateSelection();
    void deleteSelection();         // elements and links; a source alone leaves its neighbours as they are
    void alignSelection();

    QString title() const;  // its target's: the file's name, "Template: <name>", or "Untitled-N"; "(deleted)" after it
    QString path() const;   // the file it is, "" when it has none yet
    bool pristine() const;  // untitled, empty and unchanged: opening a file may use it instead of a new tab
    bool shows(const QString& sessionKey) const;  // it is bound to the target of that session key, or a file of it opens
    bool fileDeleted() const { return fileDeleted_; }
    // Its file was deleted on disk, or is back (DocumentRegistry). Deleted, it is modified: Save writes the file again.
    void setFileDeleted(bool deleted);

    // A file is opening in it: its tab already reads the file's name, and the view fits it once it is on the canvas.
    void opening(const QString& title) {
        fitNext_ = true;
        opening_ = title;
    }
    // The library has newer templates while another tab is in front: asked when this one comes to the front.
    void deferOutdated(const QStringList& ids) { outdated_ = ids; }
    QStringList takeOutdated() { return std::exchange(outdated_, {}); }
    // Its file changed on disk while it had unsaved work and another tab was in front: asked when it comes to the front.
    void deferChangedOnDisk(DiskChange why) { changedOnDisk_ = why; }
    std::optional<DiskChange> takeChangedOnDisk() { return std::exchange(changedOnDisk_, std::nullopt); }
    // Something waits for it to finish what it runs (MainWindow::whenIdle): one wait, however many ask.
    bool startWaiting() { return !std::exchange(waiting_, true); }
    void stopWaiting() { waiting_ = false; }

private:
    void showState();  // its canvas and pages follow whether it can be edited now
    void rebuilt();

    DocumentSession& session_;
    Controller* controller_;
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
    DocumentBackup* backup_;
    QStringList lastNames_;  // element names at the last rebuild: the view refits when they change
    bool fitNext_ = false;
    QString opening_;
    QStringList outdated_;
    std::optional<DiskChange> changedOnDisk_;
    bool fileDeleted_ = false;
    bool waiting_ = false;
};
