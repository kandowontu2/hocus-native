#include "native_settings.h"

#include <fstream>
#include <stdexcept>
#include <string>

namespace hocus {

NativeSettings load_native_settings(const std::filesystem::path& path) {
    NativeSettings settings;
    std::ifstream input(path);
    if (!input) {
        if (std::filesystem::exists(path)) {
            throw std::runtime_error("Could not read HOCUS_NATIVE.CFG");
        }
        return settings;
    }

    std::string line;
    while (std::getline(input, line)) {
        constexpr std::string_view high_fps_key = "high_fps_mode=";
        constexpr std::string_view widescreen_key = "widescreen_mode=";
        if (line.starts_with(high_fps_key)) {
            const auto value = line.substr(high_fps_key.size());
            settings.high_fps_mode =
                value == "1" || value == "on" || value == "true";
        } else if (line.starts_with(widescreen_key)) {
            const auto value = line.substr(widescreen_key.size());
            if (value == "1" || value == "on" || value == "true" ||
                value == "16:9") {
                settings.widescreen_mode = WidescreenMode::ratio_16_9;
            } else if (value == "2" || value == "21:9") {
                settings.widescreen_mode = WidescreenMode::ratio_21_9;
            } else if (value == "3" || value == "32:9") {
                settings.widescreen_mode = WidescreenMode::ratio_32_9;
            } else {
                settings.widescreen_mode = WidescreenMode::off;
            }
        }
    }
    if (!input.eof()) {
        throw std::runtime_error("Could not finish reading HOCUS_NATIVE.CFG");
    }
    return settings;
}

void write_native_settings(const std::filesystem::path& path,
                           const NativeSettings& settings) {
    std::ofstream output(path, std::ios::trunc);
    if (!output) {
        throw std::runtime_error("Could not write HOCUS_NATIVE.CFG");
    }
    const char* widescreen_value = "off";
    switch (settings.widescreen_mode) {
    case WidescreenMode::ratio_16_9: widescreen_value = "16:9"; break;
    case WidescreenMode::ratio_21_9: widescreen_value = "21:9"; break;
    case WidescreenMode::ratio_32_9: widescreen_value = "32:9"; break;
    case WidescreenMode::off: break;
    }
    output << "# Hocus Native settings\n"
           << "high_fps_mode=" << (settings.high_fps_mode ? 1 : 0) << '\n'
           << "widescreen_mode=" << widescreen_value << '\n';
    output.flush();
    if (!output) {
        throw std::runtime_error("Could not finish writing HOCUS_NATIVE.CFG");
    }
}

} // namespace hocus
