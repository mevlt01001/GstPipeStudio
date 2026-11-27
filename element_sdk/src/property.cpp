#include "gps_elem/property.h"

namespace gps::elem {

namespace {

int maxAllowedGstState(Mutability m) {
    switch (m) {
        case Mutability::Null:
            return static_cast<int>(GST_STATE_NULL);
        case Mutability::Ready:
            return static_cast<int>(GST_STATE_READY);
        case Mutability::Paused:
            return static_cast<int>(GST_STATE_PAUSED);
        case Mutability::Playing:
            return static_cast<int>(GST_STATE_PLAYING);
    }
    return static_cast<int>(GST_STATE_PLAYING);
}

}  // namespace

GParamFlags PropertyRegistry::buildFlags(const PropertyEntry& entry) {
    guint flags = G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS;
    if (entry.controllable) {
        flags |= GST_PARAM_CONTROLLABLE;
    }
    switch (entry.mutability) {
        case Mutability::Null:
            break;
        case Mutability::Ready:
            flags |= GST_PARAM_MUTABLE_READY;
            break;
        case Mutability::Paused:
            flags |= GST_PARAM_MUTABLE_PAUSED;
            break;
        case Mutability::Playing:
            flags |= GST_PARAM_MUTABLE_PLAYING;
            break;
    }
    return static_cast<GParamFlags>(flags);
}

PropertyRegistry::PropHandle PropertyRegistry::addEntry(
    const std::string& name,
    std::function<GParamSpec*(const PropertyEntry&)> makeSpec,
    std::function<void(Element&, const GValue*)> setter,
    std::function<void(const Element&, GValue*)> getter) {
    PropertyEntry entry;
    entry.id = static_cast<guint>(m_entries.size() + 1);
    entry.name = name;
    entry.nick = name;
    entry.blurb = name;
    entry.makeSpec = std::move(makeSpec);
    entry.setter = std::move(setter);
    entry.getter = std::move(getter);
    m_entries.push_back(std::move(entry));
    return PropHandle(*this, m_entries.size() - 1);
}

void PropertyRegistry::installAll(GObjectClass* klass) const {
    for (const auto& entry : m_entries) {
        GParamSpec* spec = entry.makeSpec(entry);
        g_object_class_install_property(klass, entry.id, spec);
    }
}

bool PropertyRegistry::setProperty(Element& elem, GstElement* gstElem, guint propId, const GValue* value) const {
    if (propId == 0 || propId > m_entries.size()) {
        return false;
    }
    const auto& entry = m_entries[propId - 1];

    std::function<void(Element&)> onChangedCopy;
    {
        GST_OBJECT_LOCK(gstElem);
        const int curState = static_cast<int>(GST_STATE(gstElem));
        const int maxState = maxAllowedGstState(entry.mutability);
        if (curState > maxState) {
            GST_OBJECT_UNLOCK(gstElem);
            GST_WARNING_OBJECT(
                gstElem,
                "Rejected setting property '%s' in state %s (mutability limit exceeded)",
                entry.name.c_str(),
                gst_element_state_get_name(static_cast<GstState>(curState)));
            return false;
        }
        entry.setter(elem, value);
        onChangedCopy = entry.onChanged;
        GST_OBJECT_UNLOCK(gstElem);
    }

    if (onChangedCopy) {
        onChangedCopy(elem);
    }
    return true;
}

bool PropertyRegistry::getProperty(const Element& elem, GstElement* gstElem, guint propId, GValue* value) const {
    if (propId == 0 || propId > m_entries.size()) {
        return false;
    }
    const auto& entry = m_entries[propId - 1];
    GST_OBJECT_LOCK(gstElem);
    entry.getter(elem, value);
    GST_OBJECT_UNLOCK(gstElem);
    return true;
}

}  // namespace gps::elem
