#include "save.h"

#include <windows.h>

#include <algorithm>
#include <fstream>
#include <stdexcept>

namespace hocus {
namespace {

constexpr std::size_t episode_offset = 0x1E; // DS:48E8, 9 uint16s
constexpr std::size_t sound_enabled_offset = 0x00; // DS:48CA
constexpr std::size_t music_enabled_offset = 0x02; // DS:48CC
constexpr std::size_t joystick_enabled_offset = 0x04; // DS:48CE
constexpr std::size_t game_speed_offset = 0x06; // DS:48D0
constexpr std::size_t joystick_x_low_offset = 0x08; // DS:48D2
constexpr std::size_t joystick_x_center_offset = 0x0A; // DS:48D4
constexpr std::size_t joystick_x_high_offset = 0x0C; // DS:48D6
constexpr std::size_t joystick_y_low_offset = 0x0E; // DS:48D8
constexpr std::size_t joystick_y_center_offset = 0x10; // DS:48DA
constexpr std::size_t joystick_y_high_offset = 0x12; // DS:48DC
constexpr std::size_t joystick_fire_button_offset = 0x14; // DS:48DE
constexpr std::size_t key_binding_offset = 0x16; // DS:48E0, 8 bytes
constexpr std::size_t level_offset = 0x30;   // DS:48FA, 9 uint16s
constexpr std::size_t skill_offset = 0x42;   // DS:490C, 9 uint16s
constexpr std::size_t name_offset = 0x54;    // DS:491E, 9 * 26 chars
constexpr std::size_t name_stride = 26;
constexpr std::size_t slot_flag_offset = 0x13E; // DS:4A08, 9 bytes
constexpr std::size_t score_offset = 0x148;     // DS:4A12, 9 uint32s
constexpr std::size_t high_score_name_offset = 0x16C; // DS:4A36
constexpr std::size_t high_score_name_stride = 26;
constexpr std::size_t high_score_score_offset = 0x374; // DS:4C3E
constexpr std::size_t sound_volume_offset = 0x3D8; // DS:4CA2
constexpr std::size_t music_volume_offset = 0x3DA; // DS:4CA4
constexpr std::uint16_t empty_episode = 0xFFFF;

std::uint16_t read_u16(
    const std::array<std::uint8_t, SaveFile::file_size>& bytes,
    const std::size_t offset) {
    return static_cast<std::uint16_t>(bytes[offset]) |
           static_cast<std::uint16_t>(bytes[offset + 1] << 8U);
}

std::uint32_t read_u32(
    const std::array<std::uint8_t, SaveFile::file_size>& bytes,
    const std::size_t offset) {
    return static_cast<std::uint32_t>(bytes[offset]) |
           (static_cast<std::uint32_t>(bytes[offset + 1]) << 8U) |
           (static_cast<std::uint32_t>(bytes[offset + 2]) << 16U) |
           (static_cast<std::uint32_t>(bytes[offset + 3]) << 24U);
}

void write_u16(std::array<std::uint8_t, SaveFile::file_size>& bytes,
               const std::size_t offset, const std::uint16_t value) {
    bytes[offset] = static_cast<std::uint8_t>(value & 0xFFU);
    bytes[offset + 1] = static_cast<std::uint8_t>(value >> 8U);
}

void write_u32(std::array<std::uint8_t, SaveFile::file_size>& bytes,
               const std::size_t offset, const std::uint32_t value) {
    bytes[offset] = static_cast<std::uint8_t>(value & 0xFFU);
    bytes[offset + 1] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
    bytes[offset + 2] = static_cast<std::uint8_t>((value >> 16U) & 0xFFU);
    bytes[offset + 3] = static_cast<std::uint8_t>(value >> 24U);
}

void check_slot_index(const std::size_t index) {
    if (index >= SaveFile::slot_count) {
        throw std::out_of_range("Hocus save slot must be in 0..8");
    }
}

void check_episode_index(const std::size_t index) {
    if (index >= SaveFile::episode_count) {
        throw std::out_of_range("Hocus episode index must be in 0..3");
    }
}

std::string printable_name(std::string name) {
    std::string result;
    result.reserve(std::min(name.size(), SaveFile::slot_name_capacity));
    for (const unsigned char character : name) {
        if (character >= 32 && character <= 126) {
            result.push_back(static_cast<char>(character));
        }
        if (result.size() == SaveFile::slot_name_capacity) {
            break;
        }
    }
    return result;
}

} // namespace

SaveFile SaveFile::blank() {
    SaveFile result;
    result.set_dos_settings({});
    for (std::size_t index = 0; index < slot_count; ++index) {
        result.clear_slot(index);
        result.bytes_[slot_flag_offset + index] = 1;
    }
    constexpr std::array<const char*, high_score_count> default_names = {
        "Hocus Pocus", "Andre Foucault", "Jamie Cook", "Karim Sultan",
        "Chris tenDen",
    };
    constexpr std::array<std::uint32_t, high_score_count> default_scores = {
        1000000, 800000, 600000, 400000, 200000,
    };
    for (std::size_t episode = 0; episode < episode_count; ++episode) {
        for (std::size_t rank = 0; rank < high_score_count; ++rank) {
            const auto name_base = high_score_name_offset +
                (episode * high_score_count + rank) * high_score_name_stride;
            std::copy(default_names[rank],
                      default_names[rank] + std::char_traits<char>::length(
                          default_names[rank]),
                      result.bytes_.begin() +
                          static_cast<std::ptrdiff_t>(name_base));
            write_u32(result.bytes_, high_score_score_offset +
                          (episode * high_score_count + rank) * 4,
                      default_scores[rank]);
        }
    }
    return result;
}

SaveFile SaveFile::load(const std::filesystem::path& path) {
    SaveFile result;
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("Could not open HOCUS.SAV");
    }
    input.read(reinterpret_cast<char*>(result.bytes_.data()),
               static_cast<std::streamsize>(result.bytes_.size()));
    if (input.gcount() != static_cast<std::streamsize>(result.bytes_.size()) ||
        input.peek() != std::ifstream::traits_type::eof()) {
        throw std::runtime_error("HOCUS.SAV is not the registered-v1.1 990-byte format");
    }
    return result;
}

