// componentprovider.h – what can be placed on a canvas besides a single GStreamer element: the template library now,
// a library of custom element plugins later. The picker, the Explorer and the Controller reach every provider through
// this one interface, so a new kind of component needs a provider and nothing else.
#pragma once

#include <QList>
#include <QMetaType>
#include <QObject>
#include <QString>
#include <optional>

#include "graph.h"
#include "nodes.h"

// Which component: its provider (ComponentProvider::kind) and its id there, a template's name for instance.
struct ComponentRef {
    QString provider, id;
    bool operator==(const ComponentRef& other) const { return provider == other.provider && id == other.id; }
};
Q_DECLARE_METATYPE(ComponentRef)

// What a list shows of a component, and what it can be linked to.
struct ComponentInfo {
    ComponentRef ref;
    QString title;        // "Tail"
    QString description;  // "videoconvert → fakesink"
    QString hint;         // "template · 2"
    gps::Role role = gps::Role::Filter;
    bool takesInput = false;   // it can follow a free src pad: a free sink pad, a Request sink, or not known yet
    bool givesOutput = false;  // a free src pad
};

// What a component's context menu may offer; the window carries them out (they ask for names and files).
enum class ComponentAction { Edit, Rename, Export, Delete };

class ComponentProvider : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;

    virtual QString kind() const = 0;       // ComponentRef::provider, and the word for one: "template"
    virtual QString title() const = 0;      // its section's header: "Templates"
    virtual QString emptyHint() const = 0;  // what its section says while it is empty
    virtual QList<ComponentInfo> components() const = 0;
    virtual gps::Graph peek(const QString& id) const = 0;  // what is inside, for a quick look; empty when there is none
    // Places it on g with the top left corner of its content at (x, y): the node it became, nothing when there is none.
    virtual std::optional<gps::NodeRef> place(gps::Graph& g, const QString& id, double x, double y) const = 0;

    // Managing the components. A provider offers in actions() what it supports; the rest refuse.
    virtual QList<ComponentAction> actions(const QString& id) const;
    virtual bool canImport() const { return false; }
    virtual bool rename(const QString& id, const QString& to, QString* newId, QString* error);
    virtual bool remove(const QString& id);
    virtual QString removalNote(const QString& id) const;  // what deleting it means for the projects that use it
    virtual bool exportTo(const QString& id, const QString& file, QString* error) const;
    virtual QString importFrom(const QString& file, QString* error);  // the id it got, "" when refused

signals:
    void changed();
};

// The provider of this kind among these; nullptr when there is none.
template <typename Provider>
Provider* providerOf(const QList<Provider*>& providers, const QString& kind) {
    for (Provider* p : providers) {
        if (p->kind() == kind) return p;
    }
    return nullptr;
}
