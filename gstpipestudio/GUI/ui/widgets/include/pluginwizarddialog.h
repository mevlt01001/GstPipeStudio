#pragma once

#include <QDialog>
#include <optional>

#include "pluginspec.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;
class QSpinBox;
class PluginLibrary;

class PluginWizardDialog : public QDialog {
    Q_OBJECT
public:
    PluginWizardDialog(PluginLibrary& library, std::optional<PluginSpec> existing, QWidget* parent);
    PluginSpec spec() const;
    bool opensEditor() const { return opensEditor_; }

private:
    QWidget* basics(const PluginSpec& spec);
    QWidget* portPage(const PluginSpec& spec);
    QWidget* propertyPage(const PluginSpec& spec);
    QWidget* dependencyPage(const PluginSpec& spec);
    void addPort(PluginPortSpec port);
    void addProperty(const PluginPropertySpec& property);
    void refresh();

    PluginLibrary& library_;
    bool editing_;
    bool opensEditor_ = false;
    QLineEdit *id_, *name_, *description_, *author_, *version_, *license_, *protocol_, *pkgConfig_, *findPackages_;
    QComboBox *category_, *archetype_, *coordination_;
    QSpinBox* tolerance_;
    QCheckBox *live_, *clockSync_, *opencv_;
    QTableWidget *ports_, *properties_;
    QLabel *resolved_, *problem_;
    QPushButton *save_, *edit_ = nullptr;
};
