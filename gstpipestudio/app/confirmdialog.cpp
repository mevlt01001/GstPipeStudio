// confirmdialog.cpp
#include "confirmdialog.h"

#include <QDialogButtonBox>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

ConfirmDialog::ConfirmDialog(const QString& title, const QString& heading, const QStringList& lines, const QString& confirmText,
                             QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(title);
    setObjectName("confirmDialog");
    resize(460, 340);

    auto* root = new QVBoxLayout(this);
    root->setSpacing(10);
    auto* label = new QLabel(heading, this);
    label->setObjectName("heading");
    label->setWordWrap(true);
    root->addWidget(label);

    auto* list = new QListWidget(this);
    list->setObjectName("removalList");
    list->addItems(lines);
    list->setSelectionMode(QAbstractItemView::NoSelection);
    root->addWidget(list, 1);

    auto* buttons = new QDialogButtonBox(this);
    QPushButton* confirm = buttons->addButton(confirmText, QDialogButtonBox::AcceptRole);
    confirm->setObjectName("saveButton");
    QPushButton* cancel = buttons->addButton("Vazgeç", QDialogButtonBox::RejectRole);
    cancel->setObjectName("cancelButton");
    cancel->setDefault(true);  // the safe answer is the default
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(buttons);
}
