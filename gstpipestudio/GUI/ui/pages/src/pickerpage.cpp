// pickerpage.cpp
#include "pickerpage.h"

#include <QButtonGroup>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>

#include "factorydelegate.h"

namespace {
constexpr int kExistingRole = Qt::UserRole + 20;  // index into PickerData::existing
constexpr int kFactoryRole = Qt::UserRole + 21;   // factory name of a new element
constexpr int kIsSourceRole = Qt::UserRole + 22;
constexpr int kIsSinkRole = Qt::UserRole + 23;
constexpr int kHasAlwaysRole = Qt::UserRole + 24;
constexpr int kHasSometimesRole = Qt::UserRole + 25;
constexpr int kHasRequestRole = Qt::UserRole + 26;
constexpr int kCustomRole = Qt::UserRole + 27;    // name of a saved custom element
}  // namespace

PickerPage::PickerPage(const PickerData& data, QWidget* parent) : DrawerPage(parent), data_(data) {
    setObjectName("pickerPage");

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(8);

    search_ = new QLineEdit(this);
    search_->setObjectName("searchBox");
    search_->setPlaceholderText("Search…");
    search_->setClearButtonEnabled(true);
    search_->installEventFilter(this);
    root->addWidget(search_);

    if (data.showFilters) {
        auto makeChipGroup = [this](const QString& prefix, const QList<QPair<QString, int>>& items, auto onSelect) {
            auto* row = new QHBoxLayout;
            row->setContentsMargins(0, 0, 0, 0);
            row->setSpacing(6);
            auto* group = new QButtonGroup(this);
            group->setExclusive(true);
            for (const auto& [label, id] : items) {
                auto* btn = new QToolButton(this);
                btn->setObjectName(prefix + "_" + label.toLower());
                btn->setText(label);
                btn->setCheckable(true);
                btn->setCursor(Qt::PointingHandCursor);
                btn->setStyleSheet(
                    "QToolButton { background: #2d2d2d; border: 1px solid #444444; border-radius: 4px; padding: 4px 10px; color: #b8b8b8; }"
                    "QToolButton:hover { border-color: #76b900; color: #e8e8e8; }"
                    "QToolButton:checked { background: #76b900; color: #111111; border-color: #76b900; font-weight: bold; }");
                if (id == 0) btn->setChecked(true);
                group->addButton(btn, id);
                row->addWidget(btn);
            }
            row->addStretch(1);
            connect(group, &QButtonGroup::idClicked, this, onSelect);
            return row;
        };
        root->addLayout(makeChipGroup("filterRole", {{"All", RoleAll}, {"SRC", RoleSrc}, {"FILTER", RoleFilterElem}, {"SINK", RoleSink}},
                                      [this](int id) { setRoleFilter(static_cast<RoleFilter>(id)); }));
        root->addLayout(makeChipGroup("filterPresence", {{"Any Pad", PresenceAll}, {"Always", PresenceAlways}, {"Sometimes", PresenceSometimes}, {"Request", PresenceRequest}},
                                      [this](int id) { setPresenceFilter(static_cast<PresenceFilter>(id)); }));
    }

    list_ = new QListWidget(this);
    list_->setObjectName("pickerList");
    list_->setItemDelegate(new FactoryDelegate(list_));
    list_->setMouseTracking(true);
    root->addWidget(list_, 1);

    auto addHeader = [&](const QString& text) {
        auto* item = new QListWidgetItem(text, list_);
        item->setData(HeaderRole, true);
        item->setFlags(Qt::NoItemFlags);
    };

    if (!data.existing.isEmpty()) {
        addHeader("Elements in the pipeline");
        for (int i = 0; i < data.existing.size(); ++i) {
            const ExistingTarget& t = data.existing[i];
            auto* item = new QListWidgetItem(t.element + " · " + t.sinkPad, list_);
            item->setData(LongNameRole, t.factory);
            item->setData(HintRole, t.request ? "Request a pad" : "");
            item->setData(SearchRole, (t.element + " " + t.factory + " " + t.sinkPad + (t.request ? " request a pad" : "")).toLower());
            item->setData(kExistingRole, i);
        }
    }

    if (!data.custom.isEmpty()) {
        addHeader("Custom elements");
        for (const CustomElementInfo& c : data.custom) {
            auto* item = new QListWidgetItem(c.name, list_);
            item->setData(LongNameRole, c.description);
            item->setData(HintRole, QString("custom · %1").arg(c.elementCount));
            item->setData(SearchRole, (c.name + " " + c.description + " custom").toLower());
            item->setData(kCustomRole, c.name);
            item->setData(kIsSourceRole, c.isSource);
            item->setData(kIsSinkRole, c.isSink);
            item->setData(kHasAlwaysRole, true);
        }
    }

    // New elements: the compatible ones first in the helper's rank order, then the rest by rank and name.
    QList<ListEntry> ordered;
    for (const QString& name : data.compatible) {
        auto found = std::find_if(data.candidates.begin(), data.candidates.end(), [&](const ListEntry& e) { return e.factory == name; });
        if (found != data.candidates.end()) ordered << *found;
    }
    int compatibleCount = ordered.size();
    QList<ListEntry> rest;
    for (const ListEntry& e : data.candidates) {
        if (!data.compatible.contains(e.factory)) rest << e;
    }
    std::sort(rest.begin(), rest.end(), [](const ListEntry& a, const ListEntry& b) {
        return a.rank != b.rank ? a.rank > b.rank : a.factory < b.factory;
    });
    ordered << rest;

    if (!ordered.isEmpty()) {
        addHeader(data.newHeader);
        for (int i = 0; i < ordered.size(); ++i) {
            const ListEntry& e = ordered[i];
            auto* item = new QListWidgetItem(e.factory, list_);
            item->setData(LongNameRole, e.longName);
            item->setData(NvidiaRole, e.nvidia);
            item->setData(HintRole, i < compatibleCount ? "compatible" : "");
            item->setData(SearchRole, (e.factory + " " + e.longName + " " + e.klass).toLower());
            item->setData(kFactoryRole, e.factory);
            item->setData(kIsSourceRole, e.isSource);
            item->setData(kIsSinkRole, e.isSink);
            item->setData(kHasAlwaysRole, e.hasAlwaysSrc || e.hasAlwaysSink || (!e.hasSometimesSrc && !e.hasSometimesSink && !e.hasRequestSrc && !e.hasRequestSink));
            item->setData(kHasSometimesRole, e.hasSometimesSrc || e.hasSometimesSink);
            item->setData(kHasRequestRole, e.hasRequestSrc || e.hasRequestSink);
        }
    }

    auto* buttons = new QHBoxLayout;
    buttons->addStretch(1);
    auto* cancel = new QPushButton("Cancel", this);
    cancel->setObjectName("cancelButton");
    primary_ = new QPushButton("Choose", this);
    primary_->setObjectName("saveButton");
    buttons->addWidget(cancel);
    buttons->addWidget(primary_);
    root->addLayout(buttons);
    connect(cancel, &QPushButton::clicked, this, &DrawerPage::cancelled);
    connect(primary_, &QPushButton::clicked, this, [this] { choose(); });

    connect(search_, &QLineEdit::textChanged, this, &PickerPage::filter);
    connect(list_, &QListWidget::itemDoubleClicked, this, [this] { choose(); });
    connect(list_, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        if (item && item->data(kFactoryRole).isValid()) emit previewRequested(item->data(kFactoryRole).toString());
    });
    filter(QString());
}