SaveFile SaveFile::load_or_default(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) {
        return blank();
    }
    return load(path);
}

SaveSlot SaveFile::slot(const std::size_t index) const {
    check_slot_index(index);
    const auto stored_episode = read_u16(bytes_, episode_offset + index * 2);
    SaveSlot result;
    result.name.clear();

    // 06B8:25CC/2B8B draw the name table even when the episode word is the
    // FFFF empty sentinel.  Blank saves store the visible "<empty>" marker in
    // that field; keep it available to the slot-screen raster while occupied
    // remains false.
    const auto base = name_offset + index * name_stride;
    for (std::size_t character = 0; character < slot_name_capacity; ++character) {
        const auto value = bytes_[base + character];
        if (value == 0) {
            break;
        }
        result.name.push_back(value >= 32 && value <= 126
                                  ? static_cast<char>(value)
                                  : '?');
    }
    if (stored_episode == empty_episode) {
        return result;
    }

    const auto stored_level = read_u16(bytes_, level_offset + index * 2);
    const auto stored_skill = read_u16(bytes_, skill_offset + index * 2);
    // Corrupt entries are presented as empty instead of allowing an invalid
    // table index to reach the level loader.
    if (stored_episode >= 4 || stored_level >= 9 || stored_skill >= 3) {
        return result;
    }

    result.occupied = true;
    result.level = {static_cast<int>(stored_episode) + 1,
                    static_cast<int>(stored_level) + 1};
    result.skill = static_cast<int>(stored_skill);
    result.score = read_u32(bytes_, score_offset + index * 4);

    return result;
}

void SaveFile::set_slot(const std::size_t index, std::string name,
                        const LevelId level, const int skill,
                        const std::uint32_t score) {
    check_slot_index(index);
    if (level.episode < 1 || level.episode > 4 ||
        level.number < 1 || level.number > 9 || skill < 0 || skill > 2) {
        throw std::out_of_range("Invalid Hocus save-game state");
    }

    write_u16(bytes_, episode_offset + index * 2,
              static_cast<std::uint16_t>(level.episode - 1));
    write_u16(bytes_, level_offset + index * 2,
              static_cast<std::uint16_t>(level.number - 1));
    write_u16(bytes_, skill_offset + index * 2,
              static_cast<std::uint16_t>(skill));
    write_u32(bytes_, score_offset + index * 4, score);

    name = printable_name(std::move(name));
    const auto base = name_offset + index * name_stride;
    std::fill_n(bytes_.begin() + static_cast<std::ptrdiff_t>(base), name_stride,
                std::uint8_t{});
    std::copy(name.begin(), name.end(),
              bytes_.begin() + static_cast<std::ptrdiff_t>(base));
}

