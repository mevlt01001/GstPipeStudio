// flows.h – UI flows: the right drawer is exclusively for picking elements (PickerPage), while element Properties
// (PropertyPage) and link Caps / Pad settings (CapsPage) live in the bottom dock tabs.
#pragma once

#include <QObject>
#include <QPoint>
#include <functional>

#include "controller.h"

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
    QString propertiesTitle() const { return propsTitle_; }
    QString capsTitle() const { return capsTitle_; }

    void closeProperties();
    void closeCaps();

    // Any element: a source, a filter or a sink (opens PickerPage in the right drawer)
    void chooseElement();
    // A component to place: the templates, later the plugins (a list in the right drawer; hovering one shows a peek)
    void chooseComponent();
    void componentMenu(const ComponentRef& component, const QPoint& at);  // right click on a component in a list
    void editTemplateParameters();  // of the library template being edited on the canvas
    // Left click on a free src pad (opens PickerPage in the right drawer)
    void fromFreePad(const QString& element, const QString& identity);
    // Right click on a linked pad or a link
    void showLinkMenu(const QString& element, const QString& identity, const QPoint& at);
    void editCaps(const QString& element, const QString& identity);
    void replaceBehind(const QString& element, const QString& identity);
    void cutLink(const QString& element, const QString& identity);
    void disconnectLinkOnly(const QString& element, const QString& identity);
    void addQueue(const QString& element, const QString& identity);
    // Element menu
    void showElementMenu(const QString& element, const QPoint& at);
    void editProperties(const QString& element);
    void deleteSource(const QString& element);
    void deleteElementOnly(const QString& element);
    // Save as template: the selection becomes a template at once (its border is drawn); the Properties tab asks for its
    // name and parameters. Cancel, or leaving the page unsaved, makes the elements plain again.
    void saveAsTemplate(const QStringList& elements);
    // A placed template's Properties tab: its parameters only, as if it were one element.
    void editTemplateParams(const QString& instanceId);

signals:
    void editComponentRequested(const ComponentRef& component);  // the window opens it in a window of its own

private:
    // What the user reads: "<element>.<pad>" (Names::pad) for a src pad or any pad, and an element's own name.
    QString path(const QString& element, const QString& identity) const;
    QString path(const gps::PadRef& pad) const;
    QString title(const QString& element) const;
    enum class Mode { Add, Replace };

    void presentProperties(DrawerPage* page, const QString& title);  // the one way a page enters the Properties tab
    void presentCaps(CapsPage* page, const QString& title, const QString& subtitle);
    void confirmRemoval(const gps::Removal& removal, const QString& verb, std::function<void()> accepted);
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
    void importInto(ComponentProvider& provider);

    Controller* controller_;
    Drawer* drawer_;
    QWidget* window_;
    Panels* panels_ = nullptr;
    QWidget* propsTab_ = nullptr;
    QWidget* capsTab_ = nullptr;
    QWidget* defaultTab_ = nullptr;
    QString propsTitle_;
    QString capsTitle_;
    QMetaObject::Connection pending_;  // waits for the validation that decides whether the caps tab opens
};
