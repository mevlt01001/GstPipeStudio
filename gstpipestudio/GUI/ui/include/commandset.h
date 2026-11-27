// commandset.h – every command of the window once: its action (words, keys, what it does), when it is on and, while it
// is off, why (its tooltip says so). The menus, the toolbar and the context menus show these same actions; refresh()
// sets every one from its rule. A command about one element, link or template copy acts on the target a context menu
// was opened on.
#pragma once

#include <QHash>
#include <QKeySequence>
#include <QList>
#include <QObject>
#include <QPointF>
#include <QString>
#include <functional>
#include <limits>

class QAction;

enum class Command {
    // File
    New, NewProject, NewPlugin, Open, Save, SaveAs, SaveAll, Revert, CloseTab, ImportLaunch, ExportSharedLibrary, VersionHistory, Quit,
    // Edit
    Undo, Redo, Cut, Copy, Paste, Duplicate, Delete, SelectAll, Find,
    // View
    LightTheme, NextTab, PreviousTab, ZoomIn, ZoomOut, Fit, Arrange, AutoAlignment, LinkCurves, LinkRightAngles, ConnectedMove,
    // Pipeline
    AddElement, AddTemplate, AddPlugin, AddLaunch, SaveAsTemplate, TemplateParameters, ConnectSelected, DisconnectSelected, AlignSelection,
    Validate, Run,
    // Help
    WelcomeTour, GstInfo, Support, About,
    // What a context menu was opened on
    EditProperties, RemoveQueue, EditCaps, ReplaceBehind, CutLink, InsertQueue, EditTemplate, ToggleTemplate, DetachTemplate,
};

class CommandSet : public QObject {
    Q_OBJECT
public:
    // One row of the table: what the command is and does, and why it is off now ("" when it is on; no rule: always on).
    struct Spec {
        Command id;
        const char* name;  // the action's object name: "actionSave"
        QString text;      // "&Save"
        QList<QKeySequence> keys;
        std::function<void()> run;
        std::function<QString()> off = {};
    };
    // What a context menu was opened on.
    struct Target {
        QString element, pad, instance;
        QPointF scenePos{std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::quiet_NaN()};
    };

    explicit CommandSet(QObject* parent);

    void add(const QList<Spec>& specs);
    QAction* operator[](Command id) const { return entries_.value(id).action; }
    // Words that change with the state (Run / Stop, a template's name), a tooltip while it is on (its words by
    // default), a check mark (a setting).
    void setLabel(Command id, std::function<QString()> label) { entries_[id].label = std::move(label); }
    void setTip(Command id, const QString& tip) { entries_[id].tip = tip; }
    void setChecked(Command id, std::function<bool()> checked);

    void setTarget(const Target& target) { target_ = target; }
    const Target& target() const { return target_; }
    void refresh();  // every action from its rule

private:
    struct Entry {
        QAction* action = nullptr;
        std::function<QString()> off, label;
        QString tip;
        std::function<bool()> checked;
    };
    QHash<Command, Entry> entries_;
    Target target_;
};
