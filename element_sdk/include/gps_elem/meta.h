#pragma once

#include "gps_elem/buffer.h"

#include <gst/gst.h>

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <mutex>
#include <string>
#include <type_traits>
#include <typeinfo>
#include <utility>

namespace gps::elem {

namespace detail {

inline std::mutex& metaRegistrationMutex() {
    static std::mutex m;
    return m;
}

inline std::string sanitizeTypeName(const char* rawName) {
    if (!rawName) {
        return "Unknown_0000000000000000";
    }
    std::string out;
    uint64_t hash = 14695981039346656037ULL;
    for (const char* p = rawName; *p != '\0'; ++p) {
        const unsigned char ch = static_cast<unsigned char>(*p);
        hash ^= static_cast<uint64_t>(ch);
        hash *= 1099511628211ULL;
        if (std::isalnum(ch) || ch == '_') {
            out.push_back(static_cast<char>(ch));
        } else {
            out.push_back('_');
        }
    }
    if (out.empty()) {
        out = "Unknown";
    }
    char hexBuf[18];
    std::snprintf(hexBuf, sizeof(hexBuf), "_%016llx", static_cast<unsigned long long>(hash));
    out += hexBuf;
    return out;
}

}  // namespace detail

template <typename T>
struct CustomMetaNode {
    GstMeta meta;
    T* value;
};

template <typename T>
class CustomMeta {
    static_assert(std::is_copy_constructible_v<T>, "CustomMeta<T> requires copy-constructible T");

public:
    static GType apiType() {
        static const GType type = []() {
            std::lock_guard<std::mutex> lock(detail::metaRegistrationMutex());
            std::string name = "GpsCustomMetaAPI_" + detail::sanitizeTypeName(typeid(T).name());
            if (GType existing = g_type_from_name(name.c_str())) {
                return existing;
            }
            static const gchar* tags[] = {nullptr};
            return gst_meta_api_type_register(name.c_str(), tags);
        }();
        return type;
    }

    static const GstMetaInfo* info() {
        static const GstMetaInfo* metaInfo = []() {
            const GType api = apiType();
            std::lock_guard<std::mutex> lock(detail::metaRegistrationMutex());
            std::string implName = "GpsCustomMeta_" + detail::sanitizeTypeName(typeid(T).name());
            if (const GstMetaInfo* existing = gst_meta_get_info(implName.c_str())) {
                return existing;
            }
            return gst_meta_register(
                api,
                implName.c_str(),
                sizeof(CustomMetaNode<T>),
                &CustomMeta<T>::initFunc,
                &CustomMeta<T>::freeFunc,
                &CustomMeta<T>::transformFunc);
        }();
        return metaInfo;
    }

    static T* attach(Buffer& buf, T value) {
        if (!buf.isValid()) {
            return nullptr;
        }
        buf.ensureWritable();
        auto* existing = reinterpret_cast<CustomMetaNode<T>*>(gst_buffer_get_meta(buf.get(), apiType()));
        if (existing) {
            if (existing->value) {
                *existing->value = std::move(value);
            } else {
                existing->value = new T(std::move(value));
            }
            return existing->value;
        }
        auto copy = std::make_unique<T>(std::move(value));
        auto* node = reinterpret_cast<CustomMetaNode<T>*>(gst_buffer_add_meta(buf.get(), info(), nullptr));
        if (!node) {
            return nullptr;
        }
        node->value = copy.release();
        return node->value;
    }

    static const T* get(const Buffer& buf) noexcept {
        if (!buf.isValid()) {
            return nullptr;
        }
        auto* node = reinterpret_cast<CustomMetaNode<T>*>(gst_buffer_get_meta(buf.get(), apiType()));
        return node ? node->value : nullptr;
    }

    static T* getMutable(Buffer& buf) {
        if (!buf.isValid()) {
            return nullptr;
        }
        buf.ensureWritable();
        auto* node = reinterpret_cast<CustomMetaNode<T>*>(gst_buffer_get_meta(buf.get(), apiType()));
        return node ? node->value : nullptr;
    }

    static bool remove(Buffer& buf) {
        if (!buf.isValid()) {
            return false;
        }
        buf.ensureWritable();
        GstMeta* meta = gst_buffer_get_meta(buf.get(), apiType());
        if (!meta) {
            return false;
        }
        return gst_buffer_remove_meta(buf.get(), meta) != FALSE;
    }

private:
    static gboolean initFunc(GstMeta* meta, gpointer /*params*/, GstBuffer* /*buffer*/) {
        auto* node = reinterpret_cast<CustomMetaNode<T>*>(meta);
        node->value = nullptr;
        return TRUE;
    }

    static void freeFunc(GstMeta* meta, GstBuffer* /*buffer*/) {
        auto* node = reinterpret_cast<CustomMetaNode<T>*>(meta);
        try {
            delete node->value;
        } catch (...) {
        }
        node->value = nullptr;
    }

    static gboolean transformFunc(
        GstBuffer* transbuf,
        GstMeta* meta,
        GstBuffer* /*buffer*/,
        GQuark type,
        gpointer /*data*/) {
        if (!GST_META_TRANSFORM_IS_COPY(type)) {
            return TRUE;
        }
        try {
            auto* srcNode = reinterpret_cast<CustomMetaNode<T>*>(meta);
            if (!srcNode->value) {
                return TRUE;
            }
            auto copy = std::make_unique<T>(*srcNode->value);
            auto* dstNode = reinterpret_cast<CustomMetaNode<T>*>(
                gst_buffer_add_meta(transbuf, info(), nullptr));
            if (!dstNode) {
                return FALSE;
            }
            dstNode->value = copy.release();
            return TRUE;
        } catch (...) {
            return FALSE;
        }
    }
};

}  // namespace gps::elem
