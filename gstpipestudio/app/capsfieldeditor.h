// capsfieldeditor.h – one caps field in the CapsDialog: "Otomatik" until the user pins a value (SPEC 8, step 4).
//   list  -> a combo box with Otomatik first
//   range -> a number box, active when "Sabitle" is ticked
//   fixed -> the value as a label with "Sabitle"
#pragma once

#include <QWidget>
#include <functional>
#include <optional>

#include "elementinfo.h"

class QCheckBox;
class QComboBox;

class CapsFieldEditor : public QWidget {
    Q_OBJECT
public:
    CapsFieldEditor(const FieldInfo& field, QWidget* parent = nullptr);

    const FieldInfo& field() const { return field_; }
    std::optional<QString> pinned() const;  // none = Otomatik
    void pin(const QString& value);

private:
    FieldInfo field_;
    QComboBox* combo_ = nullptr;   // list
    QCheckBox* pin_ = nullptr;     // range and fixed
    std::function<QString()> read_;
    std::function<void(const QString&)> write_;
};
