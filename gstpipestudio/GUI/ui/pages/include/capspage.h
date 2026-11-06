// capspage.h – drawer page 3 (UI_RULES 2): the caps choice of one link plus the settings of its sink pad.
#pragma once

#include "capsparse.h"
#include "drawer.h"
#include "elementinfo.h"
#include "graph.h"
#include "propertyeditor.h"

class QComboBox;
class QStackedWidget;
class CapsFieldEditor;

struct CapsPageInput {
    QList<StructureInfo> allowed;            // what the src pad can produce, from the last update
    QString current;                         // the link's caps text now, "" = Auto
    QList<PropertyInfo> padProperties;       // of the sink pad this link ends on
    gps::Props padProps;                     // their current values
    bool skippable = false;                  // right after an element was added: Skip keeps Auto caps
};

class CapsPage : public DrawerPage {
    Q_OBJECT
public:
    explicit CapsPage(const CapsPageInput& input, QWidget* parent = nullptr);
    QString caps() const;        // "" when the media type is Auto
    gps::Props padProps() const;

    void focusFirst() override;

signals:
    void applied();  // Apply or Enter
    void skipped();

private:
    CapsPageInput input_;
    QComboBox* media_ = nullptr;
    QStackedWidget* pages_ = nullptr;
    QList<QList<CapsFieldEditor*>> fields_;  // per structure
    QList<PropertyEditor*> padEditors_;
};
