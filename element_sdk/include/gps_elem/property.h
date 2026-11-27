#pragma once

#include <gst/gst.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

namespace gps::elem {

class Element;

enum class Mutability {
    Null,
    Ready,
    Paused,
    Playing
};

template <typename E>
struct EnumValueDesc {
    E value;
    std::string name;
    std::string nick;
};

struct PropertyEntry {
    guint id = 0;
    std::string name;
    std::string nick;
    std::string blurb;
    Mutability mutability = Mutability::Playing;
    bool controllable = false;

    std::function<GParamSpec*(const PropertyEntry&)> makeSpec;
    std::function<void(Element&, const GValue*)> setter;
    std::function<void(const Element&, GValue*)> getter;
    std::function<void(Element&)> onChanged;
};

class PropertyRegistry {
public:
    class PropHandle {
    public:
        PropHandle(PropertyRegistry& reg, size_t index) : m_reg(reg), m_index(index) {}

        PropHandle& nick(std::string n) {
            m_reg.m_entries[m_index].nick = std::move(n);
            return *this;
        }

        PropHandle& blurb(std::string b) {
            m_reg.m_entries[m_index].blurb = std::move(b);
            return *this;
        }

        PropHandle& mutability(Mutability m) {
            m_reg.m_entries[m_index].mutability = m;
            return *this;
        }

        PropHandle& controllable(bool c) {
            m_reg.m_entries[m_index].controllable = c;
            return *this;
        }

        template <typename ElemT>
        PropHandle& onChange(std::function<void(ElemT&)> cb) {
            m_reg.m_entries[m_index].onChanged = [cb = std::move(cb)](Element& base) {
                if (cb) {
                    cb(static_cast<ElemT&>(base));
                }
            };
            return *this;
        }

    private:
        PropertyRegistry& m_reg;
        size_t m_index;
    };

    static GParamFlags buildFlags(const PropertyEntry& entry);

    template <typename ElemT>
    PropHandle prop(const std::string& name, bool ElemT::*member, bool defaultVal) {
        return addEntry(
            name,
            [defaultVal](const PropertyEntry& e) {
                return g_param_spec_boolean(
                    e.name.c_str(), e.nick.c_str(), e.blurb.c_str(),
                    defaultVal ? TRUE : FALSE, buildFlags(e));
            },
            [member](Element& base, const GValue* v) {
                static_cast<ElemT&>(base).*member = (g_value_get_boolean(v) != FALSE);
            },
            [member](const Element& base, GValue* v) {
                g_value_set_boolean(v, static_cast<const ElemT&>(base).*member ? TRUE : FALSE);
            });
    }

    template <typename ElemT>
    PropHandle prop(const std::string& name, int32_t ElemT::*member, int32_t defaultVal, int32_t minVal, int32_t maxVal) {
        return addEntry(
            name,
            [defaultVal, minVal, maxVal](const PropertyEntry& e) {
                return g_param_spec_int(
                    e.name.c_str(), e.nick.c_str(), e.blurb.c_str(),
                    minVal, maxVal, defaultVal, buildFlags(e));
            },
            [member](Element& base, const GValue* v) {
                static_cast<ElemT&>(base).*member = g_value_get_int(v);
            },
            [member](const Element& base, GValue* v) {
                g_value_set_int(v, static_cast<const ElemT&>(base).*member);
            });
    }

    template <typename ElemT>
    PropHandle prop(const std::string& name, uint32_t ElemT::*member, uint32_t defaultVal, uint32_t minVal, uint32_t maxVal) {
        return addEntry(
            name,
            [defaultVal, minVal, maxVal](const PropertyEntry& e) {
                return g_param_spec_uint(
                    e.name.c_str(), e.nick.c_str(), e.blurb.c_str(),
                    minVal, maxVal, defaultVal, buildFlags(e));
            },
            [member](Element& base, const GValue* v) {
                static_cast<ElemT&>(base).*member = g_value_get_uint(v);
            },
            [member](const Element& base, GValue* v) {
                g_value_set_uint(v, static_cast<const ElemT&>(base).*member);
            });
    }

    template <typename ElemT>
    PropHandle prop(const std::string& name, int64_t ElemT::*member, int64_t defaultVal, int64_t minVal, int64_t maxVal) {
        return addEntry(
            name,
            [defaultVal, minVal, maxVal](const PropertyEntry& e) {
                return g_param_spec_int64(
                    e.name.c_str(), e.nick.c_str(), e.blurb.c_str(),
                    minVal, maxVal, defaultVal, buildFlags(e));
            },
            [member](Element& base, const GValue* v) {
                static_cast<ElemT&>(base).*member = g_value_get_int64(v);
            },
            [member](const Element& base, GValue* v) {
                g_value_set_int64(v, static_cast<const ElemT&>(base).*member);
            });
    }

    template <typename ElemT>
    PropHandle prop(const std::string& name, uint64_t ElemT::*member, uint64_t defaultVal, uint64_t minVal, uint64_t maxVal) {
        return addEntry(
            name,
            [defaultVal, minVal, maxVal](const PropertyEntry& e) {
                return g_param_spec_uint64(
                    e.name.c_str(), e.nick.c_str(), e.blurb.c_str(),
                    minVal, maxVal, defaultVal, buildFlags(e));
            },
            [member](Element& base, const GValue* v) {
                static_cast<ElemT&>(base).*member = g_value_get_uint64(v);
            },
            [member](const Element& base, GValue* v) {
                g_value_set_uint64(v, static_cast<const ElemT&>(base).*member);
            });
    }

