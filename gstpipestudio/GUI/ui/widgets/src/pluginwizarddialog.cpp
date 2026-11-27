#include "pluginwizarddialog.h"

#include "pluginlibrary.h"
#include "theme.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {

QLineEdit* line(const QString& value, const char* name = "") {
    auto* edit = new QLineEdit(value);
    edit->setObjectName(name);
    return edit;
}

QComboBox* choices(const QStringList& names, int current) {
    auto* box = new QComboBox;
    box->addItems(names);
    box->setCurrentIndex(current);
    return box;
}

QTableWidget* table(const QStringList& columns, const char* name) {
    auto* result = new QTableWidget(0, static_cast<int>(columns.size()));
    result->setObjectName(name);
    result->setHorizontalHeaderLabels(columns);
    result->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    result->horizontalHeader()->setStretchLastSection(true);
    result->verticalHeader()->hide();
    result->setSelectionBehavior(QAbstractItemView::SelectRows);
    result->setSelectionMode(QAbstractItemView::SingleSelection);
    return result;
}

template <class Widget>
Widget* cell(QTableWidget* table, int row, int column) {
    return qobject_cast<Widget*>(table->cellWidget(row, column));
}

QString text(QTableWidget* table, int row, int column) { return cell<QLineEdit>(table, row, column)->text(); }

template <class Enum>
Enum selection(QTableWidget* table, int row, int column) {
    return static_cast<Enum>(cell<QComboBox>(table, row, column)->currentIndex());
}

QStringList packages(const QString& value) {
    auto names = value.split(QRegularExpression("[,\\s]+"), Qt::SkipEmptyParts);
    names.removeDuplicates();
    return names;
}

enum PortColumn { PortName, Direction, Presence, Caps, CapsDetail, Mode, Default, Capacity, Overflow };
enum PropertyColumn { Name, Nick, Blurb, Type, Initial, Min, Max, Mutability, Controllable };

}  // namespace

PluginWizardDialog::PluginWizardDialog(PluginLibrary& library, std::optional<PluginSpec> existing, QWidget* parent)
    : QDialog(parent), library_(library), editing_(existing.has_value()) {
    setObjectName("pluginWizard");
    setWindowTitle(editing_ ? "Edit Plugin Specification" : "New Custom Plugin");
    resize(1080, 510);
    auto initial = existing.value_or(PluginSpec{});
    if (editing_) initial.archetype = initial.resolvedArchetype();
    auto* tabs = new QTabWidget;
    tabs->addTab(basics(initial), "Details");
    tabs->addTab(portPage(initial), "Ports");
    tabs->addTab(propertyPage(initial), "Properties");
    tabs->addTab(dependencyPage(initial), "Dependencies");
    problem_ = new QLabel;
    problem_->setObjectName("pluginSpecError");
    problem_->setWordWrap(true);
    Theme::setStyle(problem_, "color: @error@;");
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel);
    save_ = buttons->addButton(editing_ ? "Save Specification" : "Create && Build", QDialogButtonBox::AcceptRole);
    save_->setObjectName("pluginCreateBuild");
    connect(save_, &QPushButton::clicked, this, &QDialog::accept);
    if (!editing_) {
        edit_ = buttons->addButton("Create && Edit Code", QDialogButtonBox::AcceptRole);
        edit_->setObjectName("pluginCreateEdit");
        connect(edit_, &QPushButton::clicked, this, [this] { opensEditor_ = true; accept(); });
    }
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(tabs, 1);
    layout->addWidget(problem_);
    layout->addWidget(buttons);
    connect(&library_, &PluginLibrary::changed, this, &PluginWizardDialog::refresh);
    refresh();
}

