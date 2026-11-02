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
};

struct PickerChoice {
    enum Kind { None, Existing, NewElement };
    Kind kind = None;
    ExistingTarget existing;
    QString factory;
};

class QLineEdit;
class QListWidget;
class QListWidgetItem;

class PickerPage : public DrawerPage {
    Q_OBJECT
public:
    explicit PickerPage(const PickerData& data, QWidget* parent = nullptr);
    PickerChoice choice() const;  // the selected row

    void focusFirst() override;
    void focusSearch() override;

signals:
    void picked(const PickerChoice& choice);  // Choose, double click or Enter

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void filter(const QString& text);
    void choose();

    PickerData data_;
    QLineEdit* search_;
    QListWidget* list_;
};
