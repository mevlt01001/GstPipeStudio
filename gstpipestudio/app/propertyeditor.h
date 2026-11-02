// propertyeditor.h – the input widget for one property, chosen from its kind (SPEC 7 and 8).
// Used for element properties (PropertyDialog) and for sink pad properties (CapsDialog).
#pragma once

#include <QWidget>
#include <functional>
#include <optional>

#include "elementinfo.h"
#include "plan.h"

class PropertyEditor : public QWidget {
    Q_OBJECT
public:
    // current: the value the plan holds, none = show the default
    PropertyEditor(const PropertyInfo& info, const std::optional<QString>& current, QWidget* parent = nullptr);

    // Writable properties of a kind a text can set. "name" belongs to the Plan, not to the user.
    static bool editable(const PropertyInfo& info);
    // The plan keeps only what differs from the default: touched editors with a non-default text are written,
    // untouched ones keep what the plan had, and plan entries no editor knows about stay as they are.
    static gps::Props collect(const QList<PropertyEditor*>& editors, const gps::Props& original);

    const PropertyInfo& info() const { return info_; }
    bool touched() const { return touched_; }
    QString text() const { return read_(); }
    bool isDefault() const;

signals:
    void edited();

private:
    QWidget* build(const QString& initial);
    void touch();

    PropertyInfo info_;
    bool touched_ = false;
    std::function<QString()> read_;
};