QWidget* PluginWizardDialog::basics(const PluginSpec& spec) {
    auto* page = new QWidget;
    auto* form = new QFormLayout(page);
    id_ = line(spec.id, "pluginId");
    id_->setPlaceholderText("reticle_overlay");
    id_->setReadOnly(editing_);
    name_ = line(spec.longName, "pluginDisplayName");
    description_ = line(spec.description);
    author_ = line(spec.author);
    version_ = line(spec.version);
    license_ = line(spec.license);
    category_ = choices({"", "Filter/Effect/Video", "Filter/Effect/Audio", "Source", "Sink"}, 0);
    category_->setEditable(true);
    category_->setEditText(spec.klass);
    archetype_ = choices(pluginArchetypeNames(), static_cast<int>(spec.archetype));
    archetype_->setObjectName("pluginArchetype");
    archetype_->setItemText(0, "Auto (Recommended)");
    archetype_->setEnabled(!editing_);
    resolved_ = new QLabel;
    resolved_->setObjectName("pluginResolvedArchetype");
    live_ = new QCheckBox("Live source");
    live_->setChecked(spec.live);
    clockSync_ = new QCheckBox("Synchronize sink to the pipeline clock");
    clockSync_->setChecked(spec.clockSync);
    protocol_ = line(spec.uriProtocol);
    protocol_->setPlaceholderText("Optional URI scheme");
    form->addRow("Element ID", id_);
    form->addRow("Display name", name_);
    form->addRow("Category", category_);
    form->addRow("Description", description_);
    form->addRow("Author", author_);
    form->addRow("Version", version_);
    form->addRow("License", license_);
    form->addRow("Archetype", archetype_);
    form->addRow(QString(), resolved_);
    form->addRow(QString(), live_);
    form->addRow("URI scheme", protocol_);
    form->addRow(QString(), clockSync_);
    for (auto* edit : {id_, name_, description_, author_, version_, license_, protocol_})
        connect(edit, &QLineEdit::textChanged, this, &PluginWizardDialog::refresh);
    connect(archetype_, &QComboBox::currentIndexChanged, this, &PluginWizardDialog::refresh);
    return page;
}

QWidget* PluginWizardDialog::portPage(const PluginSpec& spec) {
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    ports_ = table({"Name", "Direction", "Presence", "Caps preset", "Caps details", "Input mode",
                    "Mailbox default", "Queue size", "Overflow"}, "pluginPorts");
    ports_->setColumnWidth(CapsDetail, 170);
    layout->addWidget(ports_, 1);
    auto* buttons = new QHBoxLayout;
    for (const auto direction : {PluginDirection::Sink, PluginDirection::Src}) {
        const auto stem = direction == PluginDirection::Sink ? QString("sink") : QString("src");
        auto* add = new QPushButton("Add " + stem);
        add->setObjectName(direction == PluginDirection::Sink ? "pluginAddSink" : "pluginAddSrc");
        buttons->addWidget(add);
        connect(add, &QPushButton::clicked, this, [this, direction, stem] {
            QString candidate = stem;
            auto used = [&](const QString& wanted) {
                for (int row = 0; row < ports_->rowCount(); ++row)
                    if (text(ports_, row, PortName) == wanted) return true;
                return false;
            };
            for (int suffix = 1; used(candidate); ++suffix) candidate = stem + "_" + QString::number(suffix);
            addPort({candidate, direction, PluginPresence::Always, PluginCaps::VideoRaw, "RGBA"});
            refresh();
        });
    }
    auto* remove = new QPushButton("Remove selected");
    buttons->addWidget(remove);
    buttons->addStretch();
    connect(remove, &QPushButton::clicked, this, [this] {
        if (ports_->currentRow() >= 0) ports_->removeRow(ports_->currentRow());
        refresh();
    });
    layout->addLayout(buttons);
    auto* queueOptions = new QFormLayout;
    coordination_ = choices(pluginCoordinationNames(), static_cast<int>(spec.coordination));
    tolerance_ = new QSpinBox;
    tolerance_->setRange(0, 60000);
    tolerance_->setSuffix(" ms");
    tolerance_->setValue(spec.toleranceMs);
    queueOptions->addRow("Queue coordination", coordination_);
    queueOptions->addRow("PTS tolerance", tolerance_);
    layout->addLayout(queueOptions);
    connect(coordination_, &QComboBox::currentIndexChanged, this, &PluginWizardDialog::refresh);
    for (const auto& port : spec.ports) addPort(port);
    return page;
}

