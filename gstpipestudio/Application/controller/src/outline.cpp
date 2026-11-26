// outline.cpp – see outline.h.
#include "outline.h"

#include "contract.h"
#include "templates.h"

Outline::Outline(const gps::Graph& plan, const QSet<QString>& expanded) {
    for (const gps::TemplateInstance& t : plan.instances) members_[fromStd(t.id)] = fromStd(gps::templates::members(plan, t.id));
    for (const gps::Element& e : plan.elements) {
        const QString name = fromStd(e.name);
        QString box = name;
        QStringList frames;
        const std::vector<std::string> around = gps::templates::chain(plan, e.name);
        for (auto up = around.rbegin(); up != around.rend(); ++up) {
            const QString id = fromStd(*up);
            if (!expanded.contains(id)) {
                box = id;
                break;
            }
            frames << id;
        }
        box_[name] = box;
        frames_[name] = frames;
    }
    for (const gps::TemplateInstance& t : plan.instances) {
        bool shown = expanded.contains(fromStd(t.id));
        for (std::string up = t.parent; shown && !up.empty(); up = plan.instance(up)->parent) shown = expanded.contains(fromStd(up));
        if (shown) visible_ << fromStd(t.id);
    }
}

QString Outline::boxOf(const QString& element) const { return box_.value(element, element); }

QString Outline::collapsedTemplate(const QString& element) const {
    const QString box = boxOf(element);
    return box == element ? QString() : box;
}

QString Outline::anchorOf(const QString& box) const { return box_.contains(box) ? box : members_.value(box).value(0); }

QStringList Outline::membersOf(const QString& box) const {
    if (box_.contains(box)) return {box};
    return members_.value(box);
}

QStringList Outline::boxesOf(const QStringList& elements) const {
    QStringList out;
    for (const QString& e : elements) {
        const QString box = boxOf(e);
        if (!out.contains(box)) out << box;
    }
    return out;
}

QStringList Outline::framesAround(const QString& element) const { return frames_.value(element); }

QStringList Outline::expand(const QStringList& elements) const {
    QStringList out;
    QSet<QString> seen;
    for (const QString& e : elements) {
        if (!box_.contains(e)) continue;
        for (const QString& m : membersOf(boxOf(e))) {
            if (!seen.contains(m)) seen.insert(m), out << m;
        }
    }
    return out;
}
