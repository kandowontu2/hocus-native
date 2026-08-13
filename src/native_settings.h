#pragma once

#include <filesystem>

namespace hocus {

struct NativeSettings {
    bool high_fps_mode{};
};

[[nodiscard]] NativeSettings load_native_settings(
    const std::filesystem::path& path);
void write_native_settings(const std::filesystem::path& path,
                           const NativeSettings& settings);

} // namespace hocus
