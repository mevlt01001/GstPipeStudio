// pickerdialog.h – the Seçici (SPEC 8): what can follow a free src pad. Existing elements first, then new ones.
#pragma once

#include <QDialog>

#include "controller.h"

struct PickerData {
    QString title, subtitle;
    QString newHeader = "Yeni element";
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

class PickerDialog : public QDialog {
    Q_OBJECT
public:
    PickerDialog(const PickerData& data, QWidget* parent = nullptr);
    PickerChoice choice() const;  // valid after accept

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void filter(const QString& text);

    PickerData data_;
    QLineEdit* search_;
    QListWidget* list_;
};
