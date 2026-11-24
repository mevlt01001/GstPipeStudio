// capspage.cpp
#include "capspage.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QVBoxLayout>

#include "capsfieldeditor.h"

CapsPage::CapsPage(const CapsPageInput& input, QWidget* parent) : DrawerPage(parent), input_(input) {
    setObjectName("capsPage");

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(6);

    auto* contentRow = new QHBoxLayout;
    contentRow->setSpacing(12);
    auto* leftCol = new QVBoxLayout;
    leftCol->setSpacing(6);

    // Media type: Auto or one of the structures the pad can produce.
    media_ = new QComboBox(this);
    media_->setObjectName("mediaCombo");
    media_->addItem("Auto");
    pages_ = new QStackedWidget(this);
    pages_->addWidget(new QLabel("No caps are chosen: GStreamer negotiates them.", this));
    for (const StructureInfo& s : input.allowed) {
        media_->addItem(fromStd(gps::structureTitle(s)));
        auto* page = new QWidget(pages_);
        auto* form = new QFormLayout(page);
        form->setContentsMargins(0, 8, 0, 0);
        form->setRowWrapPolicy(QFormLayout::WrapAllRows);
        QList<CapsFieldEditor*> editors;
        for (const CapsFieldInfo& f : s.fields) {
            auto* editor = new CapsFieldEditor(f, page);
            editor->setObjectName(fromStd("field_" + f.name));
            form->addRow(fromStd(f.name + "  (" + f.type + ")"), editor);
            editors << editor;
        }
        fields_ << editors;
        auto* scroll = new QScrollArea(pages_);
        scroll->setWidgetResizable(true);
        scroll->setWidget(page);
        pages_->addWidget(scroll);
    }
    auto* mediaRow = new QFormLayout;
    mediaRow->addRow("Media type", media_);
    leftCol->addLayout(mediaRow);
    if (input.allowed.empty()) {
        auto* none = new QLabel("The last validation gave no caps to choose from for this pad.", this);
        none->setObjectName("muted");
        none->setWordWrap(true);
        leftCol->addWidget(none);
    }
    leftCol->addWidget(pages_, 1);
    contentRow->addLayout(leftCol, 3);
    connect(media_, &QComboBox::currentIndexChanged, pages_, &QStackedWidget::setCurrentIndex);

    // Prefill from the caps the link already has.
    gps::CapsChoice now = gps::CapsChoice::fromText(toStd(input.current));
    for (int i = 0; i < static_cast<int>(input.allowed.size()); ++i) {
        const StructureInfo& s = input.allowed[i];
        if (s.media != now.media) continue;
        media_->setCurrentIndex(i + 1);
        for (const gps::CapsField& f : now.fields) {
            for (CapsFieldEditor* e : fields_[i]) {
                if (e->field().name == f.name) e->pin(fromStd(f.value));
            }
        }
        break;
    }

    // The sink pad's own settings (xpos of a compositor pad ...). Hidden when the pad has none.
    auto* padBox = new QGroupBox("Pad settings", this);
    padBox->setObjectName("padSettings");
    auto* padLayout = new QVBoxLayout(padBox);
    auto* padScroll = new QScrollArea(padBox);
    padScroll->setFrameShape(QFrame::NoFrame);
    padScroll->setWidgetResizable(true);
    auto* padInner = new QWidget;
    auto* padForm = new QFormLayout(padInner);
    padForm->setRowWrapPolicy(QFormLayout::WrapAllRows);
    padScroll->setWidget(padInner);
    padLayout->addWidget(padScroll);
    for (const PropertyInfo& p : input.padProperties) {
        if (!PropertyEditor::editable(p)) continue;
        auto given = input.padProps.find(p.name);
        std::optional<QString> current;
        if (given != input.padProps.end()) current = fromStd(given->second);
        auto* editor = new PropertyEditor(p, current, padInner);
        editor->setObjectName(fromStd("padEditor_" + p.name));
        padForm->addRow(fromStd(p.name), editor);
        padEditors_ << editor;
    }
    padBox->setVisible(!padEditors_.isEmpty());
    contentRow->addWidget(padBox, 2);
    root->addLayout(contentRow, 1);

    QHBoxLayout* buttons = addFooter(root, "Apply");
    if (input.skippable) {
        auto* skip = new QPushButton("Skip", this);
        skip->setObjectName("skipButton");
        skip->setToolTip("Keep Auto caps");
        buttons->insertWidget(buttons->indexOf(primary_), skip);
        connect(skip, &QPushButton::clicked, this, &CapsPage::skipped);
    }
    connect(primary_, &QPushButton::clicked, this, &CapsPage::applied);
}

void CapsPage::focusFirst() { media_->setFocus(); }

QString CapsPage::caps() const {
    int index = media_->currentIndex() - 1;
    if (index < 0) return QString();

    const StructureInfo& s = input_.allowed[index];
    gps::CapsChoice choice;
    choice.media = s.media;
    choice.features = s.features;
    for (const CapsFieldEditor* e : fields_[index]) {
        if (std::optional<QString> value = e->pinned()) {
            choice.fields.push_back({e->field().name, e->field().type, toStd(*value)});
        }
    }
    return QString::fromStdString(choice.toText());
}

gps::Props CapsPage::padProps() const { return PropertyEditor::collect(padEditors_, input_.padProps); }
