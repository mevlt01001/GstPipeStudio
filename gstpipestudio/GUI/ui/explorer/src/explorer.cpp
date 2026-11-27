// explorer.cpp – see explorer.h.
#include "explorer.h"

#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QStyledItemDelegate>
#include <QToolButton>
#include <QToolTip>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <functional>

#include "explorerlook.h"
#include "explorersource.h"
#include "fileoperations.h"
#include "savetarget.h"
#include "theme.h"

using Kind = ExplorerEntry::Kind;

namespace {

constexpr int kEntry = Qt::UserRole;  // the row's entry as its section made it, without its children

ExplorerEntry stored(const QModelIndex& index) { return index.data(kEntry).value<ExplorerEntry>(); }

// Every row below item, depth first.
void forEach(QTreeWidgetItem* item, const std::function<void(QTreeWidgetItem*)>& visit) {
    for (int i = 0; i < item->childCount(); ++i) {
        visit(item->child(i));
        forEach(item->child(i), visit);
    }
}

// F2: the name in place. Each keystroke is checked (FileOperations::nameError) and the reason shown; Enter does not
// take a refused name, and the rename itself runs once the editor has closed, since it rebuilds the rows.
class RenameDelegate : public QStyledItemDelegate {
public:
    RenameDelegate(FileOperations& operations, QObject* parent) : QStyledItemDelegate(parent), operations_(operations) {}

    QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem&, const QModelIndex& index) const override {
        auto* line = new QLineEdit(parent);
        line->setObjectName("explorerRenameEditor");
        const ExplorerEntry entry = stored(index);
        connect(line, &QLineEdit::textEdited, line, [this, line, entry] { show(line, operations_.nameError(entry, line->text())); });
        return line;
    }

    // The name without its ending is selected (once the view has selected all of it): typing keeps .conf.
    void setEditorData(QWidget* editor, const QModelIndex& index) const override {
        auto* line = static_cast<QLineEdit*>(editor);
        const ExplorerEntry entry = stored(index);
        line->setText(entry.name);
        const int dot = entry.onDisk() ? entry.name.lastIndexOf('.') : -1;
        QTimer::singleShot(0, line, [line, length = dot > 0 ? dot : entry.name.size()] { line->setSelection(0, length); });
    }

    void setModelData(QWidget* editor, QAbstractItemModel*, const QModelIndex& index) const override {
        const QString wanted = static_cast<QLineEdit*>(editor)->text();
        const ExplorerEntry entry = stored(index);
        if (wanted == entry.name || !operations_.nameError(entry, wanted).isEmpty()) return;
        QTimer::singleShot(0, &operations_, [operations = &operations_, entry, wanted] { operations->rename(entry, wanted); });
    }

protected:
    bool eventFilter(QObject* object, QEvent* event) override {
        auto* line = qobject_cast<QLineEdit*>(object);
        if (line && event->type() == QEvent::KeyPress && !line->property("refused").toString().isEmpty()) {
            const int key = static_cast<QKeyEvent*>(event)->key();
            if (key == Qt::Key_Return || key == Qt::Key_Enter) return true;
        }
        return QStyledItemDelegate::eventFilter(object, event);
    }

private:
    static void show(QLineEdit* line, const QString& error) {
        line->setProperty("refused", error);
        line->setToolTip(error);
        line->setStyleSheet(error.isEmpty() ? QString() : "border: 1px solid " + Theme::error().name() + ";");
        if (!error.isEmpty()) QToolTip::showText(line->mapToGlobal(QPoint(0, line->height())), error, line);
    }

    FileOperations& operations_;
};

}  // namespace

