// notificationoverlay.cpp – see notificationoverlay.h.
#include "notificationoverlay.h"

#include <QAccessible>
#include <QEnterEvent>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMainWindow>
#include <QPointer>
#include <QPushButton>
#include <QStatusBar>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>
#include <memory>

#include "theme.h"

namespace {

int timeoutFor(NoticeSeverity severity) {
    switch (severity) {
    case NoticeSeverity::Info: return 5000;
    case NoticeSeverity::Warning: return 10000;
    case NoticeSeverity::Error: return 0;  // sticky until dismissed
    }
    return 0;
}

QColor accentFor(NoticeSeverity severity) {
    switch (severity) {
    case NoticeSeverity::Info: return Theme::accent();
    case NoticeSeverity::Warning: return Theme::warning();
    case NoticeSeverity::Error: return Theme::error();
    }
    return Theme::error();
}

QStyle::StandardPixmap iconFor(NoticeSeverity severity) {
    switch (severity) {
    case NoticeSeverity::Info: return QStyle::SP_MessageBoxInformation;
    case NoticeSeverity::Warning: return QStyle::SP_MessageBoxWarning;
    case NoticeSeverity::Error: return QStyle::SP_MessageBoxCritical;
    }
    return QStyle::SP_MessageBoxCritical;
}

QString prefixFor(NoticeSeverity severity) {
    switch (severity) {
    case NoticeSeverity::Info: return "Info";
    case NoticeSeverity::Warning: return "Warning";
    case NoticeSeverity::Error: return "Error";
    }
    return "Error";
}

}  // namespace

class NotificationOverlay::ToastCard final : public QFrame {
public:
    ToastCard(const Notice& notice, QWidget* parent, std::function<void()> onClose, std::function<void()> onViewLog)
        : QFrame(parent), notice_(notice), onClose_(std::move(onClose)) {
        setObjectName("toastCard");
        setFocusPolicy(Qt::NoFocus);
        setAttribute(Qt::WA_StyledBackground, true);
        const QColor stripe = accentFor(notice.severity);
        setStyleSheet(QString("QFrame#toastCard { background: %1; border: 1px solid %2; border-left: 4px solid %3; border-radius: 4px; }"
                              "QLabel#toastTitle { color: %4; font-weight: bold; }"
                              "QLabel#toastDetail { color: %5; }"
                              "QLabel#toastCount { color: %3; font-weight: bold; }")
                          .arg(Theme::box().name(), Theme::line().name(), stripe.name(), Theme::text().name(), Theme::muted().name()));

        auto* root = new QVBoxLayout(this);
        root->setContentsMargins(12, 10, 10, 10);
        root->setSpacing(6);

        auto* header = new QHBoxLayout;
        header->setSpacing(8);
        auto* iconLabel = new QLabel(this);
        iconLabel->setPixmap(style()->standardIcon(iconFor(notice.severity)).pixmap(16, 16));
        header->addWidget(iconLabel, 0, Qt::AlignTop);

        auto* titleLabel = new QLabel(notice.title, this);
        titleLabel->setObjectName("toastTitle");
        titleLabel->setWordWrap(true);
        header->addWidget(titleLabel, 1);

        countLabel_ = new QLabel(this);
        countLabel_->setObjectName("toastCount");
        countLabel_->hide();
        header->addWidget(countLabel_, 0, Qt::AlignTop);

        auto* closeBtn = new QPushButton("×", this);
        closeBtn->setObjectName("toastClose");
        closeBtn->setFlat(true);
        closeBtn->setFixedSize(22, 22);
        closeBtn->setToolTip("Dismiss notification");
        connect(closeBtn, &QPushButton::clicked, this, [this] { onClose_(); });
        header->addWidget(closeBtn, 0, Qt::AlignTop);
        root->addLayout(header);

        if (!notice.detail.isEmpty()) {
            auto* detailLabel = new QLabel(notice.detail, this);
            detailLabel->setObjectName("toastDetail");
            detailLabel->setWordWrap(true);
            detailLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
            root->addWidget(detailLabel);
        }

        if (notice.showLog) {
            auto* actions = new QHBoxLayout;
            actions->addStretch(1);
            auto* logBtn = new QPushButton("View Log", this);
            logBtn->setObjectName("toastViewLog");
            logBtn->setFlat(true);
            connect(logBtn, &QPushButton::clicked, this, [onViewLog] { onViewLog(); });
            actions->addWidget(logBtn);
            root->addLayout(actions);
        }

        const int ms = timeoutFor(notice.severity);
        if (ms > 0) {
            timer_ = new QTimer(this);
            timer_->setSingleShot(true);
            timer_->setInterval(ms);
            connect(timer_, &QTimer::timeout, this, [this] { onClose_(); });
            timer_->start();
        }
        announce();
    }

    const Notice& notice() const { return notice_; }

