// explorer.h – the Explorer panel: the open project's folder, every project in the projects location, and a section per
// component provider (the template library, the plugins). It only shows them and says what the user picked; the window
// opens files and components.
#pragma once

#include <QFileSystemWatcher>
#include <QList>
#include <QStringList>
#include <QWidget>

#include "componentprovider.h"

class QTreeWidget;
class QTreeWidgetItem;

class Explorer : public QWidget {
    Q_OBJECT
public:
    explicit Explorer(const QList<ComponentProvider*>& providers, QWidget* parent = nullptr);

    // What there is to show: the open document's conf ("" = none yet).
    void present(const QString& projectConf);
    QTreeWidget* tree() const { return tree_; }

signals:
    void openRequested(const QString& confPath);                                         // a .conf or a project, double clicked
    void componentRequested(const ComponentRef& component);                              // a component, double clicked
    void componentMenuRequested(const ComponentRef& component, const QPoint& globalPos);  // ...or right clicked

private:
    void rebuild();
    void fillFolder(QTreeWidgetItem* parent, const QString& dir, int depth);
    void fillComponents(QTreeWidgetItem* section, const ComponentProvider& provider);
    void watch(const QStringList& dirs);

    QTreeWidget* tree_;
    QTreeWidgetItem* project_;
    QTreeWidgetItem* projects_;
    QList<QPair<ComponentProvider*, QTreeWidgetItem*>> components_;  // a section per provider
    QFileSystemWatcher watcher_;
    QString conf_;
};
