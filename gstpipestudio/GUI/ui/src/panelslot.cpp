// panelslot.cpp – see panelslot.h.
#include "panelslot.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

#include "drawer.h"
#include "panels.h"
#include "theme.h"

void PanelSlot::attach(Panels* panels, QWidget* tab, QWidget* fallback) {
    panels_ = panels;
    tab_ = tab;
    fallback_ = fallback;
}

bool PanelSlot::present(DrawerPage* page, const QString& title, const QString& subtitle, Visibility visibility) {
    if (!tab_ || !panels_) {
        page->deleteLater();
        return false;
    }
    clear();
    title_ = title;
    auto* layout = static_cast<QVBoxLayout*>(tab_->layout());
    auto* head = new QWidget(tab_);
    auto* row = new QHBoxLayout(head);
    row->setContentsMargins(0, 0, 0, 0);
    auto* titleLabel = new QLabel(title, head);
    titleLabel->setObjectName(name_ + "Title");
    titleLabel->setStyleSheet("font-weight: bold; color: " + Theme::accent().name() + ";");
    row->addWidget(titleLabel);
    if (!subtitle.isEmpty()) {
        auto* subLabel = new QLabel(subtitle, head);
        subLabel->setObjectName(name_ + "Subtitle");
        subLabel->setStyleSheet("color: " + Theme::muted().name() + ";");
        row->addWidget(subLabel);
    }
    row->addStretch(1);
    layout->addWidget(head);
    page->setParent(tab_);
    layout->addWidget(page, 1);
    page->show();
    if (visibility == Visibility::BringToFront) {
        panels_->show(tab_);
        page->focusFirst();
    }
    return true;
}

void PanelSlot::close() {
    title_.clear();
    if (!tab_) return;
    const bool wasShowing = panels_ && panels_->showing(tab_);
    clear();
    auto* layout = static_cast<QVBoxLayout*>(tab_->layout());
    auto* hint = new QLabel(hint_, tab_);
    hint->setObjectName("muted");
    layout->addWidget(hint);
    layout->addStretch(1);
    if (wasShowing && fallback_) panels_->show(fallback_);
}

// Every widget of the page goes (later: a form may be closing itself from its own signal).
void PanelSlot::clear() {
    QLayout* layout = tab_ ? tab_->layout() : nullptr;
    if (!layout) return;
    while (QLayoutItem* item = layout->takeAt(0)) {
        if (QWidget* w = item->widget()) {
            w->hide();
            w->setParent(nullptr);
            w->deleteLater();
        }
        delete item;
    }
}
