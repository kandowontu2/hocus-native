#pragma once

#include <filesystem>

namespace hocus {

// Require the exact registered-v1.1 DOS executable used for this port. The
// native game does not execute it, but its presence proves that the supported
// full version is installed beside the Windows executable.
void validate_registered_hocus_exe(const std::filesystem::path& path);

} // namespace hocus
