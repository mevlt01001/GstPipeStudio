// files.h – the one way the application writes a file, and the one way it turns a name into a file name.
// A write goes to a temporary file in the same folder first, reaches the disk (fsync) and replaces the file only when
// it is complete, so a crash, a power loss or a full disk never leaves half a file behind. The file keeps its mode, and
// a link to it stays a link. Every conf, manifest, registry, library and history copy
// is written through it.
#pragma once

#include <string>

namespace gps::files {

// Writes bytes to path atomically; the folder is made when it is missing. False, with the reason, when it fails.
bool writeAtomic(const std::string& path, const std::string& bytes, std::string* error = nullptr);

enum class CreateResult { Created, Exists, Failed };

// Publishes a complete file only if the destination has not already been created.
CreateResult createAtomic(const std::string& path, const std::string& bytes, std::string* error = nullptr);

// A name as a file name: every character outside [A-Za-z0-9_.-] becomes '_'. The only sanitizer.
std::string safeName(const std::string& name);

}  // namespace gps::files
