// setuppage.cpp
#include "setuppage.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

SetupPage::SetupPage(QWidget* parent) : QWidget(parent) {
    setObjectName("setupPage");
    auto* outer = new QVBoxLayout(this);
    outer->addStretch(1);
    auto* column = new QVBoxLayout;
    column->setSpacing(12);
    auto* row = new QHBoxLayout;
    row->addStretch(1);
    row->addLayout(column, 3);
    row->addStretch(1);
    outer->addLayout(row);
    outer->addStretch(2);

    title_ = new QLabel(this);
    title_->setObjectName("setupTitle");
    text_ = new QLabel(this);
    text_->setObjectName("setupText");
    text_->setWordWrap(true);
    hint_ = new QLabel(this);
    hint_->setObjectName("muted");
    hint_->setWordWrap(true);
    commandLabel_ = new QLabel(this);
    commandLabel_->setObjectName("setupCommand");
    commandLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    commandLabel_->setWordWrap(true);
    commandLabel_->setStyleSheet("font-family: 'DejaVu Sans Mono'; background: #242424; border: 1px solid #3a3a3a; border-radius: 4px; padding: 10px;");

    auto* buttons = new QHBoxLayout;
    auto* retry = new QPushButton("Retry", this);
    retry->setObjectName("retryButton");
    retry->setProperty("accent", true);
    auto* copy = new QPushButton("Copy command", this);
    copy->setObjectName("copyCommandButton");
    buttons->addWidget(retry);
    buttons->addWidget(copy);
    buttons->addStretch(1);

    for (QWidget* w : {static_cast<QWidget*>(title_), static_cast<QWidget*>(text_), static_cast<QWidget*>(hint_), static_cast<QWidget*>(commandLabel_)}) column->addWidget(w);
    column->addLayout(buttons);

    connect(retry, &QPushButton::clicked, this, &SetupPage::retry);
    connect(copy, &QPushButton::clicked, this, [this] { QGuiApplication::clipboard()->setText(command_); });
}

void SetupPage::show(const StartupResult& r) {
    QString needs = QString("%1.%2").arg(kMinGstVersion.major).arg(kMinGstVersion.minor);
    switch (r.state) {
    case StartupResult::NotInstalled:
        title_->setText("GStreamer was not found");
        text_->setText("GstPipeStudio needs GStreamer " + needs + " or newer. Install it with this command, then press Retry:");
        hint_->setText("GstPipeStudio never installs anything itself.");
        command_ = "sudo apt install gstreamer1.0-tools gstreamer1.0-plugins-base gstreamer1.0-plugins-good";
        break;
    case StartupResult::TooOld:
        title_->setText("GStreamer is too old");
        text_->setText("GStreamer " + r.version + " was found, GstPipeStudio needs " + needs + " or newer on this system.");
        hint_->setText("Upgrade GStreamer with your distribution, then press Retry. This command shows the version in use:");
        command_ = "gst-inspect-1.0 --version";
        break;
    default:
        title_->setText("The GstPipeStudio helper could not start");
        text_->setText("GStreamer is installed, but gps-inspect did not give a usable answer." + (r.detail.isEmpty() ? QString() : " It said: " + r.detail));
        hint_->setText("Run this command in a terminal to see the problem, then press Retry:");
        command_ = "gps-inspect version";
        break;
    }
    commandLabel_->setText(command_);
}
