// flows.h – the sequences of SPEC 8 as drawer pages: picker -> properties (with the sink pad) -> confirmation ->
// validation -> caps. Cancel on any page leaves the Plan exactly as it was; the Controller is only called when the
// last page before the edit says Add / Replace / Save. The delete confirmation is the one modal dialog.
#pragma once

#include <QObject>
#include <QPoint>
#include <functional>

#include "controller.h"

class Drawer;
class QDialog;
class QWidget;

class Flows : public QObject {
    Q_OBJECT
public:
    Flows(Controller* controller, Drawer* drawer, QWidget* window);

    // Add source
    void chooseSource();
    // Left click on a free src pad
    void fromFreePad(const QString& element, const QString& identity);
    // Right click on a linked pad or a link
    void showLinkMenu(const QString& element, const QString& identity, const QPoint& at);
    void editCaps(const QString& element, const QString& identity);
    void replaceBehind(const QString& element, const QString& identity);
    void cutLink(const QString& element, const QString& identity);
    void addQueue(const QString& element, const QString& identity);
    // Element menu
    void showElementMenu(const QString& element, const QPoint& at);
    void editProperties(const QString& element);
    void deleteSource(const QString& element);

private:
    enum class Mode { Add, Replace };

    void confirmRemoval(const gps::Removal& removal, const QString& verb, std::function<void()> accepted);
    void sourceProperties(const QString& factory);
    void newElement(Mode mode, const QString& element, const QString& identity, const QString& factory);
    // Runs an edit; when the validation succeeds the drawer moves on to the caps page of the link from (element, identity).
    void editThenCaps(const QString& element, const QString& identity, std::function<bool()> edit);
    void openCaps(const QString& element, const QString& identity, bool afterEdit);
    void abandonPending();

    Controller* controller_;
    Drawer* drawer_;
    QWidget* window_;
    QMetaObject::Connection pending_;  // waits for the validation that decides whether the caps page opens
};
