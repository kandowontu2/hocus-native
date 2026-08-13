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
        constexpr std::string_view key = "high_fps_mode=";
        if (line.starts_with(key)) {
            const auto value = line.substr(key.size());
            settings.high_fps_mode =
                value == "1" || value == "on" || value == "true";
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
    output << "# Hocus Native settings\n"
           << "high_fps_mode=" << (settings.high_fps_mode ? 1 : 0) << '\n';
    output.flush();
    if (!output) {
        throw std::runtime_error("Could not finish writing HOCUS_NATIVE.CFG");
    }
}

} // namespace hocus
