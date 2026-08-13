#pragma once

#include "level.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace hocus {

struct SaveSlot {
    bool occupied{};
    std::string name{"<empty>"};
    LevelId level{};
    int skill{};
    std::uint32_t score{};
};

struct HighScore {
    std::string name;
    std::uint32_t score{};
};

struct DosSettings {
    bool sound_enabled{true};
    bool music_enabled{true};
    bool joystick_enabled{};
    int game_speed{1};
    std::uint16_t joystick_x_low{};
    std::uint16_t joystick_x_center{};
    std::uint16_t joystick_x_high{};
    std::uint16_t joystick_y_low{};
    std::uint16_t joystick_y_center{};
    std::uint16_t joystick_y_high{};
    int joystick_fire_button{};
    std::array<std::uint8_t, 8> key_bindings{
        15, 16, 2, 3, 13, 14, 11, 12,
    };
    int sound_volume{15};
    int music_volume{15};
};

// HOCUS.EXE registered v1.1 writes one contiguous 0x3DE-byte block beginning
// at DS:48CA. Keep the bytes that are not part of the nine game slots opaque so
// native saves remain usable by the DOS executable and do not erase its options
// or high-score tables.
class SaveFile {
public:
    static constexpr std::size_t file_size = 0x3DE;
    static constexpr std::size_t slot_count = 9;
    static constexpr std::size_t slot_name_capacity = 25;
    static constexpr std::size_t episode_count = 4;
    static constexpr std::size_t high_score_count = 5;
    static constexpr std::size_t high_score_name_capacity = 25;

    static SaveFile blank();
    static SaveFile load(const std::filesystem::path& path);
    static SaveFile load_or_default(const std::filesystem::path& path);

    [[nodiscard]] SaveSlot slot(std::size_t index) const;
    void set_slot(std::size_t index, std::string name, LevelId level,
                  int skill, std::uint32_t score);
    void clear_slot(std::size_t index);
    [[nodiscard]] std::array<HighScore, high_score_count>
    high_scores(std::size_t episode_index) const;
    [[nodiscard]] std::optional<std::size_t>
    qualifying_high_score_rank(std::size_t episode_index,
                               std::uint32_t score) const;
    void insert_high_score(std::size_t episode_index, std::string name,
                           std::uint32_t score);
    void set_high_score_name(std::size_t episode_index, std::size_t rank,
                             std::string name);
    [[nodiscard]] DosSettings dos_settings() const;
    void set_dos_settings(const DosSettings& settings);
    void write(const std::filesystem::path& path) const;

    [[nodiscard]] const std::array<std::uint8_t, file_size>& bytes() const noexcept {
        return bytes_;
    }

private:
    std::array<std::uint8_t, file_size> bytes_{};
};

} // namespace hocus