void SaveFile::clear_slot(const std::size_t index) {
    check_slot_index(index);
    write_u16(bytes_, episode_offset + index * 2, empty_episode);
    write_u16(bytes_, level_offset + index * 2, 0);
    write_u16(bytes_, skill_offset + index * 2, 0);
    write_u32(bytes_, score_offset + index * 4, 0);
    const auto base = name_offset + index * name_stride;
    std::fill_n(bytes_.begin() + static_cast<std::ptrdiff_t>(base), name_stride,
                std::uint8_t{});
    constexpr char empty_name[] = "<empty>";
    std::copy(std::begin(empty_name), std::end(empty_name) - 1,
              bytes_.begin() + static_cast<std::ptrdiff_t>(base));
}

std::array<HighScore, SaveFile::high_score_count>
SaveFile::high_scores(const std::size_t episode_index) const {
    check_episode_index(episode_index);
    std::array<HighScore, high_score_count> result;
    for (std::size_t rank = 0; rank < high_score_count; ++rank) {
        const auto flat_index = episode_index * high_score_count + rank;
        const auto name_base = high_score_name_offset +
            flat_index * high_score_name_stride;
        for (std::size_t character = 0;
             character < high_score_name_capacity; ++character) {
            const auto value = bytes_[name_base + character];
            if (value == 0) {
                break;
            }
            result[rank].name.push_back(value >= 32 && value <= 126
                                            ? static_cast<char>(value)
                                            : '?');
        }
        result[rank].score = read_u32(
            bytes_, high_score_score_offset + flat_index * 4);
    }
    return result;
}

std::optional<std::size_t> SaveFile::qualifying_high_score_rank(
    const std::size_t episode_index, const std::uint32_t score) const {
    const auto scores = high_scores(episode_index);
    for (std::size_t rank = 0; rank < scores.size(); ++rank) {
        // 06B8:1BCE compares the high words signed, then (only when they are
        // equal) compares the low words unsigned. XOR-biasing the sign bit
        // gives that exact 32-bit signed ordering without relying on an
        // implementation-defined unsigned-to-signed conversion.
        if ((score ^ 0x80000000U) >
            (scores[rank].score ^ 0x80000000U)) {
            return rank;
        }
    }
    return std::nullopt;
}

void SaveFile::insert_high_score(const std::size_t episode_index,
                                 std::string name,
                                 const std::uint32_t score) {
    check_episode_index(episode_index);
    const auto rank = qualifying_high_score_rank(episode_index, score);
    if (!rank) {
        return;
    }
    auto scores = high_scores(episode_index);
    for (std::size_t index = scores.size() - 1; index > *rank; --index) {
        scores[index] = scores[index - 1];
    }
    scores[*rank] = {printable_name(std::move(name)), score};

    for (std::size_t index = 0; index < scores.size(); ++index) {
        const auto flat_index = episode_index * high_score_count + index;
        const auto name_base = high_score_name_offset +
            flat_index * high_score_name_stride;
        std::fill_n(bytes_.begin() + static_cast<std::ptrdiff_t>(name_base),
                    high_score_name_stride, std::uint8_t{});
        std::copy(scores[index].name.begin(), scores[index].name.end(),
                  bytes_.begin() + static_cast<std::ptrdiff_t>(name_base));
        write_u32(bytes_, high_score_score_offset + flat_index * 4,
                  scores[index].score);
    }
}

void SaveFile::set_high_score_name(const std::size_t episode_index,
                                   const std::size_t rank,
                                   std::string name) {
    check_episode_index(episode_index);
    if (rank >= high_score_count) {
        throw std::out_of_range("Hocus high-score rank must be in 0..4");
    }
    name = printable_name(std::move(name));
    const auto flat_index = episode_index * high_score_count + rank;
    const auto name_base = high_score_name_offset +
                           flat_index * high_score_name_stride;
    std::fill_n(bytes_.begin() + static_cast<std::ptrdiff_t>(name_base),
                high_score_name_stride, std::uint8_t{});
    std::copy(name.begin(), name.end(),
              bytes_.begin() + static_cast<std::ptrdiff_t>(name_base));
}

