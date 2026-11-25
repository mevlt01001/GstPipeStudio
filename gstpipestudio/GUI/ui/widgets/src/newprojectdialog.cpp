// newprojectdialog.cpp – see newprojectdialog.h.
#include "newprojectdialog.h"

#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QVBoxLayout>

#include "settings.h"
#include "theme.h"

namespace {
QString safe(const QString& name) {
    QString out = name.trimmed();
    out.replace(QRegularExpression("[^A-Za-z0-9_.-]"), "_");
    return out;
}
}  // namespace

NewProjectDialog::NewProjectDialog(QWidget* parent) : QDialog(parent), location_(Settings::projectsLocation()) {
    setObjectName("newProjectDialog");
    setWindowTitle("New project");
    auto* root = new QVBoxLayout(this);
    auto* form = new QFormLayout;
    name_ = new QLineEdit(this);
    name_->setObjectName("projectName");
    name_->setPlaceholderText("Name of the project");
    form->addRow("Name", name_);
    auto* placeRow = new QHBoxLayout;
    where_ = new QLabel(this);
    where_->setObjectName("projectFolder");
    where_->setStyleSheet("color: " + Theme::muted().name() + ";");
    auto* change = new QPushButton("Change…", this);
    change->setObjectName("changeLocation");
    placeRow->addWidget(where_, 1);
    placeRow->addWidget(change);
    form->addRow("Folder", placeRow);
    root->addLayout(form);
    error_ = new QLabel(this);
    error_->setStyleSheet("color: " + Theme::error().name() + ";");
    error_->hide();
    root->addWidget(error_);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText("Create");
    buttons->button(QDialogButtonBox::Ok)->setObjectName("createProject");
    root->addWidget(buttons);
    setMinimumWidth(520);

    connect(name_, &QLineEdit::textChanged, this, &NewProjectDialog::check);
    connect(change, &QPushButton::clicked, this, [this] {
        const QString dir = QFileDialog::getExistingDirectory(this, "Where new projects go", location_);
        if (dir.isEmpty()) return;
        location_ = dir;
        Settings::setProjectsLocation(dir);
        check();
    });
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    check();
}

QString NewProjectDialog::confPath() const {
    const QString name = safe(name_->text());
    return QDir(location_).filePath(name + "/" + name + ".conf");
}

void NewProjectDialog::check() {
    const QString name = safe(name_->text());
    where_->setText(QDir(location_).filePath(name.isEmpty() ? "<name>" : name) + "/");
    const bool taken = !name.isEmpty() && QFileInfo::exists(confPath());
    error_->setText("A project named " + name + " is already there. Choose another name.");
    error_->setVisible(taken);
    findChild<QPushButton*>("createProject")->setEnabled(!name.isEmpty() && !taken);
}

QString NewProjectDialog::ask(QWidget* parent) {
    NewProjectDialog dialog(parent);
    if (dialog.exec() != QDialog::Accepted) return QString();
    const QString conf = dialog.confPath();
    if (!QDir().mkpath(QFileInfo(conf).absolutePath())) return QString();
    return conf;
}
