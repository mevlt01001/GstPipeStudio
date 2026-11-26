// files.cpp – see files.h.
#include "files.h"

#include <cctype>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <random>
#include <sys/stat.h>
#include <unistd.h>

namespace gps::files {

namespace {
// Every byte, then the data on disk (fsync), then closed: a power loss after the rename finds the whole file.
bool writeAll(int fd, const std::string& bytes) {
    const char* at = bytes.data();
    size_t left = bytes.size();
    while (left > 0) {
        const ssize_t n = ::write(fd, at, left);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return false;
        at += n;
        left -= static_cast<size_t>(n);
    }
    return ::fsync(fd) == 0;
}

void syncFolder(const std::filesystem::path& folder) {
    const int fd = ::open(folder.empty() ? "." : folder.c_str(), O_RDONLY | O_DIRECTORY);
    if (fd < 0) return;
    ::fsync(fd);
    ::close(fd);
}
CreateResult write(const std::string& path, const std::string& bytes, bool replace, std::string* error) {
    namespace fs = std::filesystem;
    auto fail = [&](const std::string& why) {
        if (error) *error = "cannot write " + path + (why.empty() ? "" : ": " + why);
        return CreateResult::Failed;
    };
    std::error_code ec;
    // A link is followed: the file it points at is replaced, and the link stays.
    const fs::path target = replace && fs::exists(path, ec) ? fs::canonical(path, ec) : fs::path(path);
    if (ec) return fail(ec.message());
    if (target.has_parent_path()) fs::create_directories(target.parent_path(), ec);
    if (ec) return fail(ec.message());
    // Beside the target, so the rename stays on one file system and replaces it in one step; with the target's mode.
    const fs::path temporary = target.string() + ".tmp" + std::to_string(std::random_device{}());
    struct stat old {};
    const bool existed = replace && ::stat(target.c_str(), &old) == 0;
    const int fd = ::open(temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0666);
    if (fd < 0) return fail(std::strerror(errno));
    bool ok = (!existed || ::fchmod(fd, old.st_mode & 07777) == 0) && writeAll(fd, bytes);
    std::string why = ok ? std::string() : std::strerror(errno);
    if (::close(fd) != 0 && ok) {
        ok = false;
        why = std::strerror(errno);
    }
    if (!ok) {
        fs::remove(temporary, ec);
        return fail(why);
    }
    if (replace) {
        fs::rename(temporary, target, ec);
        if (ec) {
            std::error_code ignored;
            fs::remove(temporary, ignored);
            return fail(ec.message());
        }
    } else {
        // The hard link publishes the synced temporary file without replacing a concurrent writer's file.
        const int result = ::link(temporary.c_str(), target.c_str());
        const int reason = errno;
        fs::remove(temporary, ec);
        if (result != 0) return reason == EEXIST ? CreateResult::Exists : fail(std::strerror(reason));
    }
    syncFolder(target.parent_path());
    return CreateResult::Created;
}
}  // namespace

bool writeAtomic(const std::string& path, const std::string& bytes, std::string* error) {
    return write(path, bytes, true, error) == CreateResult::Created;
}

CreateResult createAtomic(const std::string& path, const std::string& bytes, std::string* error) {
    return write(path, bytes, false, error);
}

std::string safeName(const std::string& name) {
    std::string safe = name;
    for (char& c : safe) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '.' && c != '-') c = '_';
    }
    return safe;
}

}  // namespace gps::files
