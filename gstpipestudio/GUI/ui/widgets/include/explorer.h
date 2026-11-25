// explorer.h – the Explorer panel: the open project's folder, every project in the projects location, and the
// template library. It only shows them and says what the user picked; the window opens files and templates.
#pragma once

#include <QFileSystemWatcher>
#include <QStringList>
#include <QWidget>

class QTreeWidget;
class QTreeWidgetItem;

class Explorer : public QWidget {
    Q_OBJECT
public:
    explicit Explorer(QWidget* parent = nullptr);

    // What there is to show: the open document's conf ("" = none yet) and the library's template names.
    void present(const QString& projectConf, const QStringList& templates);
    QTreeWidget* tree() const { return tree_; }

signals:
    void openRequested(const QString& confPath);                            // a .conf or a project, double clicked
    void templateRequested(const QString& name);                            // a library template, double clicked
    void templateMenuRequested(const QString& name, const QPoint& globalPos);  // ...or right clicked

private:
    void rebuild();
    void fillFolder(QTreeWidgetItem* parent, const QString& dir, int depth);
    void watch(const QStringList& dirs);

    QTreeWidget* tree_;
    QTreeWidgetItem* project_;
    QTreeWidgetItem* projects_;
    QTreeWidgetItem* templates_;
    QFileSystemWatcher watcher_;
    QString conf_;
    QStringList names_;
};
