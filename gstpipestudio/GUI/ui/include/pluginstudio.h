#pragma once

#include <QHash>
#include <QObject>
#include <QPointer>

class MainWindow;
class PluginEditorWindow;
class PluginLibrary;

class PluginStudio : public QObject {
public:
    PluginStudio(PluginLibrary& library, MainWindow* window);

    void create();
    PluginEditorWindow* edit(const QString& id);
    void editSpec(const QString& id);
    void build(const QString& id);
    void copyPrompt(const QString& id);
    void openExternal(const QString& id);
    void reveal(const QString& id);
    void insert(const QString& id);
    void exportPlugin(const QString& id);
    void importPlugin();
    bool closeEditor(const QString& id);
    bool closeEditors();

private:
    void fail(const QString& title, const QString& error);
    void openLocation(const QString& path);

    PluginLibrary& library_;
    MainWindow* window_;
    QHash<QString, QPointer<PluginEditorWindow>> editors_;
};