void PickerPage::setRoleFilter(RoleFilter role) {
    roleFilter_ = role;
    filter(search_->text());
}

void PickerPage::setPresenceFilter(PresenceFilter presence) {
    presenceFilter_ = presence;
    filter(search_->text());
}

void PickerPage::focusFirst() { search_->setFocus(); }

void PickerPage::focusSearch() {
    search_->setFocus();
    search_->selectAll();
}

void PickerPage::choose() {
    PickerChoice c = choice();
    if (c.kind != PickerChoice::None) emit picked(c);
}

void PickerPage::filter(const QString& text) {
    QString needle = text.trimmed().toLower();
    QListWidgetItem* header = nullptr;
    bool headerHasRows = false;
    QListWidgetItem* first = nullptr;
    auto closeHeader = [&] {
        if (header) header->setHidden(!headerHasRows);
    };
    for (int i = 0; i < list_->count(); ++i) {
        QListWidgetItem* item = list_->item(i);
        if (item->data(HeaderRole).toBool()) {
            closeHeader();
            header = item;
            headerHasRows = false;
            continue;
        }
        bool match = needle.isEmpty() || item->data(SearchRole).toString().contains(needle);
        if (match && (item->data(kFactoryRole).isValid() || item->data(kCustomRole).isValid())) {
            bool isSrc = item->data(kIsSourceRole).toBool();
            bool isSnk = item->data(kIsSinkRole).toBool();
            if (roleFilter_ == RoleSrc && !isSrc) match = false;
            else if (roleFilter_ == RoleFilterElem && (isSrc || isSnk)) match = false;
            else if (roleFilter_ == RoleSink && !isSnk) match = false;

            if (match && presenceFilter_ != PresenceAll && item->data(kFactoryRole).isValid()) {
                if (presenceFilter_ == PresenceAlways && !item->data(kHasAlwaysRole).toBool()) match = false;
                else if (presenceFilter_ == PresenceSometimes && !item->data(kHasSometimesRole).toBool()) match = false;
                else if (presenceFilter_ == PresenceRequest && !item->data(kHasRequestRole).toBool()) match = false;
            }
        }
        item->setHidden(!match);
        if (match) {
            headerHasRows = true;
            if (!first) first = item;
        }
    }
    closeHeader();
    list_->setCurrentItem(first);  // Enter takes the best hit
}

PickerChoice PickerPage::choice() const {
    PickerChoice c;
    QListWidgetItem* item = list_->currentItem();
    if (!item || item->isHidden() || item->data(HeaderRole).toBool()) return c;
    if (item->data(kExistingRole).isValid()) {
        c.kind = PickerChoice::Existing;
        c.existing = data_.existing[item->data(kExistingRole).toInt()];
    } else if (item->data(kCustomRole).isValid()) {
        c.kind = PickerChoice::CustomElement;
        c.factory = item->data(kCustomRole).toString();
    } else {
        c.kind = PickerChoice::NewElement;
        c.factory = item->data(kFactoryRole).toString();
    }
    return c;
}

// Up and Down in the search box move the selection in the list.
bool PickerPage::eventFilter(QObject* watched, QEvent* event) {
    if (watched == search_ && event->type() == QEvent::KeyPress) {
        int key = static_cast<QKeyEvent*>(event)->key();
        if (key == Qt::Key_Down || key == Qt::Key_Up) {
            int step = key == Qt::Key_Down ? 1 : -1;
            for (int i = list_->currentRow() + step; i >= 0 && i < list_->count(); i += step) {
                QListWidgetItem* item = list_->item(i);
                if (!item->isHidden() && !item->data(HeaderRole).toBool()) {
                    list_->setCurrentRow(i);
                    break;
                }
            }
            return true;
        }
    }
    return DrawerPage::eventFilter(watched, event);
}
