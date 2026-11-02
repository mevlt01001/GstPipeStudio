// propertypage.h – drawer page 2 (UI_RULES 2): the form generated from an element's properties, the Request src pad
// counts and, when the element can attach to several sink pads, the choice of the sink pad.
#pragma once

#include <map>

#include "drawer.h"
#include "elementedit.h"
#include "elementinfo.h"
#include "propertyeditor.h"

class QCheckBox;
class QComboBox;
class QLineEdit;
class QSpinBox;

struct PropertyPageInput {
    ElementInfo info;
    QString acceptText = "Add element";       // "Add element", "Replace", "Save"
    gps::Props props;                         // the element's changed properties so far
    std::map<std::string, int> requestSrc;    // Request src pad counts to start from
    std::map<std::string, int> linkedSrc;     // pads already linked per template: the counts cannot go below
    std::vector<std::string> exposed;         // properties already exposed
    QStringList sinkPads;                     // more than one: the user chooses where the element attaches
};

class PropertyPage : public DrawerPage {
    Q_OBJECT
public:
    explicit PropertyPage(const PropertyPageInput& input, QWidget* parent = nullptr);
    ElementEdit edit() const;
    QString sinkPad() const;  // "" when there was nothing to choose

    void focusFirst() override;

signals:
    void confirmed();  // the accept button or Enter

private:
    void filter(const QString& text);

    PropertyPageInput input_;
    QLineEdit* search_;
    QList<PropertyEditor*> editors_;
    QMap<QString, QCheckBox*> expose_;
    QMap<QString, QWidget*> rows_;
    QMap<QString, QSpinBox*> pads_;
    QComboBox* sinkPads_ = nullptr;
};
