// updatepanel.cpp
#include "updatepanel.h"

#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include "controller.h"
#include "theme.h"

UpdatePanel::UpdatePanel(Controller* controller, QWidget* parent) : QWidget(parent), controller_(controller) {
    setObjectName("updatePanel");
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(10, 8, 10, 8);
    root->setSpacing(6);

    status_ = new QLabel(this);
    status_->setObjectName("updateStatus");
    status_->setStyleSheet("font-weight: bold;");
    message_ = new QLabel(this);
    message_->setObjectName("updateMessage");
    message_->setWordWrap(true);
    message_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    lists_ = new QLabel(this);
    lists_->setObjectName("updateLists");
    lists_->setWordWrap(true);
    lists_->setTextInteractionFlags(Qt::TextSelectableByMouse);

    table_ = new QTableWidget(0, 7, this);
    table_->setObjectName("padsTable");
    table_->setHorizontalHeaderLabels({"Element", "Pad", "Presence", "Direction", "State", "Caps", "Allowed"});
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setAlternatingRowColors(true);
    table_->verticalHeader()->hide();
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->setWordWrap(false);

    // Whatever the text is, the labels never decide how wide the window is.
    for (QLabel* label : {message_, lists_}) label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);

    support_ = new QPushButton("Request support", this);
    support_->setObjectName("supportButton");
    support_->setToolTip("Open a prefilled problem report on GitHub");
    connect(support_, &QPushButton::clicked, controller_, [this] { controller_->requestSupport("Validation problem"); });
    auto* head = new QHBoxLayout;
    head->addWidget(status_, 1);
    head->addWidget(support_);
    root->addLayout(head);
    root->addWidget(message_);
    root->addWidget(lists_);
    root->addWidget(table_, 1);

    connect(controller, &Controller::changed, this, &UpdatePanel::refresh);
    refresh();
}

namespace {
// A label wraps only at spaces; a long path in an error text would stretch the whole window. Allow a break after '/'.
QString breakable(const QString& text) { return QString(text).replace("/", "/\u200b"); }
}  // namespace

void UpdatePanel::refresh() {
    const UpdateResult& r = controller_->result();
    QColor colour = controller_->notValidated() ? Theme::warning() : !r.valid() ? Theme::muted() : r.ok() ? (r.complete ? Theme::accent() : Theme::warning()) : Theme::error();
    QString status = controller_->updating() ? "Validating…" : controller_->notValidated() ? "Validation was cancelled — press Validate" : r.statusText();
    if (r.valid() && !controller_->updating() && !controller_->notValidated() && r.ms > 0) status += QString("   (%1 ms)").arg(r.ms, 0, 'f', 1);
    status_->setText(status);
    status_->setStyleSheet(QString("font-weight: bold; color: %1;").arg(colour.name()));

    bool named = r.errorElement.isEmpty() || r.message.contains(r.errorElement);
    message_->setText(r.message.isEmpty() ? QString() : breakable((named ? "" : "Element: " + r.errorElement + " – ") + r.message));
    message_->setStyleSheet(QString("color: %1;").arg(r.ok() ? Theme::text().name() : Theme::error().name()));

    QStringList lists;
    if (!r.missing.isEmpty()) lists << "Pads that did not appear: " + r.missing.join("; ");
    if (!r.refused.isEmpty()) lists << "Refused links: " + r.refused.join("; ");
    lists_->setText(breakable(lists.join("\n")));
    lists_->setStyleSheet(QString("color: %1;").arg(Theme::warning().name()));
    support_->setVisible(r.valid() && !r.ok() && !controller_->updating());  // something went wrong: offer the report
    message_->setVisible(!message_->text().isEmpty());
    lists_->setVisible(!lists_->text().isEmpty());

    table_->setRowCount(r.pads.size());
    for (int i = 0; i < r.pads.size(); ++i) {
        const PadReport& p = r.pads[i];
        QString state = p.unknownData ? "unknown data" : p.linked ? "linked" : p.temporaryEnd ? "free (temporary end)" : "free";
        QStringList cells = {p.element, p.pad, p.presence, p.isSrc ? "src" : "sink", state, p.caps, p.allowed};
        for (int c = 0; c < cells.size(); ++c) {
            auto* item = new QTableWidgetItem(cells[c]);
            item->setToolTip(cells[c]);
            if (p.unknownData) item->setForeground(Theme::warning());
            table_->setItem(i, c, item);
        }
    }
    table_->resizeColumnsToContents();
    table_->setColumnWidth(5, qMin(table_->columnWidth(5), 360));
}