DosSettings SaveFile::dos_settings() const {
    DosSettings result;
    result.sound_enabled = read_u16(bytes_, sound_enabled_offset) != 0;
    result.music_enabled = read_u16(bytes_, music_enabled_offset) != 0;
    result.joystick_enabled = read_u16(bytes_, joystick_enabled_offset) != 0;
    result.game_speed = std::clamp<int>(read_u16(bytes_, game_speed_offset),
                                        0, 2);
    result.joystick_x_low = read_u16(bytes_, joystick_x_low_offset);
    result.joystick_x_center = read_u16(bytes_, joystick_x_center_offset);
    result.joystick_x_high = read_u16(bytes_, joystick_x_high_offset);
    result.joystick_y_low = read_u16(bytes_, joystick_y_low_offset);
    result.joystick_y_center = read_u16(bytes_, joystick_y_center_offset);
    result.joystick_y_high = read_u16(bytes_, joystick_y_high_offset);
    result.joystick_fire_button = std::clamp<int>(
        read_u16(bytes_, joystick_fire_button_offset), 0, 1);
    for (std::size_t index = 0; index < result.key_bindings.size(); ++index) {
        const auto binding = bytes_[key_binding_offset + index];
        result.key_bindings[index] = binding < 18 ? binding : 0;
    }
    result.sound_volume = std::clamp<int>(
        read_u16(bytes_, sound_volume_offset) / 16, 0, 15);
    result.music_volume = std::clamp<int>(
        read_u16(bytes_, music_volume_offset) / 16, 0, 15);
    return result;
}

void SaveFile::set_dos_settings(const DosSettings& settings) {
    write_u16(bytes_, sound_enabled_offset, settings.sound_enabled ? 1 : 0);
    write_u16(bytes_, music_enabled_offset, settings.music_enabled ? 1 : 0);
    write_u16(bytes_, joystick_enabled_offset,
              settings.joystick_enabled ? 1 : 0);
    write_u16(bytes_, game_speed_offset,
              static_cast<std::uint16_t>(std::clamp(settings.game_speed, 0, 2)));
    write_u16(bytes_, joystick_x_low_offset, settings.joystick_x_low);
    write_u16(bytes_, joystick_x_center_offset, settings.joystick_x_center);
    write_u16(bytes_, joystick_x_high_offset, settings.joystick_x_high);
    write_u16(bytes_, joystick_y_low_offset, settings.joystick_y_low);
    write_u16(bytes_, joystick_y_center_offset, settings.joystick_y_center);
    write_u16(bytes_, joystick_y_high_offset, settings.joystick_y_high);
    write_u16(bytes_, joystick_fire_button_offset,
              static_cast<std::uint16_t>(
                  std::clamp(settings.joystick_fire_button, 0, 1)));
    for (std::size_t index = 0; index < settings.key_bindings.size(); ++index) {
        bytes_[key_binding_offset + index] =
            std::min<std::uint8_t>(settings.key_bindings[index], 17);
    }
    const auto encoded_volume = [](const int step) {
        return static_cast<std::uint16_t>(std::clamp(step, 0, 15) * 16 + 15);
    };
    write_u16(bytes_, sound_volume_offset,
              encoded_volume(settings.sound_volume));
    write_u16(bytes_, music_volume_offset,
              encoded_volume(settings.music_volume));
}

void SaveFile::write(const std::filesystem::path& path) const {
    auto temporary = path;
    temporary += L".native.tmp";
    auto backup = path;
    backup += L".bak";

    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) {
            throw std::runtime_error("Could not create temporary HOCUS.SAV");
        }
        output.write(reinterpret_cast<const char*>(bytes_.data()),
                     static_cast<std::streamsize>(bytes_.size()));
        output.flush();
        if (!output) {
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
            throw std::runtime_error("Could not finish writing HOCUS.SAV");
        }
    }

    std::error_code copy_error;
    if (std::filesystem::exists(path) && !std::filesystem::exists(backup)) {
        std::filesystem::copy_file(path, backup,
                                   std::filesystem::copy_options::none,
                                   copy_error);
        if (copy_error) {
            std::filesystem::remove(temporary, copy_error);
            throw std::runtime_error("Could not back up the original HOCUS.SAV");
        }
    }

    if (!MoveFileExW(temporary.c_str(), path.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        throw std::runtime_error("Could not replace HOCUS.SAV");
    }
}

} // namespace hocus