Explorer::Explorer(const QList<ExplorerSource*>& sections, FileOperations& operations, QWidget* parent)
    : QWidget(parent), operations_(operations) {
    setObjectName("explorer");
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);

    auto* bar = new QHBoxLayout;
    bar->setContentsMargins(6, 6, 6, 0);
    bar->setSpacing(4);
    filter_ = new QLineEdit(this);
    filter_->setObjectName("explorerFilter");
    filter_->setPlaceholderText("Filter projects and components…");
    filter_->setClearButtonEnabled(true);
    connect(filter_, &QLineEdit::textChanged, this, &Explorer::applyFilter);
    bar->addWidget(filter_, 1);

    auto* newBtn = new QToolButton(this);
    newBtn->setObjectName("explorerNewProjectButton");
    newBtn->setText("+");
    newBtn->setToolTip("New project…");
    newBtn->setAutoRaise(true);
    connect(newBtn, &QToolButton::clicked, &operations_, &FileOperations::newProject);
    bar->addWidget(newBtn);

    auto* refreshBtn = new QToolButton(this);
    refreshBtn->setObjectName("explorerRefreshButton");
    refreshBtn->setText("↻");
    refreshBtn->setToolTip("Refresh Explorer");
    refreshBtn->setAutoRaise(true);
    connect(refreshBtn, &QToolButton::clicked, this, [this] {
        for (Section& s : sections_) s.source->refresh();
        refresh();
    });
    bar->addWidget(refreshBtn);
    layout->addLayout(bar);

    tree_ = new QTreeWidget(this);
    tree_->setObjectName("explorerTree");
    tree_->setHeaderHidden(true);
    tree_->setContextMenuPolicy(Qt::CustomContextMenu);
    tree_->setEditTriggers(QAbstractItemView::EditKeyPressed);
    tree_->setItemDelegate(new RenameDelegate(operations, tree_));
    tree_->installEventFilter(this);
    layout->addWidget(tree_, 1);
    for (ExplorerSource* source : sections) {
        auto* header = new QTreeWidgetItem(tree_);
        header->setFlags(Qt::ItemIsEnabled);
        header->setExpanded(true);
        sections_.append({source, header, {}, {}, {}});
        connect(source, &ExplorerSource::changed, &settle_, qOverload<>(&QTimer::start));
        const auto quick = source->quickActions();
        if (quick.isEmpty()) continue;
        auto* widget = new QWidget(tree_);
        auto* row = new QHBoxLayout(widget);
        row->setContentsMargins(2, 0, 2, 0);
        auto* label = new QLabel(source->label(editor_));
        auto font = label->font();
        font.setBold(true);
        label->setFont(font);
        label->setAttribute(Qt::WA_TransparentForMouseEvents);
        row->addWidget(label, 1);
        for (const auto action : quick) {
            auto* button = new QToolButton(widget);
            const auto entry = source->header(editor_);
            button->setAutoRaise(true);
            const bool refreshing = action == ExplorerAction::Refresh;
            button->setText(refreshing ? "↻" : "+");
            button->setObjectName(refreshing ? "explorerRefreshPluginsButton" : ExplorerLook::actionName(action) + "Button");
            button->setToolTip(ExplorerLook::actionText(action, entry));
            connect(button, &QToolButton::clicked, this, [this, source, action, entry, refreshing] {
                if (refreshing) {
                    source->refresh();
                    refresh();
                } else {
                    operations_.run(action, entry);
                }
            });
            row->addWidget(button);
        }
        tree_->setItemWidget(header, 0, widget);
    }

    settle_.setSingleShot(true);
    settle_.setInterval(kSettleMs);
    connect(&settle_, &QTimer::timeout, this, &Explorer::refresh);
    connect(&watcher_, &QFileSystemWatcher::directoryChanged, &settle_, qOverload<>(&QTimer::start));
    connect(&operations, &FileOperations::wrote, &settle_, qOverload<>(&QTimer::start));
    connect(tree_, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem* item) {
        const QList<ExplorerAction> offered = actionsOf(item);
        if (offered.contains(ExplorerAction::AddComponent)) operations_.run(ExplorerAction::AddComponent, entryOf(item));
        else if (offered.contains(ExplorerAction::Open) || offered.contains(ExplorerAction::Edit)) operations_.open(entryOf(item));
    });
    connect(tree_, &QWidget::customContextMenuRequested, this, &Explorer::showMenu);
    connect(tree_, &QTreeWidget::itemExpanded, this, [this](QTreeWidgetItem* item) { sectionOf(item)->expanded.insert(sourceEntry(item).id()); });
    connect(tree_, &QTreeWidget::itemCollapsed, this, [this](QTreeWidgetItem* item) { sectionOf(item)->expanded.remove(sourceEntry(item).id()); });
    refresh();
}

void Explorer::present(const EditorState& editor) {
    if (editor == editor_) return;  // the window tells after every change; most change nothing here
    const bool context = editor.projectConf != editor_.projectConf || editor.openFiles() != editor_.openFiles();
    const bool moved = editor.front != editor_.front;
    editor_ = editor;
    if (context) refresh();
    for (const Section& s : std::as_const(sections_)) decorate(s.header);
    if (moved || context) reveal(editor_.front);
}

void Explorer::refresh() {
    for (Section& s : sections_) reload(s);
    rewatch();
}

QString Explorer::title() const { return editor_.unsaved ? QString("Explorer · %1 unsaved").arg(editor_.unsaved) : QString("Explorer"); }

ExplorerEntry Explorer::sourceEntry(const QTreeWidgetItem* item) const { return item->data(0, kEntry).value<ExplorerEntry>(); }

