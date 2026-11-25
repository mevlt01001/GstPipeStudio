// savetarget.h – what a document is saved to. The document asks its target for its title, its file, what the session
// remembers of it and where its template references are read from; Save hands the target the plan and the target
// does the rest (history, manifest, library revision). Save as, or the first save of an untitled document, binds the
// document to another target; loading another version or reverting keeps it.
#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include "graph.h"
#include "saveresult.h"

class TemplateLibrary;

class SaveTarget {
public:
    virtual ~SaveTarget() = default;
    virtual QString title() const = 0;                       // the tab's text: "door.conf", "Template: Tail", "Untitled-2"
    virtual QString path() const { return QString(); }       // the file it is, "" while untitled
    virtual QString sessionKey() const = 0;                  // what the session remembers: "<kind>:<value>"
    virtual bool needsDestination() const { return false; }  // untitled: Save asks where first
    virtual QString projectConf() const { return QString(); }      // the project conf it is, "" for anything else
    virtual QString libraryTemplate() const { return QString(); }  // the library template it is, "" for anything else
    virtual QString runBlockedReason() const { return QString(); } // why it cannot run on its own, "" when it can
    virtual QStringList templateFolders() const;             // where its template references are read from
    virtual SaveResult write(const gps::Graph& plan) = 0;    // the plan as the document holds it
};

// A canvas never saved: "Untitled-N". Its first Save asks for a project.
class UntitledTarget final : public SaveTarget {
public:
    static constexpr const char* kKind = "untitled";
    explicit UntitledTarget(int number) : number_(number) {}
    static int lowestFree(const QList<const SaveTarget*>& open);  // the N a new untitled document takes

    QString title() const override { return QString("Untitled-%1").arg(number_); }
    QString sessionKey() const override { return QString(kKind) + ":" + QString::number(number_); }
    bool needsDestination() const override { return true; }
    SaveResult write(const gps::Graph&) override { return SaveResult::failed("Choose a project to save it in first"); }

private:
    int number_;
};

// A project's conf: the file in the reference form, a copy of every template it uses in its templates/ folder, the
// project manifest, the projects registry, the version history and the recent files.
class ProjectConfTarget final : public SaveTarget {
public:
    static constexpr const char* kKind = "file";
    explicit ProjectConfTarget(const QString& confPath);
    static QString keyFor(const QString& confPath);

    QString title() const override;
    QString path() const override { return path_; }
    QString sessionKey() const override { return keyFor(path_); }
    QString projectConf() const override { return path_; }
    QStringList templateFolders() const override;
    SaveResult write(const gps::Graph& plan) override;

private:
    QString path_;  // absolute
};

// A template of the library, edited as a canvas: Save writes its next revision.
class LibraryTemplateTarget final : public SaveTarget {
public:
    static constexpr const char* kKind = "template";
    LibraryTemplateTarget(const QString& name, TemplateLibrary& library) : name_(name), library_(library) {}
    static QString keyFor(const QString& name) { return QString(kKind) + ":" + name; }

    QString title() const override { return "Template: " + name_; }
    QString path() const override;
    QString sessionKey() const override { return keyFor(name_); }
    QString libraryTemplate() const override { return name_; }
    QString runBlockedReason() const override { return "A template runs inside a pipeline: add it to one with Add template"; }
    SaveResult write(const gps::Graph& plan) override;

private:
    QString name_;
    TemplateLibrary& library_;
};
