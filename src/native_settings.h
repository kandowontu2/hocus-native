#pragma once

#include <filesystem>

namespace hocus {

enum class WidescreenMode {
    off,
    ratio_16_9,
    ratio_21_9,
    ratio_32_9,
};

struct NativeSettings {
    bool high_fps_mode{};
    WidescreenMode widescreen_mode{WidescreenMode::off};
};

[[nodiscard]] NativeSettings load_native_settings(
    const std::filesystem::path& path);
void write_native_settings(const std::filesystem::path& path,
                           const NativeSettings& settings);

} // namespace hocus