// Decided on the row as it is drawn: an open tab's state over the disk's.
QList<ExplorerAction> Explorer::actionsOf(const QTreeWidgetItem* item) { return sectionOf(item)->source->actions(entryOf(item)); }

// The editor's state of the tab the row opens in wins over what the disk says.
ExplorerEntry Explorer::entryOf(const QTreeWidgetItem* item) const {
    ExplorerEntry e = sourceEntry(item);
    const QString key = e.key();
    if (!key.isEmpty()) {
        if (editor_.open.contains(key)) e.state = editor_.open.value(key);
        if (e.kind == Kind::Conf && editor_.validation.contains(key)) e.validation = editor_.validation.value(key);
    }
    if (e.kind == Kind::Project) {
        const QString folder = e.location();
        for (auto it = editor_.open.cbegin(); it != editor_.open.cend(); ++it) {
            if (SaveTarget::kindOf(it.key()) != ProjectConfTarget::kKind) continue;
            if (QFileInfo(SaveTarget::valueOf(it.key())).absolutePath() != folder) continue;
            if (it.value() == ExplorerEntry::State::Unsaved) e.state = ExplorerEntry::State::Unsaved;
            else if (it.value() == ExplorerEntry::State::Open && e.state == ExplorerEntry::State::Ok) e.state = ExplorerEntry::State::Open;
        }
    }
    return e;
}

QTreeWidgetItem* Explorer::itemOf(const QString& entryId) const {
    QTreeWidgetItem* found = nullptr;
    for (const Section& s : sections_) {
        forEach(s.header, [&](QTreeWidgetItem* item) {
            if (!found && sourceEntry(item).id() == entryId) found = item;
        });
    }
    return found;
}

void Explorer::reload(Section& s) {
    const ExplorerEntry head = s.source->header(editor_);
    QList<ExplorerEntry> entries = s.source->entries(editor_);
    if (entries.isEmpty()) {
        ExplorerEntry hint;
        hint.origin = s.source->id();
        hint.name = s.source->emptyHint();
        entries << hint;
    }
    if (s.built && head == s.head && entries == s.entries) return;
    const QString selected = tree_->currentItem() ? sourceEntry(tree_->currentItem()).id() : QString();
    const bool ownsSelection = tree_->currentItem() && sectionOf(tree_->currentItem()) == &s;
    s.header->setData(0, kEntry, QVariant::fromValue(head));
    qDeleteAll(s.header->takeChildren());
    fill(s.header, s, entries);
    s.head = head;
    s.entries = entries;
    s.built = true;
    decorate(s.header);
    applyFilter();
    if (ownsSelection) {
        if (QTreeWidgetItem* again = itemOf(selected)) tree_->setCurrentItem(again);
    }
}

void Explorer::applyFilter() {
    if (!filter_) return;
    const QString query = filter_->text().trimmed();
    std::function<bool(QTreeWidgetItem*, const Section&)> filterItem = [&](QTreeWidgetItem* item, const Section& sec) {
        const ExplorerEntry e = sourceEntry(item);
        bool childMatched = false;
        for (int i = 0; i < item->childCount(); ++i) {
            if (filterItem(item->child(i), sec)) childMatched = true;
        }
        if (query.isEmpty()) {
            item->setHidden(false);
            if (item != sec.header) item->setExpanded(sec.expanded.contains(e.id()));
            return true;
        }
        if (item == sec.header) {
            item->setHidden(false);
            item->setExpanded(true);
            return true;
        }
        const bool selfMatched = e.kind != Kind::Hint &&
                                 (e.name.contains(query, Qt::CaseInsensitive) || item->text(0).contains(query, Qt::CaseInsensitive) ||
                                  (e.kind == Kind::Component && e.path.contains(query, Qt::CaseInsensitive)));
        const bool visible = selfMatched || childMatched;
        item->setHidden(!visible);
        if (childMatched) item->setExpanded(true);
        return visible;
    };
    for (const Section& s : std::as_const(sections_)) filterItem(s.header, s);
}

void Explorer::fill(QTreeWidgetItem* parent, const Section& s, const QList<ExplorerEntry>& entries) {
    for (const ExplorerEntry& e : entries) {
        auto* item = new QTreeWidgetItem(parent);
        ExplorerEntry row = e;
        row.children.clear();
        item->setData(0, kEntry, QVariant::fromValue(row));
        Qt::ItemFlags flags = Qt::ItemIsEnabled;
        if (e.kind != Kind::Hint) flags |= Qt::ItemIsSelectable;
        if (s.source->actions(row).contains(ExplorerAction::Rename)) flags |= Qt::ItemIsEditable;
        item->setFlags(flags);
        fill(item, s, e.children);
        item->setExpanded(s.expanded.contains(row.id()));
    }
}

