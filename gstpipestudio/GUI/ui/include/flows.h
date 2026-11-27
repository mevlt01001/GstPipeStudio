// flows.h – UI flows: the right drawer is exclusively for picking elements (PickerPage), while element Properties
// (PropertyPage) and link Caps / Pad settings (CapsPage) live in the bottom dock tabs.
#pragma once

#include <QObject>
#include <QPoint>
#include <functional>

#include "controller.h"
#include "panelslot.h"

class CapsPage;
class Drawer;
class DrawerPage;
class PropertyPage;
class QDialog;
class Panels;
class QWidget;

class Flows : public QObject {
    Q_OBJECT
public:
    Flows(Controller* controller, Drawer* drawer, QWidget* window);

    void setBottomPanels(Panels* panels, QWidget* propsTab, QWidget* capsTab, QWidget* defaultTab);
    void setBusy(bool busy);

    PropertyPage* propertyPage() const;
    CapsPage* capsPage() const;
    QString propertiesTitle() const { return props_.title(); }
    QString capsTitle() const { return caps_.title(); }

    void closeProperties();
    void closeCaps();

    // Any element: a source, a filter or a sink (opens PickerPage in the right drawer)
    void chooseElement();
    // A component to place: the templates, later the plugins (a list in the right drawer; hovering one shows a peek).
    // Its right click and Import are the window's file operations; the list follows the providers' changes.
    void chooseComponent(const QString& providerKind = {});
    void editTemplateParameters();  // of the library template being edited on the canvas
    // Left click on a free src pad (opens PickerPage in the right drawer)
    void fromFreePad(const QString& element, const QString& identity);
    // A link's caps, the element behind a pad replaced, a queue inserted (the link menu)
    void editCaps(const QString& element, const QString& identity);
    void replaceBehind(const QString& element, const QString& identity);
    void addQueue(const QString& element, const QString& identity);
    void editProperties(const QString& element);
    // Save as template: the selection becomes a template at once (its border is drawn); the Properties tab asks for its
    // name and parameters. Cancel, or leaving the page unsaved, makes the elements plain again.
    void saveAsTemplate(const QStringList& elements);
    // A placed template's Properties tab: its parameters only, as if it were one element.
    void editTemplateParams(const QString& instanceId);

signals:
    void componentMenuRequested(const ComponentRef& component, const QPoint& at);  // right click on a component
    void importRequested(const QString& providerKind);                             // Import… under the list

private:
    // What the user reads: "<element>.<pad>" (Names::pad) for a src pad or any pad, and an element's own name.
    QString path(const QString& element, const QString& identity) const;
    QString path(const gps::PadRef& pad) const;
    QString title(const QString& element) const;
    enum class Mode { Add, Replace };

    void presentProperties(DrawerPage* page, const QString& title);  // the one way a page enters the Properties tab
    void presentCaps(CapsPage* page, const QString& title, const QString& subtitle);
    void showElementProperties(const QString& element, const std::optional<ElementEdit>& pending);
    void newElementProperties(const QString& factory);
    void newElement(Mode mode, const QString& element, const QString& identity, const QString& factory);
    // Runs an edit; when the validation succeeds the bottom Caps tab opens for the link from (element, identity).
    void editThenCaps(const QString& element, const QString& identity, std::function<bool()> edit);
    void openCaps(const QString& element, const QString& identity, bool afterEdit);
    void abandonPending();
    // The catalog's sources (true) or everything that is not a source (false), for a picker.
    std::vector<FactoryEntry> factories(bool sources) const;
    FactoryCatalog& catalog() const { return controller_->services().catalog(); }
    QList<const ComponentProvider*> providers() const;

    Controller* controller_;
    Drawer* drawer_;
    QWidget* window_;
    PanelSlot props_{"propsTab", "Click an element on the canvas to inspect or edit its Properties."};
    PanelSlot caps_{"capsTab", "Click a link on the canvas to inspect or edit its Caps."};
    QMetaObject::Connection pending_;  // waits for the validation that decides whether the caps tab opens
};
