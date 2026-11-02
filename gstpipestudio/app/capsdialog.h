// capsdialog.h – the caps choice of one link plus the settings of its sink pad (SPEC 8, step 4).
#pragma once

#include <QDialog>

#include "capsparse.h"
#include "elementinfo.h"
#include "plan.h"
#include "propertyeditor.h"

class QComboBox;
class QGroupBox;
class QStackedWidget;
class CapsFieldEditor;

struct CapsDialogInput {
    QString title;
    QString subtitle;                        // "videotestsrc_0.src → videoconvert_1.sink"
    QList<StructureInfo> allowed;            // what the src pad can produce, from the last update
    QString current;                         // the link's caps text now, "" = Otomatik
    QList<PropertyInfo> padProperties;       // of the sink pad this link ends on
    gps::Props padProps;                     // their current values
};

class CapsDialog : public QDialog {
    Q_OBJECT
public:
    CapsDialog(const CapsDialogInput& input, QWidget* parent = nullptr);
    QString caps() const;        // "" when the media type is Otomatik
    gps::Props padProps() const;

private:
    CapsDialogInput input_;
    QComboBox* media_ = nullptr;
    QStackedWidget* pages_ = nullptr;
    QList<QList<CapsFieldEditor*>> fields_;  // per structure
    QList<PropertyEditor*> padEditors_;
};
