// contract.cpp – see contract.h.
#include "contract.h"

QStringList fromStd(const std::vector<std::string>& list) {
    QStringList out;
    for (const std::string& item : list) out << fromStd(item);
    return out;
}

QString itemAt(const std::vector<std::string>& list, size_t i, const QString& fallback) {
    return i < list.size() ? fromStd(list[i]) : fallback;
}

QString statusText(const UpdateResult& r) {
    if (!r.valid()) return "Not validated yet";
    if (r.ok()) return r.complete() ? "Valid, pipeline complete" : "Valid, pipeline not complete yet";
    if (r.status == "error") return "Problem";
    if (r.status == "timeout") return "Timed out";
    if (r.status == "pad-missing") return "Pad did not appear";
    if (r.status == "crashed") return "Validator stopped unexpectedly";
    if (r.status == "stopped") return "Validator gave no answer";
    if (r.status == "hung") return "Validator did not answer";
    return fromStd(r.status);
}
