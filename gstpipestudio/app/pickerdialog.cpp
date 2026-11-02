// pickerdialog.cpp
#include "pickerdialog.h"

#include <QDialogButtonBox>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <algorithm>

#include "factorydelegate.h"

namespace {
constexpr int kExistingRole = Qt::UserRole + 20;  // index into PickerData::existing
constexpr int kFactoryRole = Qt::UserRole + 21;   // factory name of a new element
}  // namespace

PickerDialog::PickerDialog(const PickerData& data, QWidget* parent) : QDialog(parent), data_(data) {
    setWindowTitle(data.title);
    setObjectName("pickerDialog");
    resize(560, 560);

    auto* root = new QVBoxLayout(this);
    root->setSpacing(10);
    auto* heading = new QLabel(data.title, this);
    heading->setObjectName("heading");
    root->addWidget(heading);
    auto* sub = new QLabel(data.subtitle, this);
    sub->setObjectName("muted");
    sub->setWordWrap(true);
    root->addWidget(sub);

    search_ = new QLineEdit(this);
    search_->setObjectName("searchBox");
    search_->setPlaceholderText("Ara…");
    search_->setClearButtonEnabled(true);
    search_->installEventFilter(this);
    root->addWidget(search_);

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
        addHeader("Var olan elementler");
        for (int i = 0; i < data.existing.size(); ++i) {
            const ExistingTarget& t = data.existing[i];
            auto* item = new QListWidgetItem(t.element + " · " + t.sinkPad, list_);
            item->setData(LongNameRole, t.factory);
            item->setData(HintRole, t.request ? "Request a pad" : "");
            item->setData(SearchRole, (t.element + " " + t.factory + " " + t.sinkPad + (t.request ? " request a pad" : "")).toLower());
            item->setData(kExistingRole, i);
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
            item->setData(HintRole, i < compatibleCount ? "uyumlu" : "");
            item->setData(SearchRole, (e.factory + " " + e.longName + " " + e.klass).toLower());
            item->setData(kFactoryRole, e.factory);
        }
    }

    auto* buttons = new QDialogButtonBox(this);
    QPushButton* choose = buttons->addButton("Seç", QDialogButtonBox::AcceptRole);
    choose->setObjectName("saveButton");
    choose->setDefault(true);
    QPushButton* cancel = buttons->addButton("İptal", QDialogButtonBox::RejectRole);
    cancel->setObjectName("cancelButton");
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        if (choice().kind != PickerChoice::None) accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(buttons);

    connect(search_, &QLineEdit::textChanged, this, &PickerDialog::filter);
    connect(list_, &QListWidget::itemDoubleClicked, this, [this] {
        if (choice().kind != PickerChoice::None) accept();
    });
    filter(QString());
}

void PickerDialog::filter(const QString& text) {
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
        item->setHidden(!match);
        if (match) {
            headerHasRows = true;
            if (!first) first = item;
        }
    }
    closeHeader();
    list_->setCurrentItem(first);  // Enter takes the best hit
}

PickerChoice PickerDialog::choice() const {
    PickerChoice c;
    QListWidgetItem* item = list_->currentItem();
    if (!item || item->isHidden() || item->data(HeaderRole).toBool()) return c;
    if (item->data(kExistingRole).isValid()) {
        c.kind = PickerChoice::Existing;
        c.existing = data_.existing[item->data(kExistingRole).toInt()];
    } else {
        c.kind = PickerChoice::NewElement;
        c.factory = item->data(kFactoryRole).toString();
    }
    return c;
}

// Up and Down in the search box move the selection in the list.
bool PickerDialog::eventFilter(QObject* watched, QEvent* event) {
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
    return QDialog::eventFilter(watched, event);
}
