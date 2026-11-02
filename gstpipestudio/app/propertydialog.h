// propertydialog.h – the form generated from an element's properties (SPEC 8, step 2).
#pragma once

#include <QDialog>
#include <map>

#include "elementedit.h"
#include "elementinfo.h"
#include "propertyeditor.h"

class QLineEdit;
class QSpinBox;
class QCheckBox;
class QWidget;

struct PropertyDialogInput {
    ElementInfo info;
    QString title;                            // "Kaynak ekle", "Property düzenle: x"
    gps::Props props;                         // the element's changed properties so far
    std::map<std::string, int> requestSrc;    // Request src pad counts to start from
    std::map<std::string, int> linkedSrc;     // pads already linked per template: the counts cannot go below
    std::vector<std::string> exposed;         // properties already exposed
};

class PropertyDialog : public QDialog {
    Q_OBJECT
public:
    PropertyDialog(const PropertyDialogInput& input, QWidget* parent = nullptr);
    ElementEdit edit() const;  // valid after the dialog was accepted

private:
    void filter(const QString& text);

    PropertyDialogInput input_;
    QList<PropertyEditor*> editors_;
    QMap<QString, QCheckBox*> expose_;
    QMap<QString, QWidget*> rows_;
    QMap<QString, QSpinBox*> pads_;
};