void Explorer::decorate(QTreeWidgetItem* item) {
    ExplorerLook::apply(item, entryOf(item));
    for (int i = 0; i < item->childCount(); ++i) decorate(item->child(i));
}

void Explorer::reveal(const QString& key) {
    if (key.isEmpty()) return;
    for (const Section& s : std::as_const(sections_)) {
        QTreeWidgetItem* found = nullptr;
        QTreeWidgetItem* fallback = nullptr;
        forEach(s.header, [&](QTreeWidgetItem* item) {
            const ExplorerEntry e = sourceEntry(item);
            if (e.key() != key) return;
            if (e.kind != Kind::Project && !found) found = item;
            else if (!fallback) fallback = item;
        });
        QTreeWidgetItem* target = found ? found : fallback;
        if (!target) continue;
        for (QTreeWidgetItem* up = target->parent(); up; up = up->parent()) {
            up->setExpanded(true);
            if (up->parent()) {
                if (Section* sec = sectionOf(up)) sec->expanded.insert(sourceEntry(up).id());
            }
        }
        tree_->setCurrentItem(target);
        tree_->scrollToItem(target);
        return;
    }
}

// The folders the sections show that exist now; the rest leave the watcher.
void Explorer::rewatch() {
    QStringList wanted;
    for (const Section& s : std::as_const(sections_)) {
        for (const QString& dir : s.source->watched(editor_)) {
            if (QFileInfo(dir).isDir() && !wanted.contains(dir)) wanted << dir;
        }
    }
    QStringList gone;
    for (const QString& dir : watcher_.directories()) {
        if (!wanted.contains(dir)) gone << dir;
    }
    if (!gone.isEmpty()) watcher_.removePaths(gone);
    for (const QString& dir : std::as_const(wanted)) {
        if (!watcher_.directories().contains(dir)) watcher_.addPath(dir);
    }
}

// The row's menu, or its section's on the header. Rename happens in place and Refresh here; the rest is FileOperations'.
void Explorer::showMenu(const QPoint& at) {
    QTreeWidgetItem* item = tree_->itemAt(at);
    if (!item) return;
    const QString id = sourceEntry(item).id();
    ExplorerSource* source = sectionOf(item)->source;
    operations_.popup(actionsOf(item), entryOf(item), tree_->viewport()->mapToGlobal(at), [this, id, source](ExplorerAction a) {
        if (a == ExplorerAction::Rename) startRename(id);
        if (a == ExplorerAction::Refresh) {
            source->refresh();
            refresh();
        }
        return a == ExplorerAction::Rename || a == ExplorerAction::Refresh;
    });
}

void Explorer::startRename(const QString& entryId) {
    if (QTreeWidgetItem* item = itemOf(entryId)) {
        tree_->setCurrentItem(item);
        tree_->editItem(item);
    }
}

Explorer::Section* Explorer::sectionOf(const QTreeWidgetItem* item) {
    while (item->parent()) item = item->parent();
    for (Section& s : sections_) {
        if (s.header == item) return &s;
    }
    return nullptr;
}

namespace {
// The keys of the canvas commands (Delete, Cut, Copy, Paste, Duplicate, Select all): in the tree they would act on a
// canvas the user is not looking at.
bool canvasKey(const QKeyEvent* key) {
    return key->key() == Qt::Key_Delete || key->key() == Qt::Key_Backspace || key->matches(QKeySequence::Cut) ||
           key->matches(QKeySequence::Copy) || key->matches(QKeySequence::Paste) || key->matches(QKeySequence::SelectAll) ||
           (key->key() == Qt::Key_D && key->modifiers() == Qt::ControlModifier);
}
}  // namespace

// The canvas commands do not see their keys while the tree has focus; Del moves the selected row to the trash.
bool Explorer::eventFilter(QObject* watched, QEvent* event) {
    const bool key = event->type() == QEvent::ShortcutOverride || event->type() == QEvent::KeyPress;
    if (watched != tree_ || !key || !canvasKey(static_cast<QKeyEvent*>(event))) return QWidget::eventFilter(watched, event);
    event->accept();
    QTreeWidgetItem* item = tree_->currentItem();
    if (event->type() == QEvent::KeyPress && static_cast<QKeyEvent*>(event)->key() == Qt::Key_Delete && item &&
        actionsOf(item).contains(ExplorerAction::Delete))
        operations_.remove(entryOf(item));
    return true;
}
