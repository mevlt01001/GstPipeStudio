// propertyeditor.h – the input widget for one property, chosen from its kind (SPEC 7 and 8).
// Used for element properties (properties page) and for sink pad properties (caps page).
#pragma once

#include <QToolButton>
#include <QWidget>
#include <functional>
#include <optional>

#include "contract.h"
#include "graph.h"

class PropertyEditor : public QWidget {
    Q_OBJECT
public:
    // current: the value the plan holds, none = show the default
    PropertyEditor(const PropertyInfo& info, const std::optional<QString>& current, QWidget* parent = nullptr);

    // Writable properties of a kind a text can set.
    static bool editable(const PropertyInfo& info);
    // The plan keeps only what differs from the default: touched editors with a non-default text are written,
    // untouched ones keep what the plan had, and plan entries no editor knows about stay as they are.
    static gps::Props collect(const QList<PropertyEditor*>& editors, const gps::Props& original);

    const PropertyInfo& info() const { return info_; }
    bool touched() const { return touched_; }
    QString text() const { return read_(); }
    bool isDefault() const;
    void resetToDefault();

signals:
    void edited();

private:
    struct Style;
    static const Style* supportedStyle(const std::string& kind);
    static const Style& styleFor(const std::string& kind);

    QWidget* buildBoolean(const QString& initial);
    QWidget* buildInt(const QString& initial);
    QWidget* buildNumber(const QString& initial, double minimum, double maximum, int decimals, double step);
    QWidget* buildEnum(const QString& initial);
    QWidget* buildFlags(const QString& initial);
    QWidget* buildText(const QString& initial, const QString& pattern = {}, const QString& hint = {});
    QString rangeHint() const;
    void touch();
    void refresh();  // the ↺ shows while the value is not the default

    PropertyInfo info_;
    const Style& style_;
    bool touched_ = false;
    std::function<QString()> read_;
    QToolButton* reset_ = nullptr;
};
