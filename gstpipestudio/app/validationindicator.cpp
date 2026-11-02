// validationindicator.cpp
#include "validationindicator.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>

#include "controller.h"
#include "spinner.h"
#include "theme.h"

ValidationIndicator::ValidationIndicator(Controller* controller, QWidget* parent) : QWidget(parent), controller_(controller) {
    setObjectName("validationIndicator");
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(4);

    spinner_ = new Spinner(this);
    label_ = new QLabel(this);
    label_->setObjectName("indicatorText");
    elapsed_ = new QLabel(this);
    elapsed_->setObjectName("indicatorElapsed");
    auto link = [&](const char* name, const QString& text) {
        auto* b = new QPushButton(text, this);
        b->setObjectName(name);
        b->setProperty("link", true);
        b->setCursor(Qt::PointingHandCursor);
        return b;
    };
    wait_ = link("waitButton", "Wait");
    cancel_ = link("cancelValidationButton", "Cancel");
    support_ = link("supportButton", "Request support");
    retry_ = link("retryValidationButton", "Retry");
    for (QWidget* w : {static_cast<QWidget*>(spinner_), static_cast<QWidget*>(label_), static_cast<QWidget*>(elapsed_), static_cast<QWidget*>(wait_),
                       static_cast<QWidget*>(cancel_), static_cast<QWidget*>(support_), static_cast<QWidget*>(retry_)})
        row->addWidget(w);

    connect(wait_, &QPushButton::clicked, controller_, &Controller::waitMore);
    connect(cancel_, &QPushButton::clicked, controller_, &Controller::cancelUpdate);
    connect(retry_, &QPushButton::clicked, controller_, &Controller::refresh);
    connect(support_, &QPushButton::clicked, this, [this] {
        controller_->requestSupport(mode_ == Stopped ? "The validator stopped unexpectedly" : "Validation is taking longer than expected");
    });
    connect(controller_, &Controller::changed, this, &ValidationIndicator::refresh);
    connect(controller_->slowWatch(), &SlowWatch::ticked, this, &ValidationIndicator::refresh);
    refresh();
}

void ValidationIndicator::refresh() {
    if (controller_->updating()) mode_ = controller_->slowAsked() ? Slow : Validating;
    else mode_ = controller_->helperStopped() ? Stopped : Hidden;

    bool working = mode_ == Validating || mode_ == Slow;
    spinner_->setVisible(working);
    elapsed_->setVisible(working);
    wait_->setVisible(mode_ == Slow);
    cancel_->setVisible(working);
    cancel_->setEnabled(!controller_->cancelling());
    support_->setVisible(mode_ == Slow || mode_ == Stopped);
    retry_->setVisible(mode_ == Stopped);
    label_->setVisible(mode_ != Hidden);
    setVisible(mode_ != Hidden);

    elapsed_->setText(QString("%1 s").arg(controller_->slowWatch()->elapsedMs() / 1000));
    if (controller_->cancelling()) label_->setText("Cancelling…");
    else if (mode_ == Slow) label_->setText("Validation is taking longer than expected");
    else if (mode_ == Stopped) label_->setText("The validator stopped unexpectedly");
    else label_->setText("Validating…");
    label_->setStyleSheet(mode_ == Stopped ? "color: " + Theme::error().name() : QString());
}