void PluginWizardDialog::addPort(PluginPortSpec port) {
    const int row = ports_->rowCount();
    ports_->insertRow(row);
    ports_->setCellWidget(row, PortName, line(port.name));
    ports_->setCellWidget(row, Direction, choices({"Sink", "Src"}, static_cast<int>(port.direction)));
    ports_->setCellWidget(row, Presence, choices(pluginPresenceNames(), static_cast<int>(port.presence)));
    ports_->setCellWidget(row, Caps, choices(pluginCapsNames(), static_cast<int>(port.capsPreset)));
    auto* details = line(port.capsDetail);
    details->setToolTip("Comma-separated formats, comma-prefixed caps fields, or full caps for CustomMime");
    ports_->setCellWidget(row, CapsDetail, details);
    ports_->setCellWidget(row, Mode, choices(pluginInputNames(), static_cast<int>(port.inputMode)));
    ports_->setCellWidget(row, Default, line(port.mailboxDefault));
    auto* capacity = new QSpinBox;
    capacity->setRange(1, 1000000);
    capacity->setValue(port.queueCapacity);
    ports_->setCellWidget(row, Capacity, capacity);
    ports_->setCellWidget(row, Overflow, choices(pluginOverflowNames(), static_cast<int>(port.overflowPolicy)));
    for (int column : {PortName, CapsDetail, Default})
        connect(cell<QLineEdit>(ports_, row, column), &QLineEdit::textChanged, this, &PluginWizardDialog::refresh);
    for (int column : {Direction, Presence, Caps, Mode, Overflow})
        connect(cell<QComboBox>(ports_, row, column), &QComboBox::currentIndexChanged, this, &PluginWizardDialog::refresh);
}

QWidget* PluginWizardDialog::propertyPage(const PluginSpec& spec) {
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    properties_ = table({"Name", "Nick", "Description", "Type", "Default", "Min", "Max", "Mutability", "Controllable"}, "pluginProperties");
    layout->addWidget(properties_, 1);
    auto* buttons = new QHBoxLayout;
    auto* add = new QPushButton("Add property");
    add->setObjectName("pluginAddProperty");
    auto* remove = new QPushButton("Remove selected");
    buttons->addWidget(add);
    buttons->addWidget(remove);
    buttons->addStretch();
    layout->addLayout(buttons);
    connect(add, &QPushButton::clicked, this, [this] {
        addProperty({});
        refresh();
        cell<QLineEdit>(properties_, properties_->rowCount() - 1, Name)->setFocus();
    });
    connect(remove, &QPushButton::clicked, this, [this] {
        if (properties_->currentRow() >= 0) properties_->removeRow(properties_->currentRow());
        refresh();
    });
    for (const auto& p : spec.properties) addProperty(p);
    return page;
}

void PluginWizardDialog::addProperty(const PluginPropertySpec& p) {
    const int row = properties_->rowCount();
    properties_->insertRow(row);
    const QMap<int, QString> texts{{Name, p.name}, {Nick, p.nick}, {Blurb, p.blurb}, {Initial, p.defaultValue},
                                   {Min, p.minValue}, {Max, p.maxValue}};
    for (auto it = texts.constBegin(); it != texts.constEnd(); ++it) {
        auto* edit = line(it.value());
        properties_->setCellWidget(row, it.key(), edit);
        connect(edit, &QLineEdit::textChanged, this, &PluginWizardDialog::refresh);
    }
    auto* type = choices(pluginPropertyTypeNames(), static_cast<int>(p.type));
    properties_->setCellWidget(row, Type, type);
    connect(type, &QComboBox::currentIndexChanged, this, &PluginWizardDialog::refresh);
    properties_->setCellWidget(row, Mutability, choices(pluginMutabilityNames(), static_cast<int>(p.mutability)));
    auto* controllable = new QCheckBox;
    controllable->setChecked(p.controllable);
    properties_->setCellWidget(row, Controllable, controllable);
}

QWidget* PluginWizardDialog::dependencyPage(const PluginSpec& spec) {
    auto* page = new QWidget;
    auto* form = new QFormLayout(page);
    auto extra = spec.extraPkgConfig;
    opencv_ = new QCheckBox("OpenCV (opencv4)");
    opencv_->setChecked(extra.removeAll("opencv4") > 0);
    pkgConfig_ = line(extra.join(", "));
    findPackages_ = line(spec.extraFindPackages.join(", "));
    form->addRow(QString(), opencv_);
    form->addRow("Other pkg-config modules", pkgConfig_);
    form->addRow("CMake packages", findPackages_);
    connect(pkgConfig_, &QLineEdit::textChanged, this, &PluginWizardDialog::refresh);
    connect(findPackages_, &QLineEdit::textChanged, this, &PluginWizardDialog::refresh);
    return page;
}

