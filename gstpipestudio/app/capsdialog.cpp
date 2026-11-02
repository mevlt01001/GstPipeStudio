// capsdialog.cpp
#include "capsdialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QVBoxLayout>

#include "capsfieldeditor.h"
#include "capsparse.h"

CapsDialog::CapsDialog(const CapsDialogInput& input, QWidget* parent) : QDialog(parent), input_(input) {
    setWindowTitle(input.title);
    setObjectName("capsDialog");
    resize(620, 560);

    auto* root = new QVBoxLayout(this);
    root->setSpacing(10);
    auto* heading = new QLabel(input.title, this);
    heading->setObjectName("heading");
    root->addWidget(heading);
    auto* sub = new QLabel(input.subtitle, this);
    sub->setObjectName("muted");
    root->addWidget(sub);

    // Media type: Otomatik or one of the structures the pad can produce.
    media_ = new QComboBox(this);
    media_->setObjectName("mediaCombo");
    media_->addItem("Otomatik");
    pages_ = new QStackedWidget(this);
    pages_->addWidget(new QLabel("Caps seçilmez: GStreamer pazarlık eder.", this));
    for (const StructureInfo& s : input.allowed) {
        media_->addItem(s.title());
        auto* page = new QWidget(pages_);
        auto* form = new QFormLayout(page);
        form->setContentsMargins(0, 8, 0, 0);
        form->setLabelAlignment(Qt::AlignLeft);
        QList<CapsFieldEditor*> editors;
        for (const FieldInfo& f : s.fields) {
            auto* editor = new CapsFieldEditor(f, page);
            editor->setObjectName("field_" + f.name);
            form->addRow(f.name + "  (" + f.type + ")", editor);
            editors << editor;
        }
        fields_ << editors;
        auto* scroll = new QScrollArea(pages_);
        scroll->setWidgetResizable(true);
        scroll->setWidget(page);
        pages_->addWidget(scroll);
    }
    auto* mediaRow = new QFormLayout;
    mediaRow->addRow("Medya türü", media_);
    root->addLayout(mediaRow);
    if (input.allowed.isEmpty()) {
        auto* none = new QLabel("Bu pad için seçilebilir caps bilgisi yok (son güncelleme vermedi).", this);
        none->setObjectName("muted");
        none->setWordWrap(true);
        root->addWidget(none);
    }
    root->addWidget(pages_, 3);
    connect(media_, &QComboBox::currentIndexChanged, pages_, &QStackedWidget::setCurrentIndex);

    // Prefill from the caps the link already has.
    gps::CapsChoice now = parseCapsChoice(input.current);
    for (int i = 0; i < input.allowed.size(); ++i) {
        const StructureInfo& s = input.allowed[i];
        if (s.media.toStdString() != now.media) continue;
        media_->setCurrentIndex(i + 1);
        for (const gps::CapsField& f : now.fields) {
            for (CapsFieldEditor* e : fields_[i]) {
                if (e->field().name.toStdString() == f.name) e->pin(QString::fromStdString(f.value));
            }
        }
        break;
    }

    // The sink pad's own settings (xpos of a compositor pad ...). Hidden when the pad has none.
    auto* padBox = new QGroupBox("Pad ayarları", this);
    padBox->setObjectName("padSettings");
    // The settings scroll inside a box of their own, so ten of them cannot squeeze the caps fields above.
    auto* padLayout = new QVBoxLayout(padBox);
    auto* padScroll = new QScrollArea(padBox);
    padScroll->setFrameShape(QFrame::NoFrame);
    padScroll->setWidgetResizable(true);
    auto* padInner = new QWidget;
    auto* padForm = new QFormLayout(padInner);
    padScroll->setWidget(padInner);
    padLayout->addWidget(padScroll);
    for (const PropertyInfo& p : input.padProperties) {
        if (!PropertyEditor::editable(p)) continue;
        auto given = input.padProps.find(p.name.toStdString());
        std::optional<QString> current;
        if (given != input.padProps.end()) current = QString::fromStdString(given->second);
        auto* editor = new PropertyEditor(p, current, padInner);
        editor->setObjectName("padEditor_" + p.name);
        padForm->addRow(p.name, editor);
        padEditors_ << editor;
    }
    padBox->setVisible(!padEditors_.isEmpty());
    root->addWidget(padBox, 2);
    if (!padEditors_.isEmpty()) resize(660, 760);

    auto* buttons = new QDialogButtonBox(this);
    QPushButton* save = buttons->addButton("Kaydet", QDialogButtonBox::AcceptRole);
    save->setObjectName("saveButton");
    save->setDefault(true);
    QPushButton* cancel = buttons->addButton("İptal", QDialogButtonBox::RejectRole);
    cancel->setObjectName("cancelButton");
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(buttons);
}

QString CapsDialog::caps() const {
    int index = media_->currentIndex() - 1;
    if (index < 0) return QString();

    const StructureInfo& s = input_.allowed[index];
    gps::CapsChoice choice;
    choice.media = s.media.toStdString();
    for (const QString& f : s.features) choice.features.push_back(f.toStdString());
    for (const CapsFieldEditor* e : fields_[index]) {
        if (std::optional<QString> value = e->pinned()) {
            choice.fields.push_back({e->field().name.toStdString(), e->field().type.toStdString(), value->toStdString()});
        }
    }
    return QString::fromStdString(choice.toText());
}

gps::Props CapsDialog::padProps() const { return PropertyEditor::collect(padEditors_, input_.padProps); }