    void bump() {
        ++repeat_;
        countLabel_->setText(QString("×%1").arg(repeat_));
        countLabel_->show();
        if (timer_) {
            remainingMs_ = timeoutFor(notice_.severity);
            if (!hovered_) timer_->start(remainingMs_);
        }
        announce();
    }

protected:
    void enterEvent(QEnterEvent* event) override {
        hovered_ = true;
        if (timer_ && timer_->isActive()) {
            remainingMs_ = qMax(500, timer_->remainingTime());
            timer_->stop();
        }
        QFrame::enterEvent(event);
    }

    void leaveEvent(QEvent* event) override {
        hovered_ = false;
        if (timer_ && remainingMs_ > 0) timer_->start(remainingMs_);
        QFrame::leaveEvent(event);
    }

private:
    void announce() {
        setAccessibleName(prefixFor(notice_.severity) + ": " + notice_.summary());
        QAccessibleEvent alert(this, QAccessible::Alert);
        QAccessible::updateAccessibility(&alert);
    }

    Notice notice_;
    std::function<void()> onClose_;
    QLabel* countLabel_ = nullptr;
    QTimer* timer_ = nullptr;
    int repeat_ = 1;
    int remainingMs_ = 0;
    bool hovered_ = false;
};

NotificationOverlay::NotificationOverlay(QWidget* parent) : QWidget(parent) {
    setObjectName("notificationOverlay");
    setFocusPolicy(Qt::NoFocus);
    setAttribute(Qt::WA_NoSystemBackground, true);
    layout_ = new QVBoxLayout(this);
    layout_->setContentsMargins(0, 0, 0, 0);
    layout_->setSpacing(8);
    if (parent) parent->installEventFilter(this);
    hide();
}

void NotificationOverlay::push(const Notice& notice) {
    for (ToastCard* card : std::as_const(cards_)) {
        if (card->notice().sameAs(notice)) {
            card->bump();
            reposition();
            return;
        }
    }
    if (cards_.size() >= kMaxVisible) {
        ToastCard* victim = evictionCandidate(notice.severity);
        if (!victim) return;  // 3 higher-severity errors are visible; do not drop one for a low-priority info
        removeCard(victim);
    }
    auto holder = std::make_shared<QPointer<ToastCard>>();
    ToastCard* card = new ToastCard(
        notice, this,
        [this, holder] {
            if (*holder) removeCard(*holder);
        },
        [this] { emit viewLogRequested(); });
    *holder = card;
    cards_.append(card);
    layout_->addWidget(card);
    reposition();
}

void NotificationOverlay::dismissAll() {
    while (!cards_.isEmpty()) removeCard(cards_.last());
}

bool NotificationOverlay::eventFilter(QObject* watched, QEvent* event) {
    if (watched == parentWidget() && (event->type() == QEvent::Resize || event->type() == QEvent::Show)) reposition();
    return QWidget::eventFilter(watched, event);
}

void NotificationOverlay::removeCard(ToastCard* card) {
    if (!cards_.removeOne(card)) return;
    layout_->removeWidget(card);
    card->hide();
    card->setParent(nullptr);
    card->deleteLater();
    if (cards_.isEmpty()) hide();
    else QTimer::singleShot(0, this, [this] { reposition(); });
}

NotificationOverlay::ToastCard* NotificationOverlay::evictionCandidate(NoticeSeverity incoming) const {
    auto firstOf = [this](NoticeSeverity s) -> ToastCard* {
        for (ToastCard* c : cards_) {
            if (c->notice().severity == s) return c;
        }
        return nullptr;
    };
    if (ToastCard* info = firstOf(NoticeSeverity::Info)) return info;
    if (incoming == NoticeSeverity::Info) return nullptr;
    if (ToastCard* warn = firstOf(NoticeSeverity::Warning)) return warn;
    if (incoming == NoticeSeverity::Warning) return nullptr;
    return cards_.isEmpty() ? nullptr : cards_.first();
}

void NotificationOverlay::reposition() {
    QWidget* host = parentWidget();
    if (!host || cards_.isEmpty()) {
        hide();
        return;
    }
    constexpr int kMargin = 14;
    constexpr int kMaxWidth = 420;
    const int width = qMin(kMaxWidth, qMax(240, host->width() - 2 * kMargin));
    setFixedWidth(width);
    layout_->activate();
    const int height = sizeHint().height();
    int bottomReserved = 0;
    if (auto* mw = qobject_cast<QMainWindow*>(host)) {
        if (mw->statusBar() && mw->statusBar()->isVisible()) bottomReserved = mw->statusBar()->height();
    }
    const int x = qMax(kMargin, host->width() - width - kMargin);
    const int y = qMax(kMargin, host->height() - bottomReserved - height - kMargin);
    setGeometry(x, y, width, height);
    show();
    raise();
}