    template <typename ElemT>
    PropHandle prop(const std::string& name, float ElemT::*member, float defaultVal, float minVal, float maxVal) {
        return addEntry(
            name,
            [defaultVal, minVal, maxVal](const PropertyEntry& e) {
                return g_param_spec_float(
                    e.name.c_str(), e.nick.c_str(), e.blurb.c_str(),
                    minVal, maxVal, defaultVal, buildFlags(e));
            },
            [member](Element& base, const GValue* v) {
                static_cast<ElemT&>(base).*member = g_value_get_float(v);
            },
            [member](const Element& base, GValue* v) {
                g_value_set_float(v, static_cast<const ElemT&>(base).*member);
            });
    }

    template <typename ElemT>
    PropHandle prop(const std::string& name, double ElemT::*member, double defaultVal, double minVal, double maxVal) {
        return addEntry(
            name,
            [defaultVal, minVal, maxVal](const PropertyEntry& e) {
                return g_param_spec_double(
                    e.name.c_str(), e.nick.c_str(), e.blurb.c_str(),
                    minVal, maxVal, defaultVal, buildFlags(e));
            },
            [member](Element& base, const GValue* v) {
                static_cast<ElemT&>(base).*member = g_value_get_double(v);
            },
            [member](const Element& base, GValue* v) {
                g_value_set_double(v, static_cast<const ElemT&>(base).*member);
            });
    }

    template <typename ElemT>
    PropHandle prop(const std::string& name, std::string ElemT::*member, std::string defaultVal) {
        return addEntry(
            name,
            [defaultVal = std::move(defaultVal)](const PropertyEntry& e) {
                return g_param_spec_string(
                    e.name.c_str(), e.nick.c_str(), e.blurb.c_str(),
                    defaultVal.c_str(), buildFlags(e));
            },
            [member](Element& base, const GValue* v) {
                const char* s = g_value_get_string(v);
                static_cast<ElemT&>(base).*member = s ? std::string(s) : std::string();
            },
            [member](const Element& base, GValue* v) {
                g_value_set_string(v, (static_cast<const ElemT&>(base).*member).c_str());
            });
    }

    template <typename ElemT, typename EnumT, std::enable_if_t<std::is_enum_v<EnumT>, int> = 0>
    PropHandle prop(const std::string& name, EnumT ElemT::*member, EnumT defaultVal, GType enumGType) {
        return addEntry(
            name,
            [defaultVal, enumGType](const PropertyEntry& e) {
                return g_param_spec_enum(
                    e.name.c_str(), e.nick.c_str(), e.blurb.c_str(),
                    enumGType, static_cast<gint>(defaultVal), buildFlags(e));
            },
            [member](Element& base, const GValue* v) {
                static_cast<ElemT&>(base).*member = static_cast<EnumT>(g_value_get_enum(v));
            },
            [member](const Element& base, GValue* v) {
                g_value_set_enum(v, static_cast<gint>(static_cast<const ElemT&>(base).*member));
            });
    }

    template <typename ElemT, typename EnumT, std::enable_if_t<std::is_enum_v<EnumT>, int> = 0>
    PropHandle prop(
        const std::string& name,
        EnumT ElemT::*member,
        EnumT defaultVal,
        const std::string& enumTypeName,
        const std::vector<EnumValueDesc<EnumT>>& values) {
        GType gtype = registerEnumType<EnumT>(enumTypeName, values);
        return prop(name, member, defaultVal, gtype);
    }

    void installAll(GObjectClass* klass) const;
    bool setProperty(Element& elem, GstElement* gstElem, guint propId, const GValue* value) const;
    bool getProperty(const Element& elem, GstElement* gstElem, guint propId, GValue* value) const;

    const std::vector<PropertyEntry>& entries() const noexcept { return m_entries; }

private:
    PropHandle addEntry(
        const std::string& name,
        std::function<GParamSpec*(const PropertyEntry&)> makeSpec,
        std::function<void(Element&, const GValue*)> setter,
        std::function<void(const Element&, GValue*)> getter);

    template <typename EnumT>
    static GType registerEnumType(const std::string& typeName, const std::vector<EnumValueDesc<EnumT>>& values) {
        if (GType existing = g_type_from_name(typeName.c_str())) {
            return existing;
        }
        auto* storage = new std::vector<GEnumValue>();
        auto* strStorage = new std::vector<std::string>();
        strStorage->reserve(values.size() * 2);
        storage->reserve(values.size() + 1);
        for (const auto& v : values) {
            strStorage->push_back(v.name);
            const char* nPtr = strStorage->back().c_str();
            strStorage->push_back(v.nick);
            const char* nickPtr = strStorage->back().c_str();
            storage->push_back(GEnumValue{static_cast<gint>(v.value), nPtr, nickPtr});
        }
        storage->push_back(GEnumValue{0, nullptr, nullptr});
        return g_enum_register_static(typeName.c_str(), storage->data());
    }

    std::vector<PropertyEntry> m_entries;
};

}  // namespace gps::elem
