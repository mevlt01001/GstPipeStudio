// explorer.cpp – see explorer.h.
#include "explorer.h"

#include <QDir>
#include <QFileInfo>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "settings.h"
#include "theme.h"

namespace {

enum Role { Kind = Qt::UserRole, Value, Provider };
const QString kConf = "conf", kComponent = "component";

QTreeWidgetItem* section(QTreeWidget* tree, const QString& objectName) {
    auto* item = new QTreeWidgetItem(tree);
    item->setData(0, Kind, objectName);
    QFont bold = item->font(0);
    bold.setBold(true);
    item->setFont(0, bold);
    item->setFlags(Qt::ItemIsEnabled);
    item->setExpanded(true);
    return item;
}

QTreeWidgetItem* leaf(QTreeWidgetItem* parent, const QString& text, const QString& kind, const QString& value) {
    auto* item = new QTreeWidgetItem(parent, {text});
    item->setData(0, Kind, kind);
    item->setData(0, Value, value);
    item->setToolTip(0, value);
    return item;
}

void clear(QTreeWidgetItem* item) { qDeleteAll(item->takeChildren()); }

}  // namespace

Explorer::Explorer(const QList<ComponentProvider*>& providers, QWidget* parent) : QWidget(parent) {
    setObjectName("explorer");
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    tree_ = new QTreeWidget(this);
    tree_->setObjectName("explorerTree");
    tree_->setHeaderHidden(true);
    tree_->setContextMenuPolicy(Qt::CustomContextMenu);
    layout->addWidget(tree_);
    project_ = section(tree_, "project");
    projects_ = section(tree_, "projects");
    for (ComponentProvider* provider : providers) {
        components_.append({provider, section(tree_, provider->kind())});
        connect(provider, &ComponentProvider::changed, this, &Explorer::rebuild);
    }

    auto componentOf = [](const QTreeWidgetItem* item) {
        return ComponentRef{item->data(0, Provider).toString(), item->data(0, Value).toString()};
    };
    connect(tree_, &QTreeWidget::itemActivated, this, [this, componentOf](QTreeWidgetItem* item) {
        const QString kind = item->data(0, Kind).toString();
        if (kind == kConf) emit openRequested(item->data(0, Value).toString());
        else if (kind == kComponent) emit componentRequested(componentOf(item));
    });
    connect(tree_, &QWidget::customContextMenuRequested, this, [this, componentOf](const QPoint& at) {
        QTreeWidgetItem* item = tree_->itemAt(at);
        if (item && item->data(0, Kind).toString() == kComponent) emit componentMenuRequested(componentOf(item), tree_->viewport()->mapToGlobal(at));
    });
    connect(&watcher_, &QFileSystemWatcher::directoryChanged, this, &Explorer::rebuild);
    rebuild();
}

void Explorer::present(const QString& projectConf) {
    if (projectConf == conf_) return;  // the window asks after every change; most change nothing here
    conf_ = projectConf;
    rebuild();
}

void Explorer::rebuild() {
    const QString folder = conf_.isEmpty() ? QString() : QFileInfo(conf_).absolutePath();
    const QString location = Settings::projectsLocation();

    clear(project_);
    if (folder.isEmpty()) {
        project_->setText(0, "Project");
        leaf(project_, "Not saved yet", QString(), QString())->setForeground(0, Theme::muted());
    } else {
        project_->setText(0, "Project · " + QFileInfo(folder).fileName());
        fillFolder(project_, folder, 0);
    }

    clear(projects_);
    projects_->setText(0, "Projects");
    projects_->setToolTip(0, location);
    for (const QFileInfo& dir : QDir(location).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        const QString conf = QDir(dir.filePath()).filePath(dir.fileName() + ".conf");
        if (!QFileInfo::exists(conf)) continue;
        QTreeWidgetItem* item = leaf(projects_, dir.fileName(), kConf, conf);
        if (dir.absoluteFilePath() == folder) {
            QFont bold = item->font(0);
            bold.setBold(true);
            item->setFont(0, bold);
        }
    }

    for (const auto& [provider, section] : std::as_const(components_)) fillComponents(section, *provider);

    watch({folder, folder.isEmpty() ? QString() : QDir(folder).filePath("templates"), location});
}

// A provider's section: its components, or a muted line saying what will appear here.
void Explorer::fillComponents(QTreeWidgetItem* section, const ComponentProvider& provider) {
    clear(section);
    section->setText(0, provider.title());
    const QList<ComponentInfo> components = provider.components();
    if (components.isEmpty()) leaf(section, provider.emptyHint(), QString(), QString())->setForeground(0, Theme::muted());
    for (const ComponentInfo& c : components) {
        QTreeWidgetItem* item = leaf(section, c.title, kComponent, c.ref.id);
        item->setData(0, Provider, c.ref.provider);
        item->setToolTip(0, c.description);
    }
}

// The folder's sub-folders, then its files; a .conf opens. Deep trees stop after a few levels.
void Explorer::fillFolder(QTreeWidgetItem* parent, const QString& dir, int depth) {
    const QFileInfoList entries = QDir(dir).entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot, QDir::DirsFirst | QDir::Name);
    for (const QFileInfo& entry : entries) {
        if (entry.isDir()) {
            QTreeWidgetItem* sub = leaf(parent, entry.fileName() + "/", QString(), entry.absoluteFilePath());
            if (depth < 3) fillFolder(sub, entry.absoluteFilePath(), depth + 1);
        } else {
            QTreeWidgetItem* file = leaf(parent, entry.fileName(), entry.suffix() == "conf" ? kConf : QString(), entry.absoluteFilePath());
            if (entry.absoluteFilePath() == QFileInfo(conf_).absoluteFilePath()) {  // the file in front
                QFont bold = file->font(0);
                bold.setBold(true);
                file->setFont(0, bold);
            }
        }
    }
}

void Explorer::watch(const QStringList& dirs) {
    if (!watcher_.directories().isEmpty()) watcher_.removePaths(watcher_.directories());
    for (const QString& d : dirs) {
        if (!d.isEmpty() && QFileInfo(d).isDir()) watcher_.addPath(d);
    }
}
