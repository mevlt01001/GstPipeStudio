// drawer.cpp
#include "drawer.h"

#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>

Drawer::Drawer(QWidget* parent) : QWidget(parent) {
    setObjectName("drawer");
    setAttribute(Qt::WA_StyledBackground);
    setFixedWidth(0);
    hide();

    inner_ = new QWidget(this);  // always kWidth wide: the slide only uncovers it
    auto* root = new QVBoxLayout(inner_);
    root->setContentsMargins(12, 10, 12, 12);
    root->setSpacing(8);

    auto* header = new QHBoxLayout;
    back_ = new QToolButton(inner_);
    back_->setObjectName("drawerBack");
    back_->setText("←");
    back_->setToolTip("Back");
    title_ = new QLabel(inner_);
    title_->setObjectName("drawerTitle");
    title_->setWordWrap(true);
    close_ = new QToolButton(inner_);
    close_->setObjectName("drawerClose");
    close_->setText("✕");
    close_->setToolTip("Close (Esc)");
    header->addWidget(back_);
    header->addWidget(title_, 1);
    header->addWidget(close_);
    root->addLayout(header);

    subtitle_ = new QLabel(inner_);
    subtitle_->setObjectName("muted");
    subtitle_->setWordWrap(true);
    root->addWidget(subtitle_);

    stack_ = new QStackedWidget(inner_);
    stack_->setObjectName("drawerStack");
    root->addWidget(stack_, 1);

    slide_ = new QPropertyAnimation(this, "slideWidth", this);
    slide_->setDuration(kSlideMs);
    connect(slide_, &QPropertyAnimation::finished, this, [this] {
        if (open_) return;
        hide();
        clearPages();
    });
    connect(back_, &QToolButton::clicked, this, &Drawer::goBack);
    connect(close_, &QToolButton::clicked, this, [this] {
        if (page()) emit page()->cancelled();
        else dismiss();
    });
}

QString Drawer::title() const { return pages_.empty() ? QString() : pages_.back().title; }
QString Drawer::subtitle() const { return pages_.empty() ? QString() : pages_.back().subtitle; }

void Drawer::present(DrawerPage* page, const QString& title, const QString& subtitle, Placement placement) {
    if (placement == Replace) clearPages();
    pages_.push_back({page, title, subtitle});
    stack_->addWidget(page);
    connect(page, &DrawerPage::cancelled, this, &Drawer::dismiss);
    showTop();
    if (!open_) {
        open_ = true;
        show();
        slideTo(kWidth);
    }
    page->focusFirst();
}

void Drawer::goBack() {
    if (pages_.size() < 2) return;
    DrawerPage* top = pages_.back().widget;
    pages_.pop_back();
    stack_->removeWidget(top);
    top->deleteLater();
    showTop();
    pages_.back().widget->focusFirst();
}

void Drawer::dismiss() {
    if (!open_) return;
    open_ = false;
    slideTo(0);
    emit closed();
}

void Drawer::setBusy(bool busy) { stack_->setEnabled(!busy); }

void Drawer::focusSearch() {
    if (page()) page()->focusSearch();
}

void Drawer::showTop() {
    const Entry& top = pages_.back();
    stack_->setCurrentWidget(top.widget);
    title_->setText(top.title);
    subtitle_->setText(top.subtitle);
    subtitle_->setVisible(!top.subtitle.isEmpty());
    back_->setVisible(pages_.size() > 1);
}

void Drawer::clearPages() {
    for (const Entry& e : pages_) {
        stack_->removeWidget(e.widget);
        e.widget->deleteLater();
    }
    pages_.clear();
}

void Drawer::slideTo(int target) {
    slide_->stop();
    slide_->setStartValue(width());
    slide_->setEndValue(target);
    slide_->start();
}

void Drawer::resizeEvent(QResizeEvent*) { inner_->setGeometry(0, 0, kWidth, height()); }

void Drawer::keyPressEvent(QKeyEvent* event) {
    DrawerPage* current = page();
    if (!current) return QWidget::keyPressEvent(event);
    if (event->key() == Qt::Key_Escape) {
        emit current->cancelled();
    } else if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) && current->primary() && current->primary()->isEnabled()) {
        current->primary()->click();
    } else {
        QWidget::keyPressEvent(event);
    }
}
