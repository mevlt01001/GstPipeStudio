// flows.h – UI flows: the right drawer is exclusively for picking elements (PickerPage), while element Properties
// (PropertyPage) and link Caps / Pad settings (CapsPage) live in the bottom dock tabs.
#pragma once

#include <QObject>
#include <QPoint>
#include <functional>

#include "controller.h"

class CapsPage;
class Drawer;
class PropertyPage;
class QDialog;
class QTabWidget;
class QWidget;

class Flows : public QObject {
    Q_OBJECT
public:
    Flows(Controller* controller, Drawer* drawer, QWidget* window);

    void setBottomPanels(QTabWidget* tabs, QWidget* propsTab, QWidget* capsTab, QWidget* defaultTab);
    void setBusy(bool busy);

    PropertyPage* propertyPage() const;
    CapsPage* capsPage() const;
    QString propertiesTitle() const { return propsTitle_; }
    QString capsTitle() const { return capsTitle_; }

    void closeProperties();
    void closeCaps();

    // Add source / any element (opens PickerPage in the right drawer)
    void chooseSource();
    void chooseElement();
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

private:
    enum class Mode { Add, Replace };

    void presentProperties(PropertyPage* page, const QString& title);
    void presentCaps(CapsPage* page, const QString& title, const QString& subtitle);
    void confirmRemoval(const gps::Removal& removal, const QString& verb, std::function<void()> accepted);
    void sourceProperties(const QString& factory);
    void newElement(Mode mode, const QString& element, const QString& identity, const QString& factory);
    // Runs an edit; when the validation succeeds the bottom Caps tab opens for the link from (element, identity).
    void editThenCaps(const QString& element, const QString& identity, std::function<bool()> edit);
    void openCaps(const QString& element, const QString& identity, bool afterEdit);
    void abandonPending();

    Controller* controller_;
    Drawer* drawer_;
    QWidget* window_;
    QTabWidget* tabs_ = nullptr;
    QWidget* propsTab_ = nullptr;
    QWidget* capsTab_ = nullptr;
    QWidget* defaultTab_ = nullptr;
    QString propsTitle_;
    QString capsTitle_;
    QMetaObject::Connection pending_;  // waits for the validation that decides whether the caps tab opens
};