PluginSpec PluginWizardDialog::spec() const {
    PluginSpec result;
    result.id = id_->text();
    result.longName = name_->text().trimmed();
    result.description = description_->text();
    result.klass = category_->currentText().trimmed();
    result.author = author_->text();
    result.version = version_->text();
    result.license = license_->text();
    result.archetype = static_cast<PluginArchetype>(archetype_->currentIndex());
    result.coordination = static_cast<PluginCoordination>(coordination_->currentIndex());
    result.toleranceMs = tolerance_->value();
    result.extraPkgConfig = packages(pkgConfig_->text());
    if (opencv_->isChecked() && !result.extraPkgConfig.contains("opencv4")) result.extraPkgConfig << "opencv4";
    result.extraFindPackages = packages(findPackages_->text());
    result.ports.clear();
    for (int row = 0; row < ports_->rowCount(); ++row) {
        PluginPortSpec p;
        p.name = text(ports_, row, PortName);
        p.direction = selection<PluginDirection>(ports_, row, Direction);
        p.presence = selection<PluginPresence>(ports_, row, Presence);
        p.capsPreset = selection<PluginCaps>(ports_, row, Caps);
        p.capsDetail = text(ports_, row, CapsDetail);
        if (p.direction == PluginDirection::Sink) {
            p.inputMode = selection<PluginInput>(ports_, row, Mode);
            p.mailboxDefault = text(ports_, row, Default);
            p.queueCapacity = cell<QSpinBox>(ports_, row, Capacity)->value();
            p.overflowPolicy = selection<PluginOverflow>(ports_, row, Overflow);
        }
        result.ports << p;
    }
    for (int row = 0; row < properties_->rowCount(); ++row) {
        result.properties << PluginPropertySpec{text(properties_, row, Name), text(properties_, row, Nick), text(properties_, row, Blurb),
            selection<PluginPropertyType>(properties_, row, Type), text(properties_, row, Initial), text(properties_, row, Min),
            text(properties_, row, Max), selection<PluginMutability>(properties_, row, Mutability),
            cell<QCheckBox>(properties_, row, Controllable)->isChecked()};
    }
    const auto resolved = result.resolvedArchetype();
    result.live = resolved == PluginArchetype::Source && live_->isChecked();
    result.clockSync = resolved == PluginArchetype::Sink && clockSync_->isChecked();
    if (resolved == PluginArchetype::Source) result.uriProtocol = protocol_->text();
    return result;
}

void PluginWizardDialog::refresh() {
    const auto value = spec();
    const auto resolved = value.resolvedArchetype();
    resolved_->setText("C++ archetype: " + pluginArchetypeNames().at(static_cast<int>(resolved)));
    live_->setEnabled(resolved == PluginArchetype::Source);
    protocol_->setEnabled(resolved == PluginArchetype::Source);
    clockSync_->setEnabled(resolved == PluginArchetype::Sink);
    tolerance_->setEnabled(value.coordination == PluginCoordination::PtsWindow);
    for (int row = 0; row < value.ports.size(); ++row) {
        const auto& p = value.ports[row];
        const bool sink = p.direction == PluginDirection::Sink;
        ports_->cellWidget(row, Mode)->setEnabled(sink);
        ports_->cellWidget(row, Default)->setEnabled(sink && p.inputMode == PluginInput::Mailbox);
        ports_->cellWidget(row, Capacity)->setEnabled(sink && p.inputMode == PluginInput::BoundedQueue);
        ports_->cellWidget(row, Overflow)->setEnabled(sink && p.inputMode == PluginInput::BoundedQueue);
    }
    QString error = editing_ ? QString() : library_.newIdError(value.id);
    if (error.isEmpty()) error = value.error();
    problem_->setText(error);
    save_->setEnabled(error.isEmpty());
    if (edit_) edit_->setEnabled(error.isEmpty());
}
