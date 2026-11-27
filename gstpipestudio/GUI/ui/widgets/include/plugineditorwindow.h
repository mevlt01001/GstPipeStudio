#pragma once

#include <QFileSystemWatcher>
#include <QMainWindow>

#include "notice.h"
#include "plugintoolchain.h"

class QLabel;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class PluginCodeEdit;
class PluginLibrary;

class PluginEditorWindow : public QMainWindow {
    Q_OBJECT
public:
    PluginEditorWindow(PluginLibrary& library, QString id, QWidget* parent);
    bool save();
    QString sourceText() const;

signals:
    void notification(const Notice& notice);
    void buildRequested();
    void specificationRequested();
    void externalEditorRequested();
    void insertRequested();

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void syncFromDisk();
    void watchSource();
    void copyPrompt();
    void refresh();
    void showBuild(const PluginBuildResult& build);

    PluginLibrary& library_;
    QString id_, diskText_;
    QFileSystemWatcher watcher_;
    QLabel *status_, *diskWarning_;
    PluginCodeEdit* code_;
    QPlainTextEdit* output_;
    QListWidget* diagnostics_;
    QPushButton *build_, *spec_, *insert_;
    QList<BuildDiagnostic> rows_;
    bool building_ = false;
};
