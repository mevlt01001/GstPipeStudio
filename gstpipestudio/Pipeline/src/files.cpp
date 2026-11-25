// files.cpp – see files.h.
#include "files.h"

#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>

namespace gps::files {

bool writeAtomic(const std::string& path, const std::string& bytes, std::string* error) {
    namespace fs = std::filesystem;
    auto fail = [&](const std::string& why) {
        if (error) *error = "cannot write " + path + (why.empty() ? "" : ": " + why);
        return false;
    };
    const fs::path target(path);
    std::error_code ec;
    if (target.has_parent_path()) fs::create_directories(target.parent_path(), ec);
    if (ec) return fail(ec.message());
    // Beside the target, so the rename stays on one file system and replaces it in one step.
    const fs::path temporary = target.string() + ".tmp" + std::to_string(std::random_device{}());
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        file << bytes;
        file.flush();
        if (!file) {
            fs::remove(temporary, ec);
            return fail("");
        }
    }
    fs::rename(temporary, target, ec);
    if (ec) {
        std::error_code ignored;
        fs::remove(temporary, ignored);
        return fail(ec.message());
    }
    return true;
}

std::string safeName(const std::string& name) {
    std::string safe = name;
    for (char& c : safe) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '.' && c != '-') c = '_';
    }
    return safe;
}

}  // namespace gps::files
