// pickerpage.h – drawer page 1 (UI_RULES 2): what can follow a free src pad, or the sources to start with.
// Existing elements first, then new ones, compatible ones ranked on top.
#pragma once

#include "controller.h"
#include "drawer.h"

struct PickerData {
    QString newHeader = "New element";
    QList<ExistingTarget> existing;   // free Always sink pads and Request sink templates of elements in the plan
    QStringList compatible;           // factories whose sink meets the pad, ranked
    QList<ListEntry> candidates;      // every factory that may be offered
    bool showFilters = false;         // role and pad presence filter chips
};

struct PickerChoice {
    enum Kind { None, Existing, NewElement };
    Kind kind = None;
    ExistingTarget existing;
    QString factory;
};

class QButtonGroup;
class QLineEdit;
class QListWidget;
class QListWidgetItem;

class PickerPage : public DrawerPage {
    Q_OBJECT
public:
    enum RoleFilter { RoleAll = 0, RoleSrc, RoleFilterElem, RoleSink };
    enum PresenceFilter { PresenceAll = 0, PresenceAlways, PresenceSometimes, PresenceRequest };

    explicit PickerPage(const PickerData& data, QWidget* parent = nullptr);
    PickerChoice choice() const;  // the selected row

    void setRoleFilter(RoleFilter role);
    RoleFilter roleFilter() const { return roleFilter_; }
    void setPresenceFilter(PresenceFilter presence);
    PresenceFilter presenceFilter() const { return presenceFilter_; }

    void focusFirst() override;
    void focusSearch() override;

signals:
    void picked(const PickerChoice& choice);  // Choose, double click or Enter
    void previewRequested(const QString& factory);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void filter(const QString& text);
    void choose();

    PickerData data_;
    QLineEdit* search_;
    QListWidget* list_;
    RoleFilter roleFilter_ = RoleAll;
    PresenceFilter presenceFilter_ = PresenceAll;
};
