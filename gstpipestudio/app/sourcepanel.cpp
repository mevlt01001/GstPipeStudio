// sourcepanel.cpp
#include "sourcepanel.h"

#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <algorithm>

#include "controller.h"
#include "factorydelegate.h"

SourcePanel::SourcePanel(Controller* controller, QWidget* parent) : QWidget(parent), controller_(controller) {
    setObjectName("sourcePanel");
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(10, 10, 10, 10);
    root->setSpacing(8);

    auto* heading = new QLabel("Kaynak ekle", this);
    heading->setObjectName("heading");
    root->addWidget(heading);
    auto* hint = new QLabel("Çift tıkla veya seçip Ekle'ye bas.", this);
    hint->setObjectName("muted");
    hint->setWordWrap(true);
    root->addWidget(hint);

    search_ = new QLineEdit(this);
    search_->setObjectName("sourceSearch");
    search_->setPlaceholderText("Kaynak ara…");
    search_->setClearButtonEnabled(true);
    root->addWidget(search_);

    list_ = new QListWidget(this);
    list_->setObjectName("sourceList");
    list_->setItemDelegate(new FactoryDelegate(list_));
    list_->setMouseTracking(true);
    root->addWidget(list_, 1);

    auto* add = new QPushButton("Ekle", this);
    add->setObjectName("addSourceButton");
    add->setProperty("accent", true);
    root->addWidget(add);

    connect(search_, &QLineEdit::textChanged, this, &SourcePanel::filter);
    connect(list_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) { emit sourceChosen(item->text()); });
    connect(add, &QPushButton::clicked, this, [this] {
        if (list_->currentItem()) emit sourceChosen(list_->currentItem()->text());
    });
    connect(controller, &Controller::catalogReady, this, &SourcePanel::reload);
    reload();
}

void SourcePanel::reload() {
    list_->clear();
    QList<ListEntry> sources;
    for (const ListEntry& e : controller_->catalog()) {
        if (e.isSource) sources << e;
    }
    std::sort(sources.begin(), sources.end(), [](const ListEntry& a, const ListEntry& b) {
        return a.rank != b.rank ? a.rank > b.rank : a.factory < b.factory;
    });
    for (const ListEntry& e : sources) {
        auto* item = new QListWidgetItem(e.factory, list_);
        item->setData(LongNameRole, e.longName);
        item->setData(NvidiaRole, e.nvidia);
        item->setData(SearchRole, (e.factory + " " + e.longName + " " + e.klass).toLower());
    }
    filter(search_->text());
}

void SourcePanel::filter(const QString& text) {
    QString needle = text.trimmed().toLower();
    for (int i = 0; i < list_->count(); ++i) {
        list_->item(i)->setHidden(!needle.isEmpty() && !list_->item(i)->data(SearchRole).toString().contains(needle));
    }
}
