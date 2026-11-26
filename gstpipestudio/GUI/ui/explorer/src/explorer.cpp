// explorer.cpp – see explorer.h.
#include "explorer.h"

#include <QDir>
#include <QFileInfo>
#include <QKeyEvent>
#include <QLineEdit>
#include <QStyledItemDelegate>
#include <QToolTip>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <functional>

#include "explorerlook.h"
#include "explorersource.h"
#include "fileoperations.h"
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
    tree_ = new QTreeWidget(this);
    tree_->setObjectName("explorerTree");
    tree_->setHeaderHidden(true);
    tree_->setContextMenuPolicy(Qt::CustomContextMenu);
    tree_->setEditTriggers(QAbstractItemView::EditKeyPressed);
    tree_->setItemDelegate(new RenameDelegate(operations, tree_));
    tree_->installEventFilter(this);
    layout->addWidget(tree_);
    for (ExplorerSource* source : sections) {
        auto* header = new QTreeWidgetItem(tree_);
        header->setFlags(Qt::ItemIsEnabled);
        header->setExpanded(true);
        sections_.append({source, header});
        connect(source, &ExplorerSource::changed, &settle_, qOverload<>(&QTimer::start));
    }

    settle_.setSingleShot(true);
    settle_.setInterval(kSettleMs);
    connect(&settle_, &QTimer::timeout, this, &Explorer::refresh);
    connect(&watcher_, &QFileSystemWatcher::directoryChanged, &settle_, qOverload<>(&QTimer::start));
    connect(&operations, &FileOperations::wrote, &settle_, qOverload<>(&QTimer::start));
    connect(tree_, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem* item) {
        const QList<ExplorerAction> offered = sectionOf(item)->source->actions(sourceEntry(item));
        if (offered.contains(ExplorerAction::Open) || offered.contains(ExplorerAction::Edit)) operations_.open(entryOf(item));
    });
    connect(tree_, &QWidget::customContextMenuRequested, this, &Explorer::showMenu);
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

// The editor's state of the tab the row opens in wins over what the disk says.
ExplorerEntry Explorer::entryOf(const QTreeWidgetItem* item) const {
    ExplorerEntry e = sourceEntry(item);
    const QString key = e.key();
    if (!key.isEmpty() && editor_.open.contains(key)) e.state = editor_.open.value(key);
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
    QStringList expanded;
    forEach(s.header, [&](QTreeWidgetItem* item) {
        if (item->isExpanded()) expanded << sourceEntry(item).id();
    });
    const QString selected = tree_->currentItem() ? sourceEntry(tree_->currentItem()).id() : QString();
    const bool ownsSelection = tree_->currentItem() && sectionOf(tree_->currentItem()) == &s;
    s.header->setData(0, kEntry, QVariant::fromValue(head));
    qDeleteAll(s.header->takeChildren());
    fill(s.header, s, entries, expanded);
    s.head = head;
    s.entries = entries;
    s.built = true;
    decorate(s.header);
    if (ownsSelection) {
        if (QTreeWidgetItem* again = itemOf(selected)) tree_->setCurrentItem(again);
    }
}

void Explorer::fill(QTreeWidgetItem* parent, const Section& s, const QList<ExplorerEntry>& entries, const QStringList& expanded) {
    for (const ExplorerEntry& e : entries) {
        auto* item = new QTreeWidgetItem(parent);
        ExplorerEntry row = e;
        row.children.clear();
        item->setData(0, kEntry, QVariant::fromValue(row));
        Qt::ItemFlags flags = Qt::ItemIsEnabled;
        if (e.kind != Kind::Hint) flags |= Qt::ItemIsSelectable;
        if (s.source->actions(row).contains(ExplorerAction::Rename)) flags |= Qt::ItemIsEditable;
        item->setFlags(flags);
        fill(item, s, e.children, expanded);
        item->setExpanded(expanded.contains(row.id()));
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
        forEach(s.header, [&](QTreeWidgetItem* item) {
            if (!found && sourceEntry(item).key() == key) found = item;
        });
        if (!found) continue;
        for (QTreeWidgetItem* up = found->parent(); up; up = up->parent()) up->setExpanded(true);
        tree_->setCurrentItem(found);
        tree_->scrollToItem(found);
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
    Section* section = sectionOf(item);
    const QString id = sourceEntry(item).id();
    ExplorerSource* source = section->source;
    operations_.popup(source->actions(sourceEntry(item)), entryOf(item), tree_->viewport()->mapToGlobal(at), [this, id, source](ExplorerAction a) {
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

// Del moves the selected row to the trash; the window's Delete (of the canvas) does not see the key.
bool Explorer::eventFilter(QObject* watched, QEvent* event) {
    const bool del = (event->type() == QEvent::ShortcutOverride || event->type() == QEvent::KeyPress) &&
                     static_cast<QKeyEvent*>(event)->key() == Qt::Key_Delete;
    if (watched != tree_ || !del) return QWidget::eventFilter(watched, event);
    event->accept();
    QTreeWidgetItem* item = tree_->currentItem();
    if (event->type() == QEvent::KeyPress && item && sectionOf(item)->source->actions(sourceEntry(item)).contains(ExplorerAction::Delete))
        operations_.remove(entryOf(item));
    return true;
}
