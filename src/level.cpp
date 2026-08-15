#include "level.h"

#include "sprite.h"
#include "ui.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <span>
#include <string>
#include <stdexcept>
#include <utility>

namespace hocus {
namespace {

constexpr int map_width = 240;
constexpr int map_height = 60;
constexpr int tile_size = 16;
constexpr int viewport_height = gameplay_viewport_height;
constexpr int player_width = 24;
constexpr int player_height = 32;
// The collision helpers at 0BA5:0006 and 0BA5:00D3 use only VGA byte
// columns player_x+2..player_x+3 and scanlines player_y+6..player_y+25.
// One VGA byte is four native pixels.
constexpr int player_hitbox_x_offset = 8;
constexpr int player_hitbox_y_offset = 6;
constexpr int player_hitbox_width = 8;
constexpr int player_hitbox_height = 20;
constexpr int bullet_cell_width = 16;
constexpr int bullet_height = 15;
constexpr int horizontal_shot_frame = 11;
constexpr int vertical_shot_frame = 12;
constexpr int horizontal_laser_frame = 13;
constexpr int vertical_laser_frame = 14;
constexpr int enemy_spawn_ticks = 20;
constexpr std::uint16_t empty_event = 30000;
constexpr std::array<int, 9> visual_variant_by_level = {
    0, 0, 1, 1, 2, 2, 3, 3, 3,
};
// Registered-v1.1 table at file offset 0x21BDE. Values select one of archive
// entries 600..609; asset 599 is the separate completion fanfare.
constexpr std::array<std::array<int, 9>, 4> music_by_level = {{
    {{3, 3, 4, 4, 0, 0, 2, 2, 2}},
    {{9, 9, 7, 7, 8, 8, 1, 1, 1}},
    {{4, 4, 0, 0, 7, 7, 6, 6, 6}},
    {{2, 2, 8, 8, 9, 9, 5, 5, 5}},
}};

// HOCUS.EXE registered v1.1 DS:2BB6-2BDC. Selector 99 uses four staged
// positions in E4L9 and changes stage below the following health thresholds.
constexpr std::array<int, 4> boss4_x_units = {128, 128, 113, 128};
constexpr std::array<int, 4> boss4_rows = {54, 48, 42, 36};
constexpr std::array<int, 4> boss4_spawn_cells = {
    13088, 11648, 10193, 8768,
};
constexpr std::array<bool, 4> boss4_faces_west = {true, true, false, true};
constexpr std::array<int, 4> boss4_health = {800, 600, 400, 200};
constexpr std::array<int, 4> boss4_next_threshold = {600, 400, 200, -1};

struct ElevatorTiles {
    int left;
    int right;
};

// HOCUS.EXE registered v1.1 file offset 0x21C26 / DS:2B16. Episodes one
// and two do not use movable elevator tiles.
constexpr std::array<std::array<ElevatorTiles, 9>, 4> elevator_tiles = {{
    {{{-1, -1}, {-1, -1}, {-1, -1}, {-1, -1}, {-1, -1},
       {-1, -1}, {-1, -1}, {-1, -1}, {-1, -1}}},
    {{{-1, -1}, {-1, -1}, {-1, -1}, {-1, -1}, {-1, -1},
       {-1, -1}, {-1, -1}, {-1, -1}, {-1, -1}}},
    {{{78, 79}, {78, 79}, {62, 63}, {62, 63}, {40, 41},
       {40, 41}, {85, 86}, {85, 86}, {85, 86}}},
    {{{58, 59}, {58, 59}, {122, 123}, {122, 123}, {70, 71},
       {70, 71}, {60, 61}, {60, 61}, {60, 61}}},
}};

struct ItemDefinition {
    int score;
    int heal;
    int firepower;
    int type;
};

// HOCUS.EXE registered v1.1 DS:2604, 23 records of 42 bytes. Only the
// gameplay fields consumed by 0BA5:39F3 are represented here.
constexpr std::array<ItemDefinition, 23> item_definitions = {{
    {100, 0, 0, 0}, {250, 0, 0, 0}, {500, 0, 0, 0},
    {1000, 0, 0, 0}, {0, 10, 0, 0}, {0, 0, 0, 1},
    {0, 0, 0, 2}, {0, 0, 1, 0}, {0, 0, 0, 3},
    {0, 0, 0, 4}, {0, 0, 0, 5}, {0, 0, 0, 6},
    {0, 0, 0, 7}, {0, 0, 0, 8}, {0, 0, 0, 9},
    {0, 0, 0, 10}, {0, 0, 0, 11}, {0, 0, 0, 12},
    {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0},
    {0, 0, 0, 0}, {0, 0, 0, 0},
}};

// HOCUS.EXE registered v1.1 DS:2C36. The selected skill indexes the signed
// health delta; native state stores the corresponding positive damage amount.
constexpr std::array<int, 3> damage_by_skill = {4, 12, 16};

// HOCUS.EXE registered v1.1 file offset 0x21D20 / DS:2C10.
// 0BA5:5FBF selects this table, and 0BA5:6025 consumes one signed value/tick.
constexpr std::array<int, 19> normal_jump = {
    -16, -8, -8, -4, -4, -4, -2, -1, -1, 0,
      1,  1,  2,  4,  4,  4,  8,  8, 16,
};
constexpr int normal_jump_apex = 9;
constexpr std::array<int, 25> super_jump = {
    -16, -16, -16, -16, -8, -8, -4, -4, -4, -2, -1, -1, 0,
      1,   1,   2,   4,  4,  4,  8,  8, 16, 16, 16, 16,
};
constexpr int super_jump_apex = 12;

std::uint16_t u16(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    if (offset + 2 > bytes.size()) {
        throw std::runtime_error("Truncated level record");
    }
    return static_cast<std::uint16_t>(bytes[offset]) |
           (static_cast<std::uint16_t>(bytes[offset + 1]) << 8U);
}

int floor_div(int numerator, int denominator) noexcept {
    int quotient = numerator / denominator;
    const int remainder = numerator % denominator;
    if (remainder != 0 && ((remainder < 0) != (denominator < 0))) {
        --quotient;
    }
    return quotient;
}

void blit_solid_mask(const DecodedImage& source, DecodedImage& destination,
                     const int x, const int y,
                     const std::uint32_t colour) {
    // 0BA5:2896 follows the same compressed opacity runs as the normal sprite
    // blitter but sources every opaque byte from the one-byte 0x70 buffer
    // initialized at 0BA5:4A8D-4AC1.
    for (int source_y = 0; source_y < source.height; ++source_y) {
        const int destination_y = y + source_y;
        if (destination_y < 0 || destination_y >= destination.height) {
            continue;
        }
        for (int source_x = 0; source_x < source.width; ++source_x) {
            const int destination_x = x + source_x;
            if (destination_x < 0 || destination_x >= destination.width) {
                continue;
            }
            const auto source_index = static_cast<std::size_t>(source_y) *
                                      source.width + source_x;
            if (!source.opacity.empty() && source.opacity[source_index] == 0) {
                continue;
            }
            destination.pixels[static_cast<std::size_t>(destination_y) *
                                   destination.width + destination_x] = colour;
        }
    }
}

LevelId checked_level(LevelId level) {
    if (level.episode < 1 || level.episode > 4 ||
        level.number < 1 || level.number > 9) {
        throw std::out_of_range("Hocus level must be in E1L1..E4L9");
    }
    return level;
}

} // namespace

GameLevel::GameLevel(const DatArchive& archive, LevelId level,
                     const int initial_score, const int skill,
                     const bool show_crystal_tip, const int viewport_width)
    : level_id_(checked_level(level)),
      font_mask_(archive.read(0)),                   // FONT.MSK
      viewport_width_(viewport_width) {
    if (initial_score < 0) {
        throw std::out_of_range("Hocus score cannot be negative");
    }
    if (skill < 0 || skill >= static_cast<int>(damage_by_skill.size())) {
        throw std::out_of_range("Hocus skill must be easy, moderate, or hard");
    }
    if (viewport_width_ != original_frame_width &&
        viewport_width_ != widescreen_frame_width &&
        viewport_width_ != ultrawide_frame_width &&
        viewport_width_ != super_ultrawide_frame_width) {
        throw std::out_of_range("Unsupported Hocus gameplay viewport width");
    }
    skill_ = skill;
    damage_amount_ = damage_by_skill[static_cast<std::size_t>(skill)];
    progress_.score = initial_score;
    level_index_ = (level_id_.episode - 1) * 9 + level_id_.number - 1;
    const int visual_variant = (level_id_.episode - 1) * 4 +
        visual_variant_by_level[level_id_.number - 1];
    const auto elevator =
        elevator_tiles[level_id_.episode - 1][level_id_.number - 1];
    elevator_left_tile_ = elevator.left;
    elevator_right_tile_ = elevator.right;
    // Registered-v1.1 tables at file 0x21B7A and 0x21ADA select the
    // zero-based BACK/TILES variant for each level.
    backdrop_asset_index_ = 89 + visual_variant;
    tileset_asset_index_ = 105 + visual_variant;
    music_asset_index_ = 600 +
        music_by_level[level_id_.episode - 1][level_id_.number - 1];
    // 0BA5:4B6C programs GAMEPAL.PAL into DAC entries 0..127, then 4B7A
    // programs the selected BACKxx.PAL into entries 128..255. The embedded
    // PCX palettes are file-conversion aids and contain black placeholders at
    // unused indices, so every gameplay image must resolve indices through
    // this active composite palette.
    auto active_palette_bytes = archive.read(7); // GAMEPAL.PAL
    const auto backdrop_palette = archive.read(73 + visual_variant);
    active_palette_bytes.insert(active_palette_bytes.end(),
                                backdrop_palette.begin(),
                                backdrop_palette.end());
    const auto active_palette = decode_vga_palette(active_palette_bytes);
    backdrop_ = decode_pcx(archive.read(backdrop_asset_index_), active_palette);
    tiles_ = decode_pcx(archive.read(tileset_asset_index_), active_palette);
    background_layer_ = archive.read(455 + level_index_); // .009
    main_layer_ = archive.read(491 + level_index_);       // .010
    // .011 is the insert-data copy restored by switch mode one and events
    // 56..80. 0BA5:4902 also allocates a zero-filled per-cell transition
    // layer; values one and two request removal and restoration respectively.
    original_main_layer_ = archive.read(527 + level_index_);
    pending_gate_layer_.assign(map_width * map_height, 0);

    // Startup loads RANDOM.DAT at DS:711A. The helper at 1392:0617 advances
    // DS:7118 first, wraps after 999, and returns table[index] % argument.
    const auto raw_random = archive.read(6);
    if (raw_random.size() != random_table_.size() * 2) {
        throw std::runtime_error("Unexpected RANDOM.DAT dimensions");
    }
    for (std::size_t index = 0; index < random_table_.size(); ++index) {
        random_table_[index] = static_cast<std::int16_t>(
            u16(raw_random, index * 2));
    }

    const auto raw_events = archive.read(563 + level_index_); // .012
    if (raw_events.size() != map_width * map_height * 2) {
        throw std::runtime_error("Unexpected level event-layer dimensions");
    }
    event_layer_.reserve(map_width * map_height);
    for (std::size_t offset = 0; offset < raw_events.size(); offset += 2) {
        event_layer_.push_back(u16(raw_events, offset));
    }

    const auto animation_settings = archive.read(167 + level_index_); // .001
    if (animation_settings.empty()) {
        throw std::runtime_error("Missing level animation settings");
    }
    // The original loads .001 at DS:6D68 and writes its first byte into the
    // background layer when an item is removed (0BA5:3EE9-3EF8).
    clear_background_tile_ = animation_settings.front();
    if (animation_settings.size() != 4 + tile_animations_.size() * 3) {
        throw std::runtime_error("Truncated level animation settings");
    }
    // The projectile dispatcher compares collision tiles with byte 3 at
    // DS:6D6B, then replaces the matching main-layer cell with 0xFF.
    breakable_main_tile_ = animation_settings[3];
    switch_off_tile_ = animation_settings[1];
    switch_on_tile_ = animation_settings[2];
    for (std::size_t index = 0; index < tile_animations_.size(); ++index) {
        const auto base = 4 + index * 3;
        tile_animations_[index] = {
            animation_settings[base],
            animation_settings[base + 1],
            animation_settings[base + 2],
        };
    }

    const auto raw_teleporters = archive.read(239 + level_index_); // .003
    if (raw_teleporters.size() != teleporters_.size() * 4) {
        throw std::runtime_error("Unexpected level teleporter-table dimensions");
    }
    for (std::size_t index = 0; index < teleporters_.size(); ++index) {
        teleporters_[index].trigger_cell = u16(raw_teleporters, index * 4);
        teleporters_[index].destination_cell =
            u16(raw_teleporters, index * 4 + 2);
    }

    const auto raw_switches = archive.read(275 + level_index_); // .004
    constexpr std::size_t switch_record_size = 22;
    if (raw_switches.size() != switches_.size() * switch_record_size) {
        throw std::runtime_error("Unexpected level switch-table dimensions");
    }
    for (std::size_t index = 0; index < switches_.size(); ++index) {
        const auto base = index * switch_record_size;
        auto& record = switches_[index];
        record.mode = raw_switches[base];
        for (std::size_t dependency = 0; dependency < 4; ++dependency) {
            record.dependencies[dependency] =
                u16(raw_switches, base + 2 + dependency * 2);
            record.expected_tiles[dependency] = raw_switches[base + 10 + dependency];
        }
        record.first_column = u16(raw_switches, base + 14);
        record.first_row = u16(raw_switches, base + 16);
        record.last_column = u16(raw_switches, base + 18);
        record.last_row = u16(raw_switches, base + 20);
    }

    constexpr std::size_t keyhole_record_size = 12;
    const auto load_keyholes = [this, &archive](
            const int asset, auto& records) {
        const auto raw = archive.read(asset);
        if (raw.size() != records.size() * keyhole_record_size) {
            throw std::runtime_error(
                "Unexpected level keyhole-table dimensions");
        }
        for (std::size_t index = 0; index < records.size(); ++index) {
            const auto base = index * keyhole_record_size;
            auto& record = records[index];
            record.required_key = u16(raw, base);
            record.replacement_tile = raw[base + 2];
            record.first_column = u16(raw, base + 4);
            record.first_row = u16(raw, base + 6);
            record.last_column = u16(raw, base + 8);
            record.last_row = u16(raw, base + 10);
        }
    };
    // 0BA5:41AA handles .005 as events 56..80 (insert/restore), while
    // 0BA5:42F7 handles .006 as events 81..105 (remove). Both official
    // tables use the same 25 x 12-byte record layout.
    load_keyholes(311 + level_index_, insertion_gates_); // .005
    load_keyholes(347 + level_index_, keyholes_);        // .006

    const auto raw_enemy_definitions = archive.read(383 + level_index_); // .007
    constexpr std::size_t enemy_definition_size = 24;
    if (raw_enemy_definitions.size() !=
        enemy_definitions_.size() * enemy_definition_size) {
        throw std::runtime_error("Unexpected level enemy-definition dimensions");
    }
    for (std::size_t index = 0; index < enemy_definitions_.size(); ++index) {
        const auto base = index * enemy_definition_size;
        auto& definition = enemy_definitions_[index];
        for (std::size_t word = 0; word < definition.words.size(); ++word) {
            definition.words[word] = static_cast<std::int16_t>(
                u16(raw_enemy_definitions, base + word * 2));
        }
        // 0BA5:04D3 indexes the local sprite as type+4, but word 0 selects
        // which source sprite was loaded there. Word 1 is the extra-hit
        // counter and word 11 drives both spawn setup (071B) and the active
        // movement dispatch (1478).
        definition.sprite_asset = definition.words[0];
        definition.health = definition.words[1];
        definition.active_behaviour = static_cast<std::int16_t>(
            u16(raw_enemy_definitions, base + 22));
        definition.available = definition.sprite_asset >= 0;
    }

    const auto raw_enemy_triggers = archive.read(419 + level_index_); // .008
    constexpr std::size_t enemy_trigger_size = 32;
    if (raw_enemy_triggers.size() !=
        enemy_triggers_.size() * enemy_trigger_size) {
        throw std::runtime_error("Unexpected level enemy-trigger dimensions");
    }
    for (std::size_t index = 0; index < enemy_triggers_.size(); ++index) {
        const auto base = index * enemy_trigger_size;
        auto& trigger = enemy_triggers_[index];
        for (std::size_t slot = 0; slot < trigger.types.size(); ++slot) {
            trigger.types[slot] = u16(raw_enemy_triggers, base + slot * 2);
            trigger.spawn_cells[slot] =
                u16(raw_enemy_triggers, base + 16 + slot * 2);
        }
    }

    const auto raw_messages = archive.read(203 + level_index_); // .002
    constexpr std::size_t message_size = 504;
    constexpr std::size_t line_size = 50;
    if (raw_messages.size() != message_size * 10 || font_mask_.size() != 720) {
        throw std::runtime_error("Unexpected level message or font data");
    }
    for (std::size_t record = 0; record < 10; ++record) {
        const auto base = record * message_size;
        MessageRecord message{static_cast<int>(u16(raw_messages, base)),
                              static_cast<int>(u16(raw_messages, base + 2)), {}};
        if (message.tile_x == 0xFFFF || message.tile_y == 0xFFFF) {
            continue;
        }
        for (std::size_t line = 0; line < 10; ++line) {
            const auto start = base + 4 + line * line_size;
            std::string text;
            for (std::size_t i = 0; i < line_size && raw_messages[start + i]; ++i) {
                text.push_back(static_cast<char>(raw_messages[start + i]));
            }
            // 0BA5:38B6 stops on the first zero-width line in each ten-line
            // record; later fixed-size fields are padding, not extra lines.
            if (text.empty()) {
                break;
            }
            message.lines.push_back(std::move(text));
        }
        messages_.push_back(std::move(message));
    }

    for (const auto event : event_layer_) {
        if (event < item_definitions.size() && item_definitions[event].score != 0) {
            ++progress_.total_treasures;
        }
        if (event == 5) {
            ++progress_.total_crystals;
        }
    }

    if (backdrop_.width != 320 || backdrop_.height != 200 ||
        tiles_.width != 320 || tiles_.height != 200 ||
        background_layer_.size() != map_width * map_height ||
        main_layer_.size() != map_width * map_height ||
        original_main_layer_.size() != map_width * map_height) {
        throw std::runtime_error("Unexpected level image or layer dimensions");
    }

    hud_stuff_ = decode_planar_img(archive.read(11), active_palette); // HUDSTUFF.IMG
    hud_ = decode_planar_img(archive.read(12), active_palette); // NEW_HUD.IMG
    crystal_tip_ = decode_planar_img(archive.read(16), active_palette); // CRYSTAL.IMG
    if (hud_stuff_.width != 112 || hud_stuff_.height != 12 ||
        hud_.width != 320 || hud_.height != 40 ||
        crystal_tip_.width != 220 || crystal_tip_.height != 68) {
        throw std::runtime_error("Unexpected HUD dimensions");
    }
    // The second argument to 0BA5:453A is true only for a newly started E1L1
    // campaign (0548:0633-0649). Restarts, restores, and demos pass zero.
    crystal_tip_visible_ = show_crystal_tip;

    const auto sprites = archive.read(130);
    for (int direction = 0; direction < 2; ++direction) {
        auto& frames = hocus_frames_[direction];
        frames.reserve(15);
        for (std::size_t frame = 0; frame < 15; ++frame) {
            frames.push_back(decode_sprite_frame(sprites, 0, frame,
                                                 direction != 0, active_palette));
        }
    }
    for (int direction = 0; direction < 2; ++direction) {
        auto& frames = twinkle_frames_[direction];
        frames.reserve(5);
        for (std::size_t frame = 0; frame < 5; ++frame) {
            frames.push_back(decode_sprite_frame(sprites, 2, frame,
                                                 direction != 0, active_palette));
        }
    }
    for (int direction = 0; direction < 2; ++direction) {
        auto& frames = morph_frames_[direction];
        frames.reserve(5);
        for (std::size_t frame = 0; frame < 5; ++frame) {
            frames.push_back(decode_sprite_frame(sprites, 3, frame,
                                                 direction != 0, active_palette));
        }
    }
    // Each .007 word 0 selects a source record from SPRITES.SPR. The DOS loader
    // copies that record into local slot type+4; decoding type+4 directly would
    // therefore show Episode One art in later episodes.
    for (std::size_t type = 0; type < enemy_definitions_.size(); ++type) {
        if (!enemy_definitions_[type].available) {
            continue;
        }
        const auto source = static_cast<std::size_t>(
            enemy_definitions_[type].sprite_asset);
        enemy_sprite_info_[type] = decode_sprite_record_info(sprites, source);
        for (int direction = 0; direction < 2; ++direction) {
            auto& frames = enemy_frames_[type][direction];
            frames.reserve(enemy_sprite_info_[type].frame_count);
            for (std::size_t frame = 0;
                 frame < enemy_sprite_info_[type].frame_count; ++frame) {
                frames.push_back(decode_sprite_frame(
                    sprites, source, frame, direction != 0, active_palette));
            }
        }
    }

    auto bullet_sheet = decode_planar_img(archive.read(14), active_palette);
    if (bullet_sheet.width != 128 || bullet_sheet.height != bullet_height) {
        throw std::runtime_error("Unexpected BULLIT.IMG dimensions");
    }
    // 0BA5:3684 stores effect values 0..9. Values 5..9 select direction one
    // and subtract five; 0BA5:36F0 then renders the resulting cell for 17
    // ticks while moving it upward two pixels per tick.
    for (int direction = 0; direction < 2; ++direction) {
        for (int frame = 0;
             frame < static_cast<int>(bullet_frames_[direction].size()); ++frame) {
            auto& bullet = bullet_frames_[direction][frame];
            bullet.width = bullet_cell_width;
            bullet.height = bullet_height;
            bullet.palette = active_palette;
            bullet.pixels.resize(bullet_cell_width * bullet_height);
            bullet.opacity.resize(bullet_cell_width * bullet_height);
            for (int y = 0; y < bullet_height; ++y) {
                for (int x = 0; x < bullet_cell_width; ++x) {
                    const int source_x = direction == 0
                        ? frame * bullet_cell_width + x
                        : frame * bullet_cell_width + bullet_cell_width - 1 - x;
                    const auto source = static_cast<std::size_t>(y) *
                                            bullet_sheet.width + source_x;
                    const auto target = static_cast<std::size_t>(y) *
                                            bullet_cell_width + x;
                    bullet.pixels[target] = bullet_sheet.pixels[source];
                    bullet.opacity[target] =
                        bullet_sheet.pixels[source] == active_palette.front() ? 0 : 1;
                }
            }
        }
    }

    const auto start = archive.read(131 + level_index_); // .000
    // 0BA5:4734 doubles the stored X; 0BA5:473C multiplies stored Y by 16.
    player_.x_half_tiles = static_cast<int>(u16(start, 2)) * 2;
    player_.y_pixels = static_cast<int>(u16(start, 4)) * tile_size;
    return_x_half_tiles_ = player_.x_half_tiles;
    return_y_pixels_ = player_.y_pixels;
    // The first 0BA5:29C5 call runs with CF38 set and positions the camera
    // immediately; later calls advance by at most one horizontal half-tile and
    // one vertical tile.
    reset_camera();
    // The fourth .000 word lands at DS:7042 and is passed to the enemy-fire
    // random-table helper by 0BA5:1794 and 0BA5:1C25.
    enemy_fire_random_modulus_ = std::max(1, static_cast<int>(u16(start, 6)));

    if (level_id_.episode == 4 && level_id_.number == 9) {
        // 0BA5:4CAD-4CFC creates the selector-99 boss before the E4L9 loop.
        // The runner resets RANDOM.DAT's index immediately afterward.
        spawn_episode_four_boss(0, true);
        random_index_ = 0;
    }

    // Native presentation occurs after tick(), whereas the DOS loop paints
    // the map/HUD and Hocus at distinct points inside the update. Seed the
    // draw snapshots so render() is also valid before the first update.
    draw_background_layer_ = background_layer_;
    draw_main_layer_ = main_layer_;
    draw_progress_ = progress_;
    draw_level_number_flash_ticks_ = level_number_flash_ticks_;
    capture_player_draw_state();
    capture_interpolation_state();
}

std::string GameLevel::level_name() const {
    return "E" + std::to_string(level_id_.episode) +
           "L" + std::to_string(level_id_.number);
}

void GameLevel::place_player(int x_half_tiles, int y_pixels) noexcept {
    player_.x_half_tiles = std::clamp(x_half_tiles, 0, 0x1DD);
    player_.y_pixels = std::clamp(y_pixels, 0, map_height * tile_size - player_height);
    player_.jumping = false;
    player_.falling = false;
    player_.jump_index = 0;
    player_.super_jump = false;
    player_.moved_this_tick = false;
    player_.firing_ticks = 0;
    player_.firing_vertical = false;
    // This diagnostic/test helper represents an instantaneous level position,
    // so give it the same forced camera placement used during level startup.
    reset_camera();
    capture_player_draw_state();
    capture_interpolation_state();
}

void GameLevel::shift_visual_effects_for_camera(const int delta_x,
                                                const int delta_y) noexcept {
    if (delta_x == 0 && delta_y == 0) {
        return;
    }
    for (auto& sparkle : pickup_sparkles_) {
        if (sparkle.ticks > 0) {
            sparkle.x_pixels += delta_x;
            sparkle.y_pixels += delta_y;
            sparkle.draw_x_pixels += delta_x;
            sparkle.draw_y_pixels += delta_y;
        }
    }
    for (auto& twinkle : twinkles_) {
        if (twinkle.ticks > 0) {
            twinkle.x_pixels += delta_x;
            twinkle.y_pixels += delta_y;
        }
    }
    for (auto& burst : explosion_bursts_) {
        if (burst.ticks <= 0) {
            continue;
        }
        for (auto& particle : burst.particles) {
            particle.x_pixels += delta_x;
            particle.y_pixels += delta_y;
            particle.draw_x_pixels += delta_x;
            particle.draw_y_pixels += delta_y;
        }
    }
    for (auto& trail : spell_trails_) {
        if (trail.active) {
            trail.x_pixels += delta_x;
            trail.draw_x_pixels += delta_x;
            trail.y_pixels += delta_y;
            trail.draw_y_pixels += delta_y;
        }
    }
}

void GameLevel::reset_camera() noexcept {
    const int old_x = camera_pixel_x();
    const int old_y = camera_pixel_y();
    const int horizontal_focus_half_tiles = (viewport_width_ + 8) / 16;
    const int maximum_camera_half_tiles =
        (map_width * tile_size - viewport_width_ + 7) / 8;
    camera_x_half_tiles_ = std::clamp(
        player_.x_half_tiles - horizontal_focus_half_tiles,
        0, maximum_camera_half_tiles);
    camera_y_rows_ = std::clamp(
        floor_div(player_.y_pixels, tile_size) - camera_focus_rows_,
        0, map_height - viewport_height / tile_size);
    shift_visual_effects_for_camera(old_x - camera_pixel_x(),
                                    old_y - camera_pixel_y());
}

void GameLevel::update_camera() {
    const int old_x = camera_pixel_x();
    const int old_y = camera_pixel_y();
    const int horizontal_focus_half_tiles = (viewport_width_ + 8) / 16;
    const int maximum_camera_half_tiles =
        (map_width * tile_size - viewport_width_ + 7) / 8;
    const int target_x = std::clamp(
        player_.x_half_tiles - horizontal_focus_half_tiles,
        0, maximum_camera_half_tiles);
    if (camera_x_half_tiles_ < target_x) {
        ++camera_x_half_tiles_;
    } else if (camera_x_half_tiles_ > target_x) {
        --camera_x_half_tiles_;
    }

    // CF0C freezes vertical camera tracking for the recovered jump-table
    // lifetime. Horizontal tracking continues during that interval. The
    // native mid-air-jump extension deliberately lifts only this gate so a
    // chained jump cannot carry Hocus beyond the vertically frozen viewport.
    if (!player_.jumping || cheat_enabled(CheatCode::midair_jump)) {
        const int target_y = std::clamp(
            floor_div(player_.y_pixels, tile_size) - camera_focus_rows_,
            0, map_height - viewport_height / tile_size);
        if (camera_y_rows_ < target_y) {
            ++camera_y_rows_;
        } else if (camera_y_rows_ > target_y) {
            --camera_y_rows_;
        }
    }

    // The original keeps transient effects in screen-relative arrays and
    // shifts them by the inverse camera delta inside 0BA5:29C5.
    shift_visual_effects_for_camera(old_x - camera_pixel_x(),
                                    old_y - camera_pixel_y());
}

void GameLevel::update_camera_focus(const InputState& input) noexcept {
    camera_up_hold_ticks_ = input.action
        ? std::min(10, camera_up_hold_ticks_ + 1) : 0;
    camera_down_hold_ticks_ = input.down
        ? std::min(10, camera_down_hold_ticks_ + 1) : 0;

    // 0BA5:6672-66C6 combines the two long-press look controls with the
    // dedicated configurable scroll keys and joystick buttons three/four.
    // Binding seven decrements CF36; binding six increments it.
    if ((input.scroll_down || camera_down_hold_ticks_ > 0) &&
        camera_focus_rows_ > 0) {
        --camera_focus_rows_;
    }
    if ((input.scroll_up || camera_up_hold_ticks_ > 0) &&
        camera_focus_rows_ < 8) {
        ++camera_focus_rows_;
    }
    // CF54/CF56 must reach ten before the look offset is retained. Short taps
    // snap the focus back to the recovered default of five rows.
    if (!input.scroll_up && !input.scroll_down &&
        camera_up_hold_ticks_ != 10 && camera_down_hold_ticks_ != 10) {
        camera_focus_rows_ = 5;
    }
}

bool GameLevel::solid(int tile_x, int tile_y) const noexcept {
    if (tile_x < 0 || tile_x >= map_width || tile_y < 0 || tile_y >= map_height) {
        return true;
    }
    return main_layer_[static_cast<std::size_t>(tile_y) * map_width + tile_x] != 0xFF;
}

std::uint16_t GameLevel::event_at(int tile_x, int tile_y) const noexcept {
    if (tile_x < 0 || tile_x >= map_width || tile_y < 0 || tile_y >= map_height) {
        return empty_event;
    }
    return event_layer_[static_cast<std::size_t>(tile_y) * map_width + tile_x];
}

std::uint8_t GameLevel::background_tile_at(int tile_x,
                                                int tile_y) const noexcept {
    if (tile_x < 0 || tile_x >= map_width || tile_y < 0 || tile_y >= map_height) {
        return 0xFF;
    }
    return background_layer_[static_cast<std::size_t>(tile_y) * map_width + tile_x];
}

std::uint8_t GameLevel::main_tile_at(int tile_x, int tile_y) const noexcept {
    if (tile_x < 0 || tile_x >= map_width || tile_y < 0 || tile_y >= map_height) {
        return 0xFF;
    }
    return main_layer_[static_cast<std::size_t>(tile_y) * map_width + tile_x];
}

GameLevel::CollisionFlags
GameLevel::horizontal_collision_flags() const noexcept {
    CollisionFlags flags;
    const int row = floor_div(player_.y_pixels, tile_size);
    const bool aligned = player_.y_pixels % tile_size == 0;
    const int left_column = (player_.x_half_tiles + 1) / 2;
    const int right_column = (player_.x_half_tiles + 2) / 2;

    const auto direct_clear = [this, row, aligned](int column) {
        if (solid(column, row) || solid(column, row + 1)) {
            return false;
        }
        return aligned || !solid(column, row + 2);
    };
    const auto can_step = [this, row](int column) {
        return !solid(column, row - 1) && !solid(column, row) &&
               solid(column, row + 1);
    };

    flags.left = direct_clear(left_column);
    flags.step_left = can_step(left_column);
    flags.right = direct_clear(right_column);
    flags.step_right = can_step(right_column);
    return flags;
}

void GameLevel::update_vertical_motion() {
    int player_row = floor_div(player_.y_pixels, tile_size);
    const int collision_column = (player_.x_half_tiles + 1) / 2;

    if (player_.jumping) {
        const std::span<const int> jump = player_.super_jump
            ? std::span<const int>(super_jump)
            : std::span<const int>(normal_jump);
        if (player_.jump_index < static_cast<int>(jump.size())) {
            player_.y_pixels += jump[player_.jump_index];
            player_row = floor_div(player_.y_pixels, tile_size);

            // The original reverses to the matching descending table entry when
            // Hocus' top sample enters a solid tile (0BA5:6065-608B).
            if (solid(collision_column, player_row)) {
                player_.jump_index = static_cast<int>(jump.size()) - 1 -
                                     player_.jump_index;
                player_.y_pixels += jump[player_.jump_index];
                player_row = floor_div(player_.y_pixels, tile_size);
            }
            ++player_.jump_index;
            if (player_.jump_index >= static_cast<int>(jump.size())) {
                player_.jumping = false;
            }
        } else {
            player_.jumping = false;
        }
    }

    const bool ground_below = solid(collision_column, player_row + 2);
    if (ground_below) {
        const int apex = player_.super_jump ? super_jump_apex : normal_jump_apex;
        if (player_.jumping && player_.jump_index >= apex) {
            player_.jumping = false;
            player_.y_pixels = player_row * tile_size;
        }
        player_.falling = false;
    } else if (!player_.jumping) {
        // Original gravity advances one complete tile (0BA5:612D).
        player_.y_pixels += tile_size;
        player_.falling = !solid(collision_column, player_row + 3);
    }
}

void GameLevel::eject_from_floor_if_needed() noexcept {
    if (!cheat_enabled(CheatCode::midair_jump)) {
        return;
    }

    const int collision_column = (player_.x_half_tiles + 1) / 2;
    const int lower_half_first_row = floor_div(
        player_.y_pixels + player_height / 2, tile_size);
    const int bottom_row = floor_div(
        player_.y_pixels + player_height - 1, tile_size);
    bool lower_half_overlaps_floor = false;
    for (int row = lower_half_first_row; row <= bottom_row; ++row) {
        if (solid(collision_column, row)) {
            lower_half_overlaps_floor = true;
            break;
        }
    }
    if (!lower_half_overlaps_floor) {
        return;
    }

    const auto position_is_clear = [this, collision_column](
                                       const int y_pixels) {
        const int first_row = floor_div(y_pixels, tile_size);
        const int last_row = floor_div(
            y_pixels + player_height - 1, tile_size);
        for (int row = first_row; row <= last_row; ++row) {
            if (solid(collision_column, row)) {
                return false;
            }
        }
        return true;
    };

    // A restarted normal/super jump moves at most sixteen pixels per update.
    // Forty-eight pixels covers the complete player height plus that maximum
    // penetration without turning the recovery into an arbitrary level warp.
    constexpr int maximum_floor_eject_pixels = player_height + tile_size;
    const int minimum_y = std::max(
        0, player_.y_pixels - maximum_floor_eject_pixels);
    for (int candidate_y = player_.y_pixels - 1;
         candidate_y >= minimum_y; --candidate_y) {
        if (!position_is_clear(candidate_y)) {
            continue;
        }
        player_.y_pixels = candidate_y;
        player_.jumping = false;
        player_.falling = false;
        player_.jump_index = 0;
        player_.super_jump = false;
        return;
    }
}

void GameLevel::show_wizard_message() {
    const int player_column = floor_div(player_.x_half_tiles, 2);
    const int player_row = floor_div(player_.y_pixels, tile_size);
    for (const auto& message : messages_) {
        if (message.tile_y == player_row &&
            std::abs(message.tile_x - player_column) < 3) {
            active_message_ = message.lines;
            return;
        }
    }
}

std::vector<int> GameLevel::take_sound_events() {
    auto events = std::move(sound_events_);
    sound_events_.clear();
    return events;
}

void GameLevel::emit_sound(const int logical_sound) {
    if (logical_sound >= 0 && logical_sound < 16) {
        sound_events_.push_back(logical_sound);
    }
}

void GameLevel::spawn_pickup_sparkle(const int kind) {
    if (kind < 0 || kind > 9) {
        return;
    }
    const auto free = std::find_if(
        pickup_sparkles_.begin(), pickup_sparkles_.end(),
        [](const PickupSparkleState& sparkle) { return sparkle.ticks == 0; });
    if (free == pickup_sparkles_.end()) {
        return;
    }

    const int player_x = player_.x_half_tiles * 8;
    const int camera_x = camera_pixel_x();
    const int camera_y = camera_pixel_y();
    *free = {
        player_x - camera_x - 1,
        player_.y_pixels - camera_y - 16,
        kind > 4 ? kind - 5 : kind,
        17,
        0,
        0,
        kind > 4,
        false,
    };
}

void GameLevel::spawn_twinkle(const int x_pixels, const int y_pixels) {
    auto& twinkle = twinkles_[next_twinkle_];
    twinkle = {x_pixels, y_pixels, 9, 0, random_below(2) != 0, false};
    next_twinkle_ = (next_twinkle_ + 1) % twinkles_.size();
}

void GameLevel::spawn_explosion(const int x_pixels, const int y_pixels) {
    // 0BA5:2F1C owns eight circular burst slots. Each burst contains sixteen
    // single-pixel fragments with independent ballistic velocities.
    auto& burst = explosion_bursts_[next_explosion_burst_];
    burst.ticks = 16;
    burst.draw_visible = false;
    for (auto& particle : burst.particles) {
        particle.x_pixels = x_pixels;
        particle.y_pixels = y_pixels;
        particle.x_velocity = random_below(10) - random_below(10);
        particle.y_velocity = -(random_below(7) + 5);
        particle.colour_index = random_below(2) == 0 ? 0x70 : 0x60;
    }
    next_explosion_burst_ =
        (next_explosion_burst_ + 1) % explosion_bursts_.size();
}

void GameLevel::spawn_spell_trail(const int x_pixels, const int y_pixels) {
    // 0BA5:21C6 advances a circular 20-slot pool and only claims the current
    // slot after its previous 16-tick speck has expired.
    auto& trail = spell_trails_[next_spell_trail_];
    if (!trail.active) {
        trail = {floor_div(x_pixels, 4) * 4, y_pixels, 0, x_pixels,
                 y_pixels, 0, false, true};
    }
    next_spell_trail_ = (next_spell_trail_ + 1) % spell_trails_.size();
}

void GameLevel::update_visual_effects() {
    for (auto& sparkle : pickup_sparkles_) {
        sparkle.draw_visible = false;
        if (sparkle.ticks == 0) {
            continue;
        }
        // 0BA5:3704-3732 draws first; 3735-3744 moves and decrements the
        // persistent state afterward.
        sparkle.draw_x_pixels = sparkle.x_pixels;
        sparkle.draw_y_pixels = sparkle.y_pixels;
        sparkle.draw_visible = true;
        sparkle.y_pixels -= 2;
        --sparkle.ticks;
    }
    for (auto& twinkle : twinkles_) {
        twinkle.draw_visible = false;
        if (twinkle.ticks > 0) {
            twinkle.draw_ticks = twinkle.ticks;
            twinkle.draw_visible = true;
            --twinkle.ticks;
        }
    }
    // 0BA5:2FF3 applies gravity after the current vertical velocity, moves
    // horizontally, draws the fragments, and expires the burst after 16 ticks.
    for (auto& burst : explosion_bursts_) {
        burst.draw_visible = false;
        if (burst.ticks <= 0) {
            continue;
        }
        for (auto& particle : burst.particles) {
            particle.y_pixels += particle.y_velocity;
            ++particle.y_velocity;
            particle.x_pixels += particle.x_velocity;
            particle.draw_x_pixels = particle.x_pixels;
            particle.draw_y_pixels = particle.y_pixels;
        }
        burst.draw_visible = true;
        --burst.ticks;
    }
    // 0BA5:20B1 selects one VGA plane, draws on alternate random updates,
    // shifts from palette entry 0x78 through 0x7B, and falls one pixel/tick.
    for (auto& trail : spell_trails_) {
        trail.visible = false;
        if (!trail.active) {
            continue;
        }
        trail.draw_x_pixels = trail.x_pixels + random_below(4);
        trail.visible = random_below(2) == 0;
        trail.draw_y_pixels = trail.y_pixels;
        trail.draw_age = trail.age;
        ++trail.y_pixels;
        ++trail.age;
        if (trail.age >= 16) {
            trail.active = false;
            trail.visible = false;
        }
    }
}

void GameLevel::spawn_super_jump_twinkle() {
    if (super_jump_effect_ticks_ <= 0) {
        return;
    }
    const int player_x = player_.x_half_tiles * 8;
    const int camera_x = camera_pixel_x();
    const int camera_y = camera_pixel_y();
    // 0BA5:5343-5368 emits one Twinks cell around Hocus on every update while
    // the super-jump pickup is armed and during its 25-tick release effect.
    const int screen_y = player_.y_pixels - camera_y + random_below(10) + 20;
    const int screen_x = player_x - camera_x + (random_below(7) - 2) * 4;
    spawn_twinkle(screen_x, screen_y);
    // CF0E remains armed while the pickup is available. Once a jump consumes
    // CF10, 0BA5:536E-5375 counts out the remaining 25 update effects.
    if (!progress_.super_jump_available) {
        --super_jump_effect_ticks_;
    }
}

void GameLevel::process_item_event(std::size_t cell, std::uint16_t event,
                                   bool& action_pressed,
                                   const bool primary_sample) {
    if (event >= item_definitions.size()) {
        return;
    }

    const auto item = item_definitions[event];
    bool keep = item.heal != 0 && progress_.health >= 100;
    if (item.heal != 0 && !keep) {
        // 0BA5:3A89-3A98 uses the distinct health-pickup cue before the
        // common score/health/firepower update.
        emit_sound(1);
    }

    if (item.score != 0) {
        ++progress_.treasures;
        if (progress_.treasures == progress_.total_treasures) {
            // 0BA5:3AC0-3AD3 starts the perpetual 1..20 HUD level-number
            // flash and emits logical sound nine as soon as the final scoring
            // treasure is collected. The randomized treasure sound follows.
            level_number_flash_ticks_ = 1;
            emit_sound(9);
        }
        const int sparkle = item.score == 100 ? 0
            : item.score == 250 ? 1
            : item.score == 500 ? 2
            : item.score == 1000 ? 3 : 4;
        spawn_pickup_sparkle(sparkle);
        // 0BA5:3B55 selects one of the two treasure effects.
        emit_sound(random_below(2) + 2);
    } else {
        switch (item.type) {
        case 1: // Crystal
            ++progress_.crystals;
            // 0BA5:3B70-3B7A starts the eight-frame solid-palette flash
            // rendered by 0BA5:220F.
            crystal_flash_ticks_ = 8;
            emit_sound(4);
            break;
        case 2: // Repeating spike/contact damage; 20-tick immunity.
            keep = true;
            if (primary_sample && progress_.damage_cooldown == 0 &&
                progress_.invisibility_ticks == 0) {
                progress_.health -= damage_amount_;
                progress_.damage_cooldown = 20;
                emit_sound(8);
            }
            break;
        case 3: // Invisibility
            progress_.invisibility_ticks = 400;
            {
                const int player_x = player_.x_half_tiles * 8;
                const int camera_x = camera_pixel_x();
                const int camera_y = camera_pixel_y();
                const int screen_x = player_x - camera_x;
                const int screen_y = player_.y_pixels - camera_y;
                // CF2A is a VGA byte column, so the recovered -1/+4 offsets
                // correspond to -4/+16 native pixels.
                spawn_twinkle(screen_x - 4, screen_y);
                spawn_twinkle(screen_x + 16, screen_y);
                spawn_twinkle(screen_x - 4, screen_y + 16);
                spawn_twinkle(screen_x + 16, screen_y + 16);
            }
            spawn_pickup_sparkle(8);
            progress_.damage_cooldown = 0;
            emit_sound(5);
            break;
        case 4: // One super jump
            if (!primary_sample || progress_.super_jump_available) {
                keep = true;
            } else {
                progress_.super_jump_available = true;
                super_jump_effect_ticks_ = 25;
                spawn_pickup_sparkle(7);
                emit_sound(9);
            }
            break;
        case 5: // Timed super shot
            if (!primary_sample) {
                keep = true;
            } else {
                if (progress_.super_shot_ticks == 0) {
                    saved_firepower_ = progress_.firepower;
                }
                progress_.firepower = 10;
                progress_.super_shot_ticks = 600;
                spawn_pickup_sparkle(9);
                emit_sound(9);
            }
            break;
        case 7:
            progress_.silver_key = true;
            spawn_pickup_sparkle(5);
            emit_sound(9);
            break;
        case 8:
            progress_.gold_key = true;
            spawn_pickup_sparkle(5);
            emit_sound(9);
            break;
        case 9: // Lava/kill field; repeats after 10 ticks.
            keep = true;
            if (primary_sample && progress_.damage_cooldown == 0 &&
                progress_.invisibility_ticks == 0) {
                progress_.health -= damage_amount_;
                progress_.damage_cooldown = 10;
                emit_sound(8);
            }
            break;
        case 10: // Wizard note: persistent and activated by Fire.
            keep = true;
            if (action_pressed) {
                show_wizard_message();
                emit_sound(5);
                // DS:70AE is an edge byte shared by all six samples. The DOS
                // dispatcher clears it after the first wizard-note attempt.
                action_pressed = false;
            }
            break;
        case 11:
            if (!primary_sample) {
                keep = true;
            } else {
                progress_.laser_shots += 3;
                // 0BA5:3DE9-3DFC restarts the five-tick blinking stack of
                // available laser bolts whenever this pickup is collected.
                laser_indicator_visible_ = true;
                laser_indicator_ticks_ = 5;
                emit_sound(9);
            }
            break;
        case 12:
            // 0BA5:3E04-3E53 stores Hocus's current coordinates and consumes
            // both halves of this otherwise-unused registered marker.
            return_x_half_tiles_ = player_.x_half_tiles;
            return_y_pixels_ = player_.y_pixels;
            keep = true;
            event_layer_[cell] = empty_event;
            background_layer_[cell] = clear_background_tile_;
            if (cell + 1 < event_layer_.size()) {
                event_layer_[cell + 1] = empty_event;
                background_layer_[cell + 1] = clear_background_tile_;
            }
            emit_sound(9);
            break;
        default:
            break;
        }
    }

    if (item.firepower != 0) {
        emit_sound(9);
    }

    progress_.score += item.score;
    // 0BA5:3ED4 adds health without clamping inside the six-cell scan. The
    // HUD routine at 348D clamps once after the entire dispatcher returns.
    progress_.health += item.heal;
    progress_.firepower = std::clamp(progress_.firepower + item.firepower, 1, 10);
    if (progress_.super_shot_ticks != 0) {
        // While the visible firepower is pinned at ten, the original applies
        // normal firepower pickups to CE86 so expiration restores the upgrade.
        saved_firepower_ = std::clamp(
            saved_firepower_ + item.firepower, 1, 10);
    }

    if (!keep) {
        event_layer_[cell] = empty_event;
        background_layer_[cell] = clear_background_tile_;
    }
}

void GameLevel::process_teleporter_event(std::size_t cell,
                                         std::uint16_t event,
                                         bool primary_sample) {
    constexpr std::uint16_t first_teleporter_event = 23;
    if (!primary_sample || event < first_teleporter_event ||
        event >= first_teleporter_event + teleporters_.size()) {
        return;
    }
    const auto& teleporter = teleporters_[event - first_teleporter_event];
    // 0BA5:3F07-3F19 rejects the matching event at the destination; only the
    // first .003 word is the trigger cell for this one-way transition.
    if (teleporter.trigger_cell != cell ||
        teleporter.destination_cell >= event_layer_.size()) {
        return;
    }

    teleporter_ticks_ = 1;
    spawn_pickup_sparkle(6);
    emit_sound(5);
    teleporter_target_x_ = (teleporter.destination_cell % map_width) * 2;
    teleporter_target_y_ = (teleporter.destination_cell / map_width) * tile_size;
    player_.y_pixels = floor_div(player_.y_pixels, tile_size) * tile_size;
    player_.jumping = false;
    player_.falling = false;
    player_.jump_index = 0;
    player_.super_jump = false;
    player_.moved_this_tick = false;

    // The original consumes only the trigger endpoint at 0BA5:3F75-3F8F.
    event_layer_[cell] = empty_event;
    background_layer_[cell] = clear_background_tile_;
}

bool GameLevel::update_teleporter() {
    if (teleporter_ticks_ == 0) {
        return false;
    }

    if (player_.x_half_tiles == teleporter_target_x_ &&
        player_.y_pixels == teleporter_target_y_) {
        if (teleporter_ticks_ == 40) {
            teleporter_ticks_ = 0;
            // State zero takes effect for the next outer-loop update; the
            // current update remains input-locked.
            return true;
        }
        ++teleporter_ticks_;
    } else if (teleporter_ticks_ < 25) {
        ++teleporter_ticks_;
    }

    // At state 25, 0BA5:65FC-662E moves vertically by one tile and
    // horizontally by one original X unit (half a tile) on every update.
    if (teleporter_ticks_ == 25) {
        if (player_.y_pixels < teleporter_target_y_) {
            player_.y_pixels += tile_size;
        } else if (player_.y_pixels > teleporter_target_y_) {
            player_.y_pixels -= tile_size;
        }
        if (player_.x_half_tiles < teleporter_target_x_) {
            ++player_.x_half_tiles;
        } else if (player_.x_half_tiles > teleporter_target_x_) {
            --player_.x_half_tiles;
        }
    }
    return teleporter_ticks_ != 0;
}

void GameLevel::process_switch_event(std::size_t cell, std::uint16_t event,
                                     bool& action_pressed,
                                     const bool primary_sample) {
    constexpr std::uint16_t first_switch_event = 33;
    if (!action_pressed || !primary_sample || event < first_switch_event ||
        event >= first_switch_event + switches_.size()) {
        return;
    }
    auto& record = switches_[event - first_switch_event];

    // DS:70AE is global to this dispatcher pass, so no later overlapping
    // switch or wizard cell can consume the same Up edge.
    action_pressed = false;

    background_layer_[cell] = background_layer_[cell] == switch_off_tile_
        ? switch_on_tile_
        : switch_off_tile_;

    bool conditions_met = false;
    for (std::size_t index = 0; index < record.dependencies.size(); ++index) {
        const auto dependency = record.dependencies[index];
        if (dependency == 0xFFFF) {
            continue;
        }
        conditions_met = true;
        if (dependency >= background_layer_.size() ||
            background_layer_[dependency] != record.expected_tiles[index]) {
            conditions_met = false;
            break;
        }
    }
    if (conditions_met) {
        emit_sound(7);
        record.activated = true;
        record.dependencies.fill(0xFFFF);
        gate_countdown_ = 25;
        for (int row = record.first_row; row <= record.last_row; ++row) {
            for (int column = record.first_column;
                 column <= record.last_column; ++column) {
                if (column < 0 || column >= map_width ||
                    row < 0 || row >= map_height) {
                    continue;
                }
                const auto target = static_cast<std::size_t>(row) * map_width +
                                    column;
                if (record.mode == 0) {
                    if (main_layer_[target] != 0xFF) {
                        pending_gate_layer_[target] = 1;
                    }
                } else if (original_main_layer_[target] != 0xFF) {
                    pending_gate_layer_[target] = 2;
                }
            }
        }
    } else {
        emit_sound(6);
    }
}

void GameLevel::process_gate_event(const std::size_t cell,
                                   const std::uint16_t event,
                                   const bool primary_sample) {
    constexpr std::uint16_t first_insertion_event = 56;
    constexpr std::uint16_t first_removal_event = 81;
    (void)primary_sample;
    // Unlike teleporters, switches, and enemy triggers, 41AA/42F7 do not test
    // the center-column selector. Any of Hocus's six sampled cells can unlock
    // an insertion/removal gate.
    if (event < first_insertion_event || event >= 106) {
        return;
    }

    const bool removing = event >= first_removal_event;
    auto& records = removing ? keyholes_ : insertion_gates_;
    auto& record = records[event - (removing ? first_removal_event
                                             : first_insertion_event)];

    const bool unlocked = record.required_key == 0 ||
        (record.required_key == 1 && progress_.silver_key) ||
        (record.required_key == 2 && progress_.gold_key);
    if (!unlocked) {
        return;
    }

    emit_sound(7);
    gate_countdown_ = 10;
    for (int row = record.first_row; row <= record.last_row; ++row) {
        for (int column = record.first_column;
             column <= record.last_column; ++column) {
            if (column < 0 || column >= map_width ||
                row < 0 || row >= map_height) {
                continue;
            }
            const auto target = static_cast<std::size_t>(row) * map_width +
                                column;
            if (removing) {
                if (main_layer_[target] != 0xFF) {
                    pending_gate_layer_[target] = 1;
                }
            } else if (original_main_layer_[target] != 0xFF) {
                pending_gate_layer_[target] = 2;
            }
        }
    }
    event_layer_[cell] = empty_event;
    background_layer_[cell] = record.required_key == 0
        ? record.replacement_tile
        : clear_background_tile_;
    if (record.required_key == 1) {
        progress_.silver_key = false;
    } else if (record.required_key == 2) {
        progress_.gold_key = false;
    }
    if (record.required_key != 0) {
        const int column = static_cast<int>(cell % map_width);
        const int row = static_cast<int>(cell / map_width);
        spawn_twinkle(column * tile_size - camera_pixel_x(),
                      row * tile_size - camera_pixel_y());
    }
}

void GameLevel::update_tile_viewport() {
    // 0BA5:2C81 runs on alternating fixed updates. It decrements the one
    // shared gate timer and visits exactly the 21 x 10 map cells covered by
    // the potentially half-tile-aligned viewport.
    --gate_countdown_;
    const int first_column = floor_div(camera_x_half_tiles_, 2);
    for (int row_offset = 0; row_offset < 10; ++row_offset) {
        const int row = camera_y_rows_ + row_offset;
        if (row < 0 || row >= map_height) {
            continue;
        }
        for (int column_offset = 0; column_offset < 21; ++column_offset) {
            const int column = first_column + column_offset;
            if (column < 0 || column >= map_width) {
                continue;
            }
            const auto cell = static_cast<std::size_t>(row) * map_width +
                              column;
            const auto pending = pending_gate_layer_[cell];
            if (pending != 0) {
                const bool forced = gate_countdown_ == 0;
                if (forced || random_below(3) == 0) {
                    if (pending == 1) {
                        main_layer_[cell] = 0xFF;
                    } else if (pending == 2) {
                        main_layer_[cell] = original_main_layer_[cell];
                    }
                    pending_gate_layer_[cell] = 0;
                    if (!forced && column_offset < 20) {
                        spawn_twinkle(column * tile_size - camera_pixel_x(),
                                      row * tile_size - camera_pixel_y());
                    }
                }
            }

            const auto tile = background_layer_[cell];
            if (tile >= tile_animations_.size()) {
                continue;
            }
            const auto animation = tile_animations_[tile];
            int next = tile;
            if (animation.mode == 1) {
                ++next;
                if (next > animation.last) {
                    next = animation.first;
                }
            } else if (animation.mode == 2) {
                if (tile != animation.first || random_below(20) == 0) {
                    ++next;
                    if (next > animation.last) {
                        next = animation.first;
                    }
                }
            }
            background_layer_[cell] = static_cast<std::uint8_t>(next);
        }
    }
}

void GameLevel::process_enemy_trigger(std::uint16_t event,
                                            bool primary_sample) {
    constexpr std::uint16_t first_enemy_trigger = 116;
    if (!primary_sample || event < first_enemy_trigger ||
        event >= first_enemy_trigger + enemy_triggers_.size()) {
        return;
    }

    const auto& trigger = enemy_triggers_[event - first_enemy_trigger];
    for (std::size_t slot = 0; slot < trigger.types.size(); ++slot) {
        const auto type = trigger.types[slot];
        if (type == 0xFFFF) {
            break;
        }
        const auto spawn_cell = trigger.spawn_cells[slot];
        if (type >= enemy_definitions_.size() ||
            !enemy_definitions_[type].available ||
            spawn_cell >= event_layer_.size() ||
            event_layer_[spawn_cell] != 106 + type) {
            continue;
        }

        const bool already_live = std::any_of(
            enemies_.begin(), enemies_.end(),
            [spawn_cell](const EnemyState& enemy) {
                return enemy.active && enemy.spawn_cell == spawn_cell;
            });
        const bool already_pending = std::any_of(
            pending_enemy_spawns_.begin(), pending_enemy_spawns_.end(),
            [spawn_cell](const PendingEnemySpawn& pending) {
                return pending.active && pending.spawn_cell == spawn_cell;
            });
        if (already_live || already_pending) {
            continue;
        }

        const auto pending = std::find_if(
            pending_enemy_spawns_.begin(), pending_enemy_spawns_.end(),
            [](const PendingEnemySpawn& candidate) { return !candidate.active; });
        if (pending == pending_enemy_spawns_.end()) {
            return;
        }
        *pending = {static_cast<int>(type), static_cast<int>(spawn_cell), true};
    }
}

void GameLevel::spawn_pending_enemies() {
    for (auto& pending : pending_enemy_spawns_) {
        if (!pending.active) {
            continue;
        }
        const auto free = std::find_if(enemies_.begin(), enemies_.end(),
                                       [](const EnemyState& enemy) {
                                           return !enemy.active;
                                       });
        if (free == enemies_.end()) {
            return;
        }
        if (pending.spawn_cell < 0 ||
            pending.spawn_cell >= static_cast<int>(event_layer_.size()) ||
            event_layer_[pending.spawn_cell] != 106 + pending.type) {
            pending.active = false;
            continue;
        }

        const int column = pending.spawn_cell % map_width;
        const int row = pending.spawn_cell / map_width;
        const int x = column * tile_size;
        EnemyState spawned{};
        spawned.type = pending.type;
        spawned.spawn_cell = pending.spawn_cell;
        spawned.x_pixels = x;
        spawned.y_pixels = row * tile_size;
        const int definition_health = enemy_definitions_[pending.type].health;
        // 0BA5:05DB-05FE doubles DS:706A (the zero-based skill selector)
        // before adding it to ordinary non-negative extra-hit counts.
        // Negative sentinel values such as -2 retain their special meaning.
        spawned.health = definition_health >= 0
            ? definition_health + skill_ * 2
            : definition_health;
        spawned.max_health = spawned.health;
        spawned.behaviour =
            enemy_definitions_[pending.type].active_behaviour;
        spawned.sprite_frame = std::max(
            0, enemy_sprite_info_[pending.type].movement_first);
        spawned.spawn_ticks = enemy_spawn_ticks;
        spawned.facing_west = x > player_.x_half_tiles * 8;
        spawned.active = true;
        initialise_enemy_motion(spawned);
        *free = spawned;
        pending.active = false;
        ++active_enemy_count_;
        // Selector four's creation target (0BA5:08B4) has its own sound.
        if (spawned.behaviour == 4) {
            emit_sound(11);
        }
        if (std::abs(spawned.x_pixels - player_.x_half_tiles * 8) < 200 &&
            std::abs(spawned.y_pixels - player_.y_pixels) < 100 &&
            elapsed_ticks_ > spawn_sound_deadline_) {
            emit_sound(14);
            // 0BA5:06D2-0705 stores a deadline five ticks ahead on the
            // independent 140 Hz counter. The slowest gameplay update waits
            // only eight such ticks (DS:14F8 = {8,7,6}), so the lockout can
            // suppress later creations in this update but has always expired
            // before the next update. elapsed_ticks_ is the update-domain
            // equivalent of that observable rule.
            spawn_sound_deadline_ = elapsed_ticks_;
        }
    }
}

void GameLevel::spawn_episode_four_boss(int stage, bool initial_spawn) {
    if (stage < 0 || stage >= static_cast<int>(boss4_x_units.size())) {
        return;
    }
    const auto free = std::find_if(enemies_.begin(), enemies_.end(),
                                   [](const EnemyState& enemy) {
                                       return !enemy.active;
                                   });
    if (free == enemies_.end()) {
        return;
    }

    EnemyState boss{};
    boss.type = 0;
    boss.spawn_cell = boss4_spawn_cells[stage];
    // The table stores tile columns. 4CD2 converts them to the engine's
    // four-pixel X units; the native state keeps the resulting pixel value.
    boss.x_pixels = boss4_x_units[stage] * tile_size;
    boss.y_pixels = boss4_rows[stage] * tile_size;
    boss.health = boss4_health[stage];
    boss.max_health = boss.health;
    boss.behaviour = 99;
    boss.behaviour_frame = 0;
    boss.sprite_frame = std::max(0, enemy_sprite_info_[0].movement_first);
    boss.behaviour_phase = 0;
    boss.boss_stage = stage;
    boss.attack_ticks = initial_spawn
        ? random_below(20) + 10
        : random_below(50) + 5;
    boss.spawn_ticks = enemy_spawn_ticks;
    boss.facing_west = boss4_faces_west[stage];
    boss.active = true;
    *free = boss;
    ++active_enemy_count_;
}

int GameLevel::random_below(int upper_bound) noexcept {
    if (upper_bound <= 0) {
        return 0;
    }
    ++random_index_;
    if (random_index_ > 999) {
        random_index_ = 0;
    }
    return random_table_[static_cast<std::size_t>(random_index_)] % upper_bound;
}

void GameLevel::initialise_enemy_motion(EnemyState& enemy) {
    const auto random_signed_unit = [this]() {
        return random_below(2) - random_below(2);
    };

    switch (enemy.behaviour) {
    case 0:
    case 1:
    case 7:
        enemy.x_velocity_pixels = enemy.facing_west ? -4 : 4;
        enemy.y_velocity_pixels = 0;
        break;
    case 2:
    case 3:
        do {
            enemy.x_velocity_pixels = random_signed_unit() * 4;
            enemy.y_velocity_pixels = random_signed_unit() * 4;
        } while (enemy.x_velocity_pixels == 0 &&
                 enemy.y_velocity_pixels == 0);
        enemy.movement_ticks = random_below(20);
        // Creation at 0BA5:064C forces selector two to direction zero. Several
        // such source records intentionally have no usable west layouts.
        if (enemy.behaviour == 2) {
            enemy.facing_west = false;
        }
        break;
    case 6:
        do {
            enemy.x_velocity_pixels = random_signed_unit() * 4;
            enemy.y_velocity_pixels = random_signed_unit() * 4;
        } while (enemy.x_velocity_pixels == 0);
        enemy.movement_ticks = random_below(20);
        break;
    case 4:
        // Creation target 0BA5:08B4 clears both velocities, forces direction
        // zero and frame zero, then emits its selector-specific sound in the
        // caller.
        enemy.x_velocity_pixels = 0;
        enemy.y_velocity_pixels = 0;
        enemy.facing_west = false;
        enemy.sprite_frame = 0;
        break;
    case 5:
        // 0BA5:08E6 likewise clears motion and frame but retains direction.
        enemy.x_velocity_pixels = 0;
        enemy.y_velocity_pixels = 0;
        enemy.sprite_frame = 0;
        break;
    case 8:
        // Selector eight is created by 0BA5:0720. It remembers its original
        // X coordinate and waits 10..19 ticks between four-pixel steps.
        enemy.anchor_x_pixels = enemy.x_pixels;
        enemy.behaviour_frame = 0;
        enemy.movement_ticks = random_below(10) + 10;
        enemy.x_velocity_pixels = 0;
        enemy.y_velocity_pixels = 0;
        break;
    default:
        enemy.x_velocity_pixels = 0;
        enemy.y_velocity_pixels = 0;
        break;
    }
}

bool GameLevel::enemy_collides_at(int x_pixels, int y_pixels,
                                  int width, int height) const noexcept {
    const int first_column = floor_div(x_pixels, tile_size);
    const int last_column = floor_div(x_pixels + width - 1, tile_size);
    const int first_row = floor_div(y_pixels, tile_size);
    const int last_row = floor_div(y_pixels + height - 1, tile_size);
    for (int row = first_row; row <= last_row; ++row) {
        for (int column = first_column; column <= last_column; ++column) {
            if (solid(column, row)) {
                return true;
            }
        }
    }
    return false;
}

bool GameLevel::spawn_enemy_projectile(const EnemyState& enemy,
                                       int width, int height,
                                       const int delay_override) {
    const auto free = std::find_if(
        enemy_projectiles_.begin(), enemy_projectiles_.end(),
        [](const EnemyProjectileState& projectile) {
            return !projectile.active;
        });
    if (free == enemy_projectiles_.end()) {
        return false;
    }

    const auto& definition = enemy_definitions_[enemy.type];
    const auto& sprite = enemy_sprite_info_[enemy.type];
    if (sprite.projectile_first < 0 || sprite.projectile_last <
                                               sprite.projectile_first) {
        return false;
    }
    EnemyProjectileState projectile{};
    projectile.owner_type = enemy.type;
    const bool directional = definition.words[2] != 0;
    projectile.facing_west = directional &&
        enemy.x_pixels > player_.x_half_tiles * 8;
    // The two call sites at 1CE5 and 1D9B pass different origins. Moving
    // shots start at the enemy's facing edge. A zero-X-speed shot instead
    // uses definition word 4 as a four-pixel X offset and starts one body
    // height below the enemy before header +50 is applied.
    if (directional) {
        projectile.x_pixels = projectile.facing_west
            ? enemy.x_pixels
            : enemy.x_pixels + width;
        projectile.y_pixels = enemy.y_pixels + sprite.projectile_y_offset;
    } else {
        projectile.x_pixels = enemy.x_pixels + definition.words[4] * 4;
        projectile.y_pixels = enemy.y_pixels + height +
                              sprite.projectile_y_offset;
    }
    projectile.delay_ticks = delay_override >= 0
        ? delay_override
        : enemy.behaviour == 5
            ? std::max(0, sprite.attack_last - sprite.attack_first)
            : 1;
    // Header +46 is stored in VGA byte-columns, not pixels.
    projectile.width = std::max(1, sprite.projectile_width * 4);
    // 0BA5:09C7-09D2 adds three scanlines to header +48.
    projectile.height = std::max(1, sprite.projectile_height + 3);
    projectile.sprite_frame = sprite.projectile_first;
    projectile.last_sprite_frame = sprite.projectile_last;
    // 0BA5:0A37-0A69 copies definition words 2, 3, 5, and 9 into
    // horizontal speed, vertical speed, horizontal homing, and random Y
    // wobble. Enemy X velocities use the same four-pixel conversion as body
    // movement.
    projectile.x_speed_pixels = std::abs(definition.words[2]) * 4;
    projectile.y_speed_pixels = definition.words[3];
    projectile.horizontal_homing = definition.words[5] != 0;
    projectile.vertical_wobble = definition.words[9] != 0;
    projectile.active = true;
    *free = projectile;
    ++active_enemy_projectile_count_;
    emit_sound(13);
    return true;
}

void GameLevel::collide_enemy_projectiles_with_player() {
    const int player_x = player_.x_half_tiles * 8;
    for (auto& projectile : enemy_projectiles_) {
        if (!projectile.active || projectile.delay_ticks != 0) {
            continue;
        }
        const int player_hitbox_x = player_x + player_hitbox_x_offset;
        const int player_hitbox_y = player_.y_pixels + player_hitbox_y_offset;
        const bool touches_player =
            projectile.x_pixels < player_hitbox_x + player_hitbox_width &&
            projectile.x_pixels + projectile.width > player_hitbox_x &&
            projectile.y_pixels < player_hitbox_y + player_hitbox_height &&
            projectile.y_pixels + projectile.height > player_hitbox_y;
        if (touches_player && progress_.damage_cooldown == 0 &&
            progress_.invisibility_ticks == 0) {
            progress_.health = std::max(0, progress_.health - damage_amount_);
            progress_.damage_cooldown = 20;
            emit_sound(8);
            projectile.active = false;
            --active_enemy_projectile_count_;
        }
    }
}

void GameLevel::update_enemy_projectiles() {
    const int player_x = player_.x_half_tiles * 8;
    const int camera_x = camera_pixel_x();
    const int camera_y = camera_pixel_y();

    for (auto& projectile : enemy_projectiles_) {
        if (!projectile.active) {
            continue;
        }
        projectile.draw_visible = false;
        // 0BA5:0A9E skips delayed shots and 0BA5:0C46 decrements the delay.
        // Collision was already evaluated by 0BA5:0006 earlier in the tick.
        if (projectile.delay_ticks > 0) {
            --projectile.delay_ticks;
            continue;
        }

        const bool outside_view =
            projectile.x_pixels < camera_x - 40 ||
            projectile.x_pixels > camera_x + viewport_width_ ||
            projectile.y_pixels < camera_y - 20 ||
            projectile.y_pixels > camera_y + viewport_height;
        const bool terrain_collision = solid(
            floor_div(projectile.x_pixels, tile_size),
            floor_div(projectile.y_pixels, tile_size));
        if (outside_view || terrain_collision) {
            if (terrain_collision) {
                // 0BA5:0B35-0B51 creates a centred burst only for a solid
                // tile impact; a projectile leaving the viewport vanishes.
                spawn_explosion(projectile.x_pixels - camera_x + 8,
                                projectile.y_pixels - camera_y + 8);
            }
            projectile.active = false;
            --active_enemy_projectile_count_;
            continue;
        }

        // 0BA5:0B57 renders the slot before advancing its frame and position.
        // Native presentation happens after tick(), so preserve that exact
        // draw state rather than exposing the post-movement state early.
        projectile.draw_x_pixels = projectile.x_pixels;
        projectile.draw_y_pixels = projectile.y_pixels;
        projectile.draw_sprite_frame = projectile.sprite_frame;
        projectile.draw_facing_west = projectile.facing_west;
        projectile.draw_visible = true;

        projectile.x_pixels += projectile.facing_west
            ? -projectile.x_speed_pixels
            : projectile.x_speed_pixels;
        if (projectile.horizontal_homing) {
            if (projectile.x_pixels < player_x) {
                projectile.x_pixels += 4;
            } else if (projectile.x_pixels > player_x) {
                projectile.x_pixels -= 4;
            }
        }
        projectile.y_pixels += projectile.y_speed_pixels;
        if (projectile.vertical_wobble) {
            projectile.y_pixels += random_below(2) - random_below(2);
        }
        if (projectile.sprite_frame < projectile.last_sprite_frame) {
            ++projectile.sprite_frame;
        }
        ++projectile.animation_tick;
    }
}

void GameLevel::advance_enemy_animation(EnemyState& enemy) {
    if (enemy.behaviour == 8 || enemy.behaviour == 99) {
        return;
    }
    const auto& sprite = enemy_sprite_info_[enemy.type];
    // 0BA5:1AD7 increments first and then applies one of three loops. BF94
    // (attack_pose_ticks) is not the projectile launch countdown: selector
    // one uses it to hold the attack range for 10..24 eligible updates.
    ++enemy.sprite_frame;
    if (enemy.attack_pose_ticks != 0) {
        if (sprite.attack_first >= 0) {
            if (enemy.sprite_frame > sprite.attack_last) {
                enemy.sprite_frame = sprite.attack_first;
            }
        } else if (enemy.sprite_frame > sprite.movement_last) {
            enemy.sprite_frame = std::max(0, sprite.movement_first);
        }
    } else if (enemy.attacking) {
        if (enemy.sprite_frame > sprite.attack_last) {
            enemy.attacking = false;
            enemy.sprite_frame = std::max(0, sprite.movement_first);
        }
    } else if (enemy.sprite_frame > sprite.movement_last) {
        enemy.sprite_frame = std::max(0, sprite.movement_first);
    }

    // BFC4 is initialized to one for every ordinary slot. A normal animation
    // update therefore arms one skipped update; an active BF94 pose suppresses
    // that skip and counts down on every eligible update instead.
    if (enemy.attack_pose_ticks == 0) {
        enemy.animation_delay_ticks = enemy.animation_delay_reset_ticks;
    } else {
        enemy.animation_delay_ticks = 0;
        --enemy.attack_pose_ticks;
    }
}

void GameLevel::collide_enemies_with_player() {
    const int player_x = player_.x_half_tiles * 8;
    const int player_hitbox_x = player_x + player_hitbox_x_offset;
    const int player_hitbox_y = player_.y_pixels + player_hitbox_y_offset;
    if (progress_.invisibility_ticks != 0) {
        return;
    }

    for (const auto& enemy : enemies_) {
        if (!enemy.active || enemy.spawn_ticks != 0) {
            continue;
        }
        const auto& frames = enemy_frames_[enemy.type][enemy.facing_west ? 1 : 0];
        if (frames.empty()) {
            continue;
        }
        const int width = frames.front().width;
        const int height = frames.front().height;
        const bool touches_player =
            enemy.x_pixels < player_hitbox_x + player_hitbox_width &&
            enemy.x_pixels + width > player_hitbox_x &&
            enemy.y_pixels < player_hitbox_y + player_hitbox_height &&
            enemy.y_pixels + height > player_hitbox_y;
        if (!touches_player) {
            continue;
        }

        // 0BA5:012B-0146 makes contact with a current extra-hit count above
        // 20 immediately fatal on the selectable skills. Only ordinary
        // contact observes the shared damage cooldown.
        if (enemy.health > 20 && skill_ != 3) {
            progress_.health = 0;
        } else if (progress_.damage_cooldown == 0) {
            progress_.health = std::max(0,
                progress_.health - damage_amount_);
            progress_.damage_cooldown = 20;
            emit_sound(8);
        }
    }
}

void GameLevel::update_enemies() {
    const int player_x = player_.x_half_tiles * 8;

    for (auto& enemy : enemies_) {
        if (!enemy.active) {
            continue;
        }
        enemy.health_bar_visible = enemy.health > 20 ||
                                   enemy.behaviour == 8 ||
                                   enemy.behaviour == 99;
        if (enemy.spawn_ticks > 0) {
            const int direction = enemy.facing_west ? 1 : 0;
            const auto& spawn_frames = enemy_frames_[enemy.type][direction];
            const int spawn_width = spawn_frames.empty()
                ? tile_size * 2 : spawn_frames.front().width;
            const int spawn_height = spawn_frames.empty()
                ? tile_size * 2 : spawn_frames.front().height;
            // 0BA5:1F3D gives each morphing slot a one-in-five chance to add
            // a Twinks cell around its screen-space centre. Preserve the five
            // RNG calls made by 1F49-1F7F when the chance succeeds.
            if (random_below(5) == 0) {
                const int centre_y = enemy.y_pixels - camera_pixel_y() +
                                     spawn_height / 2;
                const int twinkle_y = centre_y + random_below(12) -
                                      random_below(12);
                const int centre_x = enemy.x_pixels - camera_pixel_x() +
                                     spawn_width / 2;
                const int twinkle_x = centre_x + random_below(3) -
                                      random_below(3);
                spawn_twinkle(twinkle_x, twinkle_y);
            }
            --enemy.spawn_ticks;
            continue;
        }

        const auto& frames = enemy_frames_[enemy.type][enemy.facing_west ? 1 : 0];
        if (frames.empty()) {
            continue;
        }
        const int width = frames.front().width;
        const int height = frames.front().height;

        const bool shared_enemy = enemy.behaviour != 8 &&
                                  enemy.behaviour != 99;
        bool skip_shared_movement = false;
        if (shared_enemy && enemy.attack_ticks != 0) {
            // BF64 has priority over the animation gate. It freezes movement
            // and animation, decrements, and still enters the common firing
            // test in the same update (12D9 -> 1C12 -> 1C1A).
            --enemy.attack_ticks;
            skip_shared_movement = true;
        } else if (shared_enemy && enemy.animation_delay_ticks != 0) {
            --enemy.animation_delay_ticks;
            skip_shared_movement = true;
        }

        // The DOS engine stores enemy X in four-pixel units and Y in pixels.
        // 0BA5:1350-1368 applies those velocities before dispatching through
        // the active-behaviour jump table at 0BA5:1478.
        const int old_x = enemy.x_pixels;
        const int old_y = enemy.y_pixels;
        const bool used_vertical_motion = shared_enemy &&
            !skip_shared_movement && enemy.vertical_state != -99;
        if (shared_enemy && !skip_shared_movement) {
            if (used_vertical_motion) {
                enemy.y_pixels += enemy.vertical_state;
                ++enemy.vertical_state;
                if (enemy.vertical_state == 7) {
                    enemy.vertical_state = -99;
                    enemy.sprite_frame = 0;
                }
            } else {
                enemy.x_pixels += enemy.x_velocity_pixels;
                enemy.y_pixels += enemy.y_velocity_pixels;
            }
        }
        const bool terrain_collision = shared_enemy &&
            !skip_shared_movement && !used_vertical_motion &&
            enemy_collides_at(enemy.x_pixels, enemy.y_pixels, width, height);

        if (!skip_shared_movement) {
        const auto reverse_patrol_if_blocked = [&]() {
            const int leading_x = enemy.facing_west
                ? enemy.x_pixels
                : enemy.x_pixels + width - 1;
            const int column = floor_div(leading_x, tile_size);
            const int bottom = floor_div(
                enemy.y_pixels + height - 1, tile_size);
            if (terrain_collision || !solid(column, bottom + 1)) {
                enemy.x_pixels = old_x;
                enemy.y_pixels = old_y;
                enemy.facing_west = !enemy.facing_west;
                enemy.x_velocity_pixels = enemy.facing_west ? -4 : 4;
            }
        };
        switch (enemy.behaviour) {
        case 0: {
            reverse_patrol_if_blocked();
            const bool faces_player =
                (enemy.x_pixels <= player_x && !enemy.facing_west) ||
                (enemy.x_pixels > player_x && enemy.facing_west);
            if (faces_player &&
                enemy_definitions_[enemy.type].words[7] != 0 &&
                random_below(enemy_fire_random_modulus_) == 0) {
                // The 16CF branch arms DS:C064 with nine; the shared tail
                // fires when it counts down to one.
                enemy.attack_ticks = 9;
                const auto& sprite = enemy_sprite_info_[enemy.type];
                if (sprite.attack_first >= 0) {
                    enemy.sprite_frame = sprite.attack_first;
                }
            } else if (random_below(20) == 0) {
                enemy.facing_west = enemy.x_pixels > player_x;
                enemy.x_velocity_pixels = enemy.facing_west ? -4 : 4;
            }
            break;
        }
        case 1: {
            reverse_patrol_if_blocked();
            if (random_below(20) == 0) {
                // 19E3-1A8E owns a distinct BF94 pose timer; it does not
                // delay the projectile-launch dispatcher.
                enemy.facing_west = enemy.x_pixels > player_x;
                enemy.attack_pose_ticks = random_below(15) + 10;
                const auto& sprite = enemy_sprite_info_[enemy.type];
                if (sprite.attack_first >= 0) {
                    enemy.sprite_frame = sprite.attack_first;
                }
            }
            enemy.x_velocity_pixels = enemy.facing_west ? -4 : 4;
            break;
        }
        case 7: {
            // No registered .007 record selects mode seven. The dormant DOS
            // branch at 182B scans all ten Hocus-shot slots and starts the
            // enemy's -6..+6 vertical arc when a shot crosses its X. Keep the
            // intended slot-local effect without reproducing the original's
            // out-of-bounds SI clobber after that scan.
            if (enemy.vertical_state == -99) {
                const bool crossed = std::any_of(
                    projectiles_.begin(), projectiles_.end(),
                    [&enemy](const ProjectileState& projectile) {
                        return projectile.active &&
                            std::abs(projectile.x_pixels - enemy.x_pixels) < 40;
                    });
                if (crossed) {
                    enemy.vertical_state = -6;
                }
            }
            if (random_below(20) == 0) {
                enemy.facing_west = enemy.x_pixels > player_x;
                enemy.x_velocity_pixels = enemy.facing_west ? -4 : 4;
            }
            break;
        }
        case 2:
        case 3:
        case 6: {
            const bool retarget = terrain_collision || enemy.movement_ticks <= 0;
            if (retarget) {
                enemy.x_pixels = old_x;
                enemy.y_pixels = old_y;
                enemy.movement_ticks = random_below(30) + 5;

                const auto random_signed_unit = [this]() {
                    return random_below(2) - random_below(2);
                };
                do {
                    if (enemy.behaviour == 2 &&
                        enemy_definitions_[enemy.type].words[5] != 0) {
                        const int magnitude = random_below(2) * 4;
                        enemy.x_velocity_pixels =
                            enemy.x_pixels > player_x ? -magnitude : magnitude;
                    } else {
                        const int scale = enemy.behaviour == 6 ? 8 : 4;
                        enemy.x_velocity_pixels = random_signed_unit() * scale;
                    }
                    enemy.y_velocity_pixels = random_signed_unit() * 4;
                } while ((enemy.behaviour == 6 &&
                          enemy.x_velocity_pixels == 0) ||
                         (enemy.behaviour != 6 &&
                          enemy.x_velocity_pixels == 0 &&
                          enemy.y_velocity_pixels == 0));
            }
            // 0BA5:1555 changes selector three's facing only when it chooses
            // a new vector. Selector six updates facing every active tick.
            if ((enemy.behaviour == 3 && retarget) ||
                enemy.behaviour == 6) {
                enemy.facing_west = enemy.x_pixels > player_x;
            }
            --enemy.movement_ticks;
            break;
        }
        case 4:
            // Selector four jumps straight to the shared animation/attack
            // tail and has no movement branch.
            enemy.x_pixels = old_x;
            enemy.y_pixels = old_y;
            break;
        case 5:
            // 0BA5:1AB1 only faces the player.
            enemy.x_pixels = old_x;
            enemy.y_pixels = old_y;
            enemy.facing_west = enemy.x_pixels > player_x;
            break;
        case 8: {
            // The E3L9 boss bypasses the ordinary behaviour jump table and
            // runs 0BA5:0CE8. Frame one may fire only while the boss is to
            // Hocus's right; the original projectile helper then aims west.
            enemy.x_pixels = old_x;
            enemy.y_pixels = old_y;
            enemy.facing_west = enemy.behaviour_frame >= 2;
            enemy.draw_x_pixels = enemy.x_pixels;
            enemy.draw_y_pixels = enemy.y_pixels;
            enemy.draw_sprite_frame = enemy.behaviour_frame;
            enemy.draw_facing_west = enemy.facing_west;
            enemy.draw_state_valid = true;
            if (enemy.behaviour_frame == 1 && random_below(10) == 0 &&
                enemy.x_pixels > player_x) {
                EnemyState aimed = enemy;
                aimed.facing_west = true;
                // 0CE8 passes a literal zero delay to 0922.
                (void)spawn_enemy_projectile(aimed, width, height, 0);
                emit_sound(12);
            }

            if (enemy.movement_ticks == 0) {
                enemy.movement_ticks = random_below(10) + 10;
                const int distance = enemy.anchor_x_pixels - enemy.x_pixels;
                if (distance > 0) {
                    if (distance >= 20 || random_below(2) == 0) {
                        enemy.x_pixels += 4;
                        --enemy.behaviour_frame;
                    } else {
                        enemy.x_pixels -= 4;
                        ++enemy.behaviour_frame;
                    }
                } else {
                    if (-distance >= 20 || random_below(2) == 0) {
                        enemy.x_pixels -= 4;
                        ++enemy.behaviour_frame;
                    } else {
                        enemy.x_pixels += 4;
                        --enemy.behaviour_frame;
                    }
                }
            }
            if (enemy.behaviour_frame > 3) {
                enemy.behaviour_frame = 0;
            } else if (enemy.behaviour_frame < 0) {
                enemy.behaviour_frame = 3;
            }
            --enemy.movement_ticks;
            enemy.facing_west = enemy.behaviour_frame >= 2;
            break;
        }
        case 99: {
            // E4L9's boss uses the isolated 0BA5:1019 controller. Its three
            // attack phases wind up, launch one aimed missile, then idle for
            // 5..49 ticks. Crossing a 200-hit boundary starts a 20-tick morph
            // before the next position/health stage is created.
            enemy.x_pixels = old_x;
            enemy.y_pixels = old_y;
            enemy.facing_west = enemy.x_pixels > player_x;
            enemy.draw_x_pixels = enemy.x_pixels;
            enemy.draw_y_pixels = enemy.y_pixels;
            enemy.draw_sprite_frame = enemy.behaviour_frame;
            enemy.draw_facing_west = enemy.facing_west;
            enemy.draw_state_valid = true;

            if (enemy.attack_ticks == 0) {
                if (enemy.behaviour_phase == 0) {
                    enemy.behaviour_frame = 1;
                    enemy.behaviour_phase = 1;
                    enemy.attack_ticks = 4;
                } else if (enemy.behaviour_phase == 1) {
                    enemy.behaviour_phase = 2;
                    enemy.attack_ticks = 1;
                    // 1019 also launches with a literal zero delay.
                    (void)spawn_enemy_projectile(enemy, width, height, 0);
                } else if (enemy.behaviour_phase == 2) {
                    enemy.behaviour_frame = 0;
                    enemy.behaviour_phase = 0;
                    enemy.attack_ticks = random_below(45) + 5;
                }
            } else {
                --enemy.attack_ticks;
            }

            const int stage = enemy.boss_stage;
            if (stage >= 0 &&
                stage < static_cast<int>(boss4_next_threshold.size()) &&
                boss4_next_threshold[stage] > enemy.health) {
                if (enemy.behaviour_phase < 3) {
                    enemy.behaviour_phase = 3;
                    enemy.behaviour_frame = 2;
                    enemy.attack_ticks = 20;
                    emit_sound(5);
                } else if (enemy.attack_ticks == 0 &&
                           stage + 1 < static_cast<int>(boss4_health.size())) {
                    enemy.active = false;
                    --active_enemy_count_;
                    spawn_episode_four_boss(stage + 1, false);
                    continue;
                }
            }
            break;
        }
        default:
            enemy.x_pixels = old_x;
            enemy.y_pixels = old_y;
            break;
        }
        }

        if (shared_enemy && !skip_shared_movement) {
            advance_enemy_animation(enemy);
        }

        const auto& definition = enemy_definitions_[enemy.type];
        if (shared_enemy && definition.words[7] != 0) {
            // 1C25 consumes this RNG sample for every fire-capable ordinary
            // enemy on every update, including selector zero and BF64/BFB4
            // early paths.
            const bool random_attack =
                random_below(enemy_fire_random_modulus_) == 0;
            if ((enemy.behaviour != 0 && random_attack) ||
                enemy.attack_ticks == 1) {
                const auto& sprite = enemy_sprite_info_[enemy.type];
                enemy.attacking = true;
                bool launch_allowed = true;
                if (definition.words[2] != 0 &&
                    definition.words[6] != 0) {
                    launch_allowed =
                        (enemy.x_pixels <= player_x && !enemy.facing_west) ||
                        (enemy.x_pixels > player_x && enemy.facing_west);
                }
                if (!launch_allowed ||
                    !spawn_enemy_projectile(enemy, width, height)) {
                    enemy.attacking = false;
                }
                if (sprite.attack_first >= 0) {
                    enemy.sprite_frame = sprite.attack_first;
                }
            }
        }

        if (shared_enemy && enemy.vertical_state != -99) {
            const int airborne = enemy_sprite_info_[enemy.type].airborne_frame;
            if (airborne >= 0) {
                enemy.sprite_frame = airborne;
            }
        }
        if (shared_enemy) {
            enemy.draw_x_pixels = enemy.x_pixels;
            enemy.draw_y_pixels = enemy.y_pixels;
            enemy.draw_sprite_frame = enemy.sprite_frame;
            enemy.draw_facing_west = enemy.facing_west;
            enemy.draw_state_valid = true;
        }
        // Original rendering observes the pre-decrement C014 parity. Native
        // rendering happens after tick(), so retain that choice explicitly.
        enemy.hit_flash_masked = enemy.hit_cooldown % 2 != 0;
        if (enemy.hit_cooldown > 0) {
            --enemy.hit_cooldown;
        }
        ++enemy.animation_tick;
    }
}

void GameLevel::release_distant_enemies() {
    const int camera_x = camera_pixel_x();
    const int camera_y = camera_pixel_y();

    // 0BA5:0439 runs after the enemy update. It frees ordinary enemies outside
    // the expanded camera rectangle without clearing the persistent event, so
    // revisiting the trigger can create them again.
    for (auto& enemy : enemies_) {
        if (!enemy.active || enemy.health >= 500) {
            continue;
        }
        if (enemy.x_pixels < camera_x - 200 ||
            enemy.x_pixels > camera_x + 480 ||
            enemy.y_pixels < camera_y - 120 ||
            enemy.y_pixels > camera_y + 510) {
            enemy.active = false;
            enemy.spawn_cell = -1;
            --active_enemy_count_;
        }
    }
}

void GameLevel::process_events(bool action_pressed) {
    // The nested loops at 0BA5:3A04-452D check the six cells covered by
    // Hocus: columns center-1 through center+1 on his current and next rows.
    // Event processing precedes movement in the outer loop at
    // 0BA5:4DAB-4DC6.
    const int top_row = floor_div(player_.y_pixels, tile_size);
    const int center = floor_div(player_.x_half_tiles + 1, 2);
    for (int offset = -1; offset <= 1; ++offset) {
        const int column = center + offset;
        for (int row_offset = 0; row_offset < 2; ++row_offset) {
            const int row = top_row + row_offset;
            if (column < 0 || column >= map_width ||
                row < 0 || row >= map_height) {
                continue;
            }
            const auto cell = static_cast<std::size_t>(row) * map_width + column;
            const auto event = event_layer_[cell];
            if (event != empty_event && event < 23) {
                process_item_event(cell, event, action_pressed, offset == 0);
            } else if (event >= 23 && event < 33) {
                process_teleporter_event(cell, event, offset == 0);
            } else if (event >= 33 && event < 56) {
                process_switch_event(cell, event, action_pressed, offset == 0);
            } else if (event >= 56 && event < 106) {
                process_gate_event(cell, event, offset == 0);
            } else if (event >= 116 && event < 366) {
                process_enemy_trigger(event, offset == 0);
            }
        }
    }
}

void GameLevel::update_horizontal_motion(const InputState& input) {
    player_.moved_this_tick = false;
    const auto collision = horizontal_collision_flags();

    if (input.left && player_.x_half_tiles > 0) {
        // A direction change consumes the first tick without displacement,
        // matching the facing flag at DS:CE90.
        if (!player_.facing_west) {
            player_.facing_west = true;
        } else if (collision.left) {
            --player_.x_half_tiles;
            player_.moved_this_tick = true;
        } else if (collision.step_left) {
            --player_.x_half_tiles;
            player_.y_pixels -= tile_size;
            player_.moved_this_tick = true;
        }
    } else if (input.right && player_.x_half_tiles < 0x1DD) {
        if (player_.facing_west) {
            player_.facing_west = false;
        } else if (collision.right) {
            ++player_.x_half_tiles;
            player_.moved_this_tick = true;
        } else if (collision.step_right) {
            ++player_.x_half_tiles;
            player_.y_pixels -= tile_size;
            player_.moved_this_tick = true;
        }
    }
}

void GameLevel::update_elevator(const InputState& input) {
    if (elevator_left_tile_ < 0 || elevator_right_tile_ < 0 ||
        (!input.action && !input.down)) {
        return;
    }

    const int player_row = floor_div(player_.y_pixels, tile_size);
    const int center = floor_div(player_.x_half_tiles + 1, 2);
    const int platform_row = player_row + 2;
    if (center < 0 || center >= map_width ||
        platform_row < 0 || platform_row >= map_height) {
        return;
    }

    std::size_t platform =
        static_cast<std::size_t>(platform_row) * map_width + center;
    const int sampled_tile = main_layer_[platform];
    if (sampled_tile == elevator_right_tile_) {
        if (center == 0) {
            return;
        }
        --platform;
    } else if (sampled_tile != elevator_left_tile_) {
        return;
    }
    if (platform + 1 >= main_layer_.size() ||
        main_layer_[platform] != elevator_left_tile_ ||
        main_layer_[platform + 1] != elevator_right_tile_) {
        return;
    }

    if (input.action) {
        // 0BA5:64A9-6519 checks the player's new top row, carries Hocus up
        // one tile, then moves the two elevator cells up one map row.
        if (platform < static_cast<std::size_t>(map_width * 3)) {
            return;
        }
        const auto clearance = platform - map_width * 3;
        if (main_layer_[clearance] != 0xFF ||
            main_layer_[clearance + 1] != 0xFF) {
            return;
        }
        const auto destination = platform - map_width;
        player_.y_pixels -= tile_size;
        main_layer_[destination] = main_layer_[platform];
        main_layer_[destination + 1] = main_layer_[platform + 1];
        main_layer_[platform] = 0xFF;
        main_layer_[platform + 1] = 0xFF;
    } else {
        // 0BA5:6526-6596 performs the symmetric downward move after checking
        // the next elevator row for two empty cells.
        const auto destination = platform + map_width;
        if (destination + 1 >= main_layer_.size() ||
            main_layer_[destination] != 0xFF ||
            main_layer_[destination + 1] != 0xFF) {
            return;
        }
        player_.y_pixels += tile_size;
        main_layer_[destination] = main_layer_[platform];
        main_layer_[destination + 1] = main_layer_[platform + 1];
        main_layer_[platform] = 0xFF;
        main_layer_[platform + 1] = 0xFF;
    }
    player_.jumping = false;
    player_.falling = false;
    player_.jump_index = 0;
}

void GameLevel::spawn_projectile(bool vertical) {
    if (active_projectile_count_ >= progress_.firepower) {
        return;
    }
    const auto free = std::find_if(projectiles_.begin(), projectiles_.end(),
                                   [](const ProjectileState& projectile) {
                                       return !projectile.active;
                                   });
    if (free == projectiles_.end()) {
        return;
    }

    const bool laser = progress_.laser_shots > 0;
    if (laser) {
        --progress_.laser_shots;
    }
    const int player_x = player_.x_half_tiles * 8;
    *free = {
        vertical ? player_x + 4
                 : player_x + (player_.facing_west ? -8 : 8),
        player_.y_pixels + (vertical ? 0 : 8),
        player_.facing_west,
        vertical,
        laser,
        true,
    };
    ++active_projectile_count_;
    player_.firing_ticks = 2;
    player_.firing_vertical = vertical;
    emit_sound(progress_.super_shot_ticks > 0 ? 13 : 0);
}

void GameLevel::apply_cheat(const CheatCode cheat) noexcept {
    // INT 09h handler 1392:0475-04E7 applies the first four state changes
    // after recognizing FEELGOOD, BLAKE, QUARK, and BANANA by scan-code sum.
    // Mid-air jump is a native persistent-menu addition with no one-shot
    // state mutation.
    switch (cheat) {
    case CheatCode::full_health:
        progress_.health = 100;
        break;
    case CheatCode::both_keys:
        progress_.silver_key = true;
        progress_.gold_key = true;
        break;
    case CheatCode::rapid_fire:
        if (progress_.super_shot_ticks == 0) {
            saved_firepower_ = progress_.firepower;
            progress_.firepower = 5;
        }
        progress_.super_shot_ticks = 600;
        break;
    case CheatCode::laser_shots:
        if (progress_.laser_shots == 0) {
            progress_.laser_shots = 3;
        }
        laser_indicator_visible_ = true;
        laser_indicator_ticks_ = 5;
        break;
    case CheatCode::midair_jump:
        break;
    }
}

bool GameLevel::cheat_enabled(const CheatCode cheat) const noexcept {
    return enabled_cheats_[static_cast<std::size_t>(cheat)];
}

void GameLevel::set_cheat_enabled(const CheatCode cheat,
                                  const bool enabled) noexcept {
    auto& state = enabled_cheats_[static_cast<std::size_t>(cheat)];
    if (state == enabled) {
        return;
    }
    state = enabled;
    if (enabled) {
        apply_cheat(cheat);
        enforce_enabled_cheats();
        return;
    }

    // QUARK normally expires through 0BA5:5806 and restores the firepower
    // saved by the original cheat/pickup path. Turning its persistent menu
    // toggle off performs that same restoration immediately.
    if (cheat == CheatCode::rapid_fire) {
        progress_.super_shot_ticks = 0;
        progress_.firepower = saved_firepower_;
    }
}

void GameLevel::enforce_enabled_cheats() noexcept {
    if (cheat_enabled(CheatCode::full_health)) {
        progress_.health = 100;
    }
    if (cheat_enabled(CheatCode::both_keys)) {
        progress_.silver_key = true;
        progress_.gold_key = true;
    }
    if (cheat_enabled(CheatCode::rapid_fire)) {
        progress_.firepower = 5;
        progress_.super_shot_ticks = 600;
    }
    if (cheat_enabled(CheatCode::laser_shots) &&
        progress_.laser_shots < 3) {
        const bool was_empty = progress_.laser_shots == 0;
        progress_.laser_shots = 3;
        if (was_empty) {
            laser_indicator_visible_ = true;
            laser_indicator_ticks_ = 5;
        }
    }
}

void GameLevel::kill_all_enemies() {
    const int camera_x = camera_pixel_x();
    const int camera_y = camera_pixel_y();

    // 0BA5:01B6 walks all eight enemy slots. Every active slot gets one
    // centred burst, sound 10, and its persistent spawn marker cleared.
    for (auto& enemy : enemies_) {
        if (!enemy.active) {
            continue;
        }
        const int direction = enemy.facing_west ? 1 : 0;
        const auto& frames = enemy_frames_[enemy.type][direction];
        const int width = frames.empty() ? tile_size * 2 : frames.front().width;
        const int height = frames.empty() ? tile_size * 2 : frames.front().height;
        spawn_explosion(enemy.x_pixels - camera_x + width / 2,
                        enemy.y_pixels - camera_y + height / 2);
        emit_sound(10);
        if (enemy.spawn_cell >= 0 &&
            enemy.spawn_cell < static_cast<int>(event_layer_.size())) {
            event_layer_[static_cast<std::size_t>(enemy.spawn_cell)] =
                empty_event;
        }
        enemy.active = false;
    }
    active_enemy_count_ = 0;
}

void GameLevel::hit_enemy(EnemyState& enemy,
                          ProjectileState& projectile) {
    // 0BA5:03A4-03C6 treats the definition's health as extra hits: zero is
    // still alive and the following projectile kills the enemy. The branch at
    // 03AF sends a laser directly to the death path regardless of that count;
    // the laser projectile itself remains active.
    const int direction = enemy.facing_west ? 1 : 0;
    const auto& frames = enemy_frames_[enemy.type][direction];
    const int width = frames.empty() ? tile_size * 2 : frames.front().width;
    const int height = frames.empty() ? tile_size * 2 : frames.front().height;
    const int camera_x = camera_pixel_x();
    const int camera_y = camera_pixel_y();
    const int burst_x = enemy.x_pixels - camera_x + width / 2;
    const int burst_y = enemy.y_pixels - camera_y + height / 2;

    // The collision path at 0BA5:0353 creates one impact burst on every hit.
    spawn_explosion(burst_x, burst_y);
    if (enemy.health == -2) {
        // The -2 definition value is a level-specific kill-all sentinel.
        // 0BA5:02ED changes it to -1 before dispatching 0BA5:01B6.
        enemy.health = -1;
        kill_all_enemies();
    } else if (enemy.health == -1) {
        // 0BA5:0396-03A1 ignores an already-killed sentinel after creating the
        // common impact burst and consuming a non-laser shot.
    } else if (enemy.health != 0 && !projectile.laser) {
        --enemy.health;
        enemy.hit_cooldown = 10;
    } else {
        // The death path at 03CE adds three more bursts at the same centre.
        spawn_explosion(burst_x, burst_y);
        spawn_explosion(burst_x, burst_y);
        spawn_explosion(burst_x, burst_y);
        const bool was_active = enemy.active;
        enemy.active = false;
        if (enemy.spawn_cell >= 0 &&
            enemy.spawn_cell < static_cast<int>(event_layer_.size())) {
            event_layer_[static_cast<std::size_t>(enemy.spawn_cell)] =
                empty_event;
        }
        if (was_active) {
            --active_enemy_count_;
        }
        emit_sound(10);
    }
    if (!projectile.laser) {
        projectile.active = false;
        --active_projectile_count_;
    }
}

void GameLevel::collide_projectiles_with_enemies() {
    // 0BA5:0267 is an enemy-major, projectile-minor pass and runs before
    // either pool moves. Multiple ordinary shots can therefore hit one enemy
    // in a tick, while a laser can continue through multiple enemy slots.
    for (auto& enemy : enemies_) {
        if (!enemy.active || enemy.spawn_ticks != 0) {
            continue;
        }
        const auto& frames =
            enemy_frames_[enemy.type][enemy.facing_west ? 1 : 0];
        if (frames.empty()) {
            continue;
        }
        const auto& sprite = frames.front();
        for (auto& projectile : projectiles_) {
            if (!projectile.active) {
                continue;
            }
            // The spell collision coordinate is the recovered (x,y+2) point,
            // not the 16x15 artwork footprint.
            const int hit_x = projectile.x_pixels;
            const int hit_y = projectile.y_pixels + 2;
            const bool point_inside =
                hit_x >= enemy.x_pixels &&
                hit_x < enemy.x_pixels + sprite.width &&
                hit_y >= enemy.y_pixels &&
                hit_y < enemy.y_pixels + sprite.height;
            if (point_inside) {
                hit_enemy(enemy, projectile);
            }
        }
    }
}

bool GameLevel::update_level_completion() {
    // The outer level loop compares DS:704A (required crystals) with DS:7048
    // (collected crystals) at 0BA5:4FE3. Equality starts the 90-tick completion
    // sequence; the runner exits when the countdown reaches one (0BA5:690A).
    if (!level_complete_ && level_complete_ticks_ == 0 &&
        progress_.total_crystals > 0 &&
        progress_.crystals == progress_.total_crystals) {
        level_complete_ticks_ = 90;
        progress_.invisibility_ticks = 30000;
        // 0BA5:5005 calls the enemy-slot purge at 0BA5:01B6. It does not
        // clear Hocus's projectile pool here.
        kill_all_enemies();
    }
    if (level_complete_ticks_ > 1) {
        --level_complete_ticks_;
        // 0BA5:5016-50A9 emits two Twinks around the vanished player on one
        // in seven updates while more than 25 transition ticks remain.
        if (random_below(7) == 0 && level_complete_ticks_ > 25) {
            const int player_x = player_.x_half_tiles * 8;
            const int camera_x = camera_pixel_x();
            const int camera_y = camera_pixel_y();
            const auto emit_transition_twinkle = [this, player_x, camera_x,
                                                   camera_y]() {
                const int y = player_.y_pixels - camera_y +
                              random_below(12) - random_below(12);
                const int x = player_x - camera_x +
                              (random_below(3) - random_below(3)) * 4;
                spawn_twinkle(x, y);
            };
            emit_transition_twinkle();
            emit_transition_twinkle();
            emit_sound(9);
        }
        if (level_complete_ticks_ == 1) {
            level_complete_ = true;
        }
    }
    return level_complete_ticks_ != 0;
}

bool GameLevel::update_death() {
    // DS:CE8C is independent of the crystal-completion counter. 4E8B starts
    // it at 90 when health reaches zero, and 68FF exits the level at one.
    if (!player_dead_ && death_ticks_ == 0 && progress_.health == 0) {
        death_ticks_ = 90;
        progress_.invisibility_ticks = 30000;
    }
    if (death_ticks_ > 1) {
        --death_ticks_;
        // 0BA5:4EB4-4FE2 guarantees the first burst and otherwise emits one
        // on one in four updates while more than 25 transition ticks remain.
        const int random_gate = random_below(4);
        if (death_ticks_ == 89 ||
            (death_ticks_ > 25 && random_gate == 0)) {
            const int player_x = player_.x_half_tiles * 8;
            const int camera_x = camera_pixel_x();
            const int camera_y = camera_pixel_y();
            const auto emit_death_pair = [this, player_x, camera_x, camera_y]() {
                const int burst_y = player_.y_pixels - camera_y +
                                    random_below(10) + 16 - random_below(10);
                const int burst_x = player_x - camera_x +
                                    random_below(10) - random_below(10);
                spawn_explosion(burst_x, burst_y);
                const int twinkle_y = player_.y_pixels - camera_y +
                                      random_below(12) - random_below(12);
                const int twinkle_x = player_x - camera_x +
                    (random_below(3) - random_below(3)) * 4;
                spawn_twinkle(twinkle_x, twinkle_y);
            };
            emit_death_pair();
            emit_death_pair();
            emit_sound(10);
        }
        if (death_ticks_ == 1) {
            player_dead_ = true;
        }
    }
    return death_ticks_ != 0;
}

void GameLevel::update_projectiles() {
    // 0BA5:5806-5821 decrements the 600-tick super-shot counter in the
    // projectile dispatcher and restores the firepower saved at pickup when
    // the counter reaches one.
    if (progress_.super_shot_ticks > 0) {
        --progress_.super_shot_ticks;
        if (progress_.super_shot_ticks == 1) {
            progress_.super_shot_ticks = 0;
            progress_.firepower = saved_firepower_;
        }
    }

    const int camera_x = camera_pixel_x();
    const int camera_y = camera_pixel_y();

    for (auto& projectile : projectiles_) {
        if (!projectile.active) {
            continue;
        }

        const int screen_x = projectile.x_pixels - camera_x;
        const int screen_y = projectile.y_pixels - camera_y;
        const auto tile_value = [this](const int column, const int row) {
            if (column < 0 || column >= map_width ||
                row < 0 || row >= map_height) {
                return std::uint8_t{0};
            }
            return main_layer_[static_cast<std::size_t>(row) * map_width +
                               column];
        };
        const auto break_tile = [this](const int column, const int row) {
            if (column < 0 || column >= map_width ||
                row < 0 || row >= map_height) {
                return false;
            }
            const auto cell = static_cast<std::size_t>(row) * map_width +
                              column;
            if (main_layer_[cell] != breakable_main_tile_) {
                return false;
            }
            main_layer_[cell] = 0xFF;
            return true;
        };

        // The three branches at 0BA5:5853-5F61 inspect the projectile's
        // current map position before applying the sixteen-pixel step. Their
        // breakable-wall probes and ordinary-solid probes are deliberately
        // different shapes, and a broken tile creates a Twink (2174), not the
        // sixteen-particle solid-wall burst (2F1C).
        bool collision = false;
        if (projectile.vertical) {
            const int column = floor_div(projectile.x_pixels, tile_size) +
                               (projectile.facing_west ? 1 : 0);
            const int row = std::max(0,
                floor_div(projectile.y_pixels, tile_size) - 1);
            if (break_tile(column, row)) {
                spawn_twinkle(screen_x - 4, screen_y - 15);
                collision = true;
            } else if (break_tile(column + 1, row)) {
                spawn_twinkle(screen_x + 12, screen_y - 15);
                collision = true;
            } else if (tile_value(column, row) != 0xFF) {
                spawn_explosion(screen_x + 8, screen_y + 2);
                collision = true;
            }
        } else {
            const int column = floor_div(projectile.x_pixels, tile_size) + 1;
            const int row = floor_div(projectile.y_pixels, tile_size);
            if (projectile.facing_west) {
                if (break_tile(column, row)) {
                    spawn_twinkle(screen_x, screen_y - 8);
                    collision = true;
                } else if (break_tile(column, row + 1)) {
                    spawn_twinkle(screen_x, screen_y + 8);
                    collision = true;
                } else if (break_tile(column - 1, row)) {
                    spawn_twinkle(screen_x - 8, screen_y - 8);
                    collision = true;
                } else if (break_tile(column - 1, row + 1)) {
                    spawn_twinkle(screen_x - 8, screen_y + 8);
                    collision = true;
                } else if (tile_value(column - 1, row) != 0xFF ||
                           tile_value(column, row) != 0xFF) {
                    spawn_explosion(screen_x + 1, screen_y + 2);
                    collision = true;
                }
            } else {
                if (break_tile(column, row)) {
                    spawn_twinkle(screen_x + 8, screen_y - 8);
                    collision = true;
                } else if (break_tile(column, row + 1)) {
                    spawn_twinkle(screen_x + 8, screen_y + 8);
                    collision = true;
                } else if (break_tile(column + 1, row)) {
                    spawn_twinkle(screen_x + 24, screen_y - 8);
                    collision = true;
                } else if (break_tile(column + 1, row + 1)) {
                    spawn_twinkle(screen_x + 24, screen_y + 8);
                    collision = true;
                } else if (tile_value(column, row) != 0xFF ||
                           tile_value(column + 1, row) != 0xFF) {
                    spawn_explosion(screen_x + 24, screen_y + 10);
                    collision = true;
                }
            }
        }

        if (!collision) {
            if (projectile.vertical) {
                projectile.y_pixels -= tile_size;
            } else {
                projectile.x_pixels += projectile.facing_west
                    ? -tile_size : tile_size;
            }
            const int moved_screen_x = projectile.x_pixels - camera_x;
            const int moved_screen_y = projectile.y_pixels - camera_y;
            collision = moved_screen_x < -bullet_cell_width ||
                        moved_screen_x > viewport_width_ - 1 ||
                        moved_screen_y < 0 ||
                        moved_screen_y >= viewport_height;
        }

        if (collision) {
            projectile.active = false;
            --active_projectile_count_;
        } else if (projectile.active && random_below(10) > 2) {
            // The three projectile direction branches at 0BA5:5AC0, 5D40,
            // and 5F5B all leave a falling palette-cycling speck behind.
            spawn_spell_trail(projectile.x_pixels - camera_x,
                              projectile.y_pixels - camera_y + 5);
        }
    }
}

void GameLevel::tick(const InputState& input) {
    enforce_enabled_cheats();
    if (modal_overlay_active()) {
        // 0BA5:5405 and 38B6 are blocking input loops inside the current
        // update. The ISR clock continues, but no game-domain counter or
        // entity advances.
        if (input.action || input.fire || input.jump) {
            dismiss_modal_overlay();
        }
        return;
    }
    capture_interpolation_state();
    // 0BA5:220F decrements after presenting a flash page.  Because the native
    // renderer runs after tick(), consume the preceding frame's count here;
    // a crystal collected later in this tick still renders the initial 8.
    if (crystal_flash_ticks_ > 0) {
        --crystal_flash_ticks_;
    }
    if (level_number_flash_ticks_ > 0 &&
        ++level_number_flash_ticks_ > 20) {
        level_number_flash_ticks_ = 1;
    }
    if (!level_complete_ && !player_dead_) {
        ++elapsed_ticks_;
    }
    const bool action_pressed = input.action && !action_down_;
    const bool fire_requested = input.fire &&
        (!fire_down_ || progress_.super_shot_ticks > 0);
    const bool jump_pressed = input.jump && !jump_down_;
    action_down_ = input.action;
    fire_down_ = input.fire;
    jump_down_ = input.jump;
    // 0BA5:29C5 is the first game-owned call in each fixed update. It uses
    // the position produced by the preceding update and retains camera state.
    update_camera();

    if (player_.firing_ticks > 0) {
        --player_.firing_ticks;
    }

    if (!teleporting()) {
        // 0BA5:39F9 is inside the event dispatcher, which the outer loop skips
        // during teleportation. The shared damage cooldown pauses with it.
        if (progress_.damage_cooldown > 0) {
            --progress_.damage_cooldown;
        }
        process_events(action_pressed);
    }
    enforce_enabled_cheats();

    // 0BA5:4DD7-4E7E presents both map layers immediately after the event
    // dispatcher. Later animation, projectile, and elevator mutations do not
    // become visible until the following update.
    draw_background_layer_ = background_layer_;
    draw_main_layer_ = main_layer_;

    // 0BA5:348D runs immediately after the playfield is presented and before
    // the death/completion branches. Besides drawing the HUD it writes the
    // clamped 0..100 health value back to DS:704C.
    progress_.health = std::clamp(progress_.health, 0, 100);
    draw_progress_ = progress_;
    draw_level_number_flash_ticks_ = level_number_flash_ticks_;

    // Death/completion initialization occurs at 0BA5:4E8B-50B1, before the
    // collision and entity phases at 0BA5:546A. Completion may purge every
    // enemy before any of those collision passes can see it.
    const bool death_locked = update_death();
    const bool completion_locked = update_level_completion();
    // 0BA5:50B1-53EC draws Hocus (or the teleporter morph) from this
    // pre-movement state. Invisibility skips Hocus, the super-jump Twink, and
    // the laser indicator, and only then decrements its timer.
    capture_player_draw_state();
    if (progress_.invisibility_ticks > 0) {
        --progress_.invisibility_ticks;
        if (progress_.invisibility_ticks == 0) {
            emit_sound(5);
        }
    } else {
        // 0BA5:533C emits this pickup effect before collision/entity updates
        // and before current input can consume the available super jump.
        spawn_super_jump_twinkle();
        // 0BA5:5379-53E7 draws the current laser inventory before advancing
        // its five-update blink timer.
        if (progress_.laser_shots > 0 && --laser_indicator_ticks_ == 0) {
            laser_indicator_ticks_ = 5;
            laser_indicator_visible_ = !laser_indicator_visible_;
        }
    }

    // Exact phase order from 0BA5:546A-5482: existing Hocus shots, existing
    // enemy shots, enemy bodies, pending spawns, enemy-shot movement, enemy
    // movement, then distant-enemy release.
    collide_projectiles_with_enemies();
    collide_enemy_projectiles_with_player();
    collide_enemies_with_player();
    spawn_pending_enemies();
    update_enemy_projectiles();
    update_enemies();
    release_distant_enemies();
    update_visual_effects();
    if (tile_animation_phase_) {
        update_tile_viewport();
    }
    tile_animation_phase_ = !tile_animation_phase_;

    const bool teleporter_locked = teleporting();

    if (!completion_locked && !death_locked && !teleporter_locked &&
        fire_requested) {
        spawn_projectile(input.action);
    }
    update_projectiles();
    enforce_enabled_cheats();
    if (teleporter_locked) {
        // 0BA5:65EA updates teleport state after the projectile dispatcher.
        (void)update_teleporter();
    }
    if (completion_locked || death_locked || teleporter_locked) {
        update_camera_focus(input);
        ++player_.animation_tick;
        return;
    }

    const bool ordinary_jump_requested =
        input.jump && !player_.jumping && !player_.falling;
    const bool midair_jump_requested =
        cheat_enabled(CheatCode::midair_jump) && jump_pressed;
    if (ordinary_jump_requested || midair_jump_requested) {
        const int row = floor_div(player_.y_pixels, tile_size);
        const int column = (player_.x_half_tiles + 1) / 2;
        if (midair_jump_requested || solid(column, row + 2)) {
            player_.jumping = true;
            player_.falling = false;
            player_.jump_index = 0;
            player_.super_jump = progress_.super_jump_available;
            progress_.super_jump_available = false;
        }
    }

    update_vertical_motion();
    update_horizontal_motion(input);
    update_elevator(input);
    eject_from_floor_if_needed();
    update_camera_focus(input);
    ++player_.animation_tick;
}

int GameLevel::current_player_frame() const noexcept {
    if (player_.firing_ticks > 0) {
        return player_.firing_vertical ? 10 : 9;
    }
    if (player_.jumping || player_.falling) {
        const int apex = player_.super_jump ? super_jump_apex : normal_jump_apex;
        return player_.jumping && player_.jump_index <= apex ? 7 : 8;
    }
    if (player_.moved_this_tick) {
        return 1 + (player_.animation_tick / 2) % 6;
    }
    return 0;
}

void GameLevel::capture_player_draw_state() noexcept {
    player_draw_.x_pixels = player_.x_half_tiles * 8;
    player_draw_.y_pixels = player_.y_pixels;
    player_draw_.frame = current_player_frame();
    player_draw_.teleporter_ticks = teleporter_ticks_;
    player_draw_.invisibility_ticks = progress_.invisibility_ticks;
    player_draw_.damage_cooldown = progress_.damage_cooldown;
    player_draw_.laser_shots = progress_.laser_shots;
    player_draw_.facing_west = player_.facing_west;
    player_draw_.laser_indicator_visible = laser_indicator_visible_;
}

void GameLevel::capture_interpolation_state() noexcept {
    previous_player_draw_ = player_draw_;
    previous_projectiles_ = projectiles_;
    previous_enemies_ = enemies_;
    previous_enemy_projectiles_ = enemy_projectiles_;
    previous_camera_x_pixels_ = camera_pixel_x();
    previous_camera_y_pixels_ = camera_pixel_y();
}

void GameLevel::draw_text(DecodedImage& frame, const std::string& text,
                               int x, int y, std::uint32_t colour) const {
    int cursor = x;
    for (const unsigned char character : text) {
        if (character == ' ') {
            cursor += 8;
            continue;
        }
        if (character < 33 || character > 122) {
            cursor += 8;
            continue;
        }
        const std::size_t glyph = character - 33;
        for (int gy = 0; gy < 8; ++gy) {
            const auto bits = font_mask_[glyph * 8 + gy];
            for (int gx = 0; gx < 8; ++gx) {
                const int px = cursor + gx;
                const int py = y + gy;
                if (px >= 0 && px < frame.width && py >= 0 && py < frame.height &&
                    (bits & (1U << gx)) != 0) {
                    frame.pixels[static_cast<std::size_t>(py) * frame.width + px] =
                        colour;
                }
            }
        }
        cursor += 8;
    }
}

void GameLevel::draw_hud_values(DecodedImage& frame,
                                const int ui_x_offset) const {
    // 0BA5:3125 copies the ten 8x8 number cells from HUDSTUFF.IMG. Its
    // destination arguments are Mode-X byte offsets, so each byte of X is
    // four native pixels. 0BA5:348D centers the variable-width score and
    // health strings around byte columns 10 and 25 respectively.
    const auto digit = [this, &frame, ui_x_offset](
                           const int value, const int x, const int y) {
        if (value >= 0 && value <= 9) {
            blit_region(hud_stuff_, frame, value * 8, 0, 8, 8,
                        x + ui_x_offset, y);
        }
    };

    const auto draw_centered_number = [&digit](const int value,
                                                const int byte_column) {
        const auto text = std::to_string(std::max(value, 0));
        const int x = byte_column * 4 - static_cast<int>(text.size()) * 4;
        for (std::size_t index = 0; index < text.size(); ++index) {
            digit(text[index] - '0', x + static_cast<int>(index) * 8, 181);
        }
    };
    draw_centered_number(draw_progress_.score, 10);
    draw_centered_number(std::clamp(draw_progress_.health, 0, 100), 25);

    // 0BA5:3596/35BB convert each crystal count to decimal but then read only
    // DS:70B1, the first character. Preserve that visible registered quirk:
    // 15 crystals is shown as "1", not clamped to "9" or rendered as "15".
    digit(dos_hud_first_decimal_digit(draw_progress_.crystals), 156, 181);
    digit(dos_hud_first_decimal_digit(draw_progress_.total_crystals), 172, 181);

    // 0BA5:3297 copies one of three 8x12 cells: silver, gold, or the empty
    // key-field background. One key is centered; two occupy separate slots.
    const auto key_cell = [this, &frame, ui_x_offset](
                              const int cell, const int x) {
        blit_region(hud_stuff_, frame, 88 + cell * 8, 0, 8, 12,
                    x + ui_x_offset, 180);
    };
    key_cell(2, 212);
    key_cell(2, 220);
    const int key_count = static_cast<int>(draw_progress_.silver_key) +
                          static_cast<int>(draw_progress_.gold_key);
    if (key_count > 1) {
        key_cell(0, 212);
        key_cell(1, 220);
    } else if (key_count == 1) {
        key_cell(draw_progress_.silver_key ? 0 : 1, 216);
    }

    if (draw_level_number_flash_ticks_ > 0 &&
        draw_level_number_flash_ticks_ < 10) {
        blit_region(hud_stuff_, frame, 80, 0, 8, 8,
                    296 + ui_x_offset, 182);
    } else {
        digit(level_id_.number, 296, 182);
    }
}

void GameLevel::draw_message(DecodedImage& frame,
                             const int ui_x_offset) const {
    if (crystal_tip_visible_) {
        // 0BA5:540A passes y=46 and Mode-X x-byte 12 (48 native pixels) to
        // the planar-image renderer before entering its any-input loop.
        blit(crystal_tip_, frame, 48 + ui_x_offset, 46);
        return;
    }
    if (active_message_.empty()) {
        return;
    }
    // 0BA5:38B6 draws the proportional message directly over the alternate
    // gameplay page. It has no invented box: palette index 1 is the one-pixel
    // shadow and index 0x68 is the foreground.
    int width = 0;
    for (const auto& line : active_message_) {
        width = std::max(width, font_text_width(font_mask_, line));
    }
    const int left = (frame.width - width) / 2;
    const int top = 80 - static_cast<int>(active_message_.size()) * 12;
    for (std::size_t line = 0; line < active_message_.size(); ++line) {
        const int y = top + static_cast<int>(line) * 12;
        draw_font_text(frame, font_mask_, active_message_[line],
                       left + 1, y + 1, frame.palette[1]);
        draw_font_text(frame, font_mask_, active_message_[line],
                       left, y, frame.palette[0x68]);
    }
}

void GameLevel::draw_projectiles(DecodedImage& frame, const int camera_x,
                                 const int camera_y,
                                 const double interpolation) const {
    for (std::size_t index = 0; index < projectiles_.size(); ++index) {
        const auto& projectile = projectiles_[index];
        if (!projectile.active) {
            continue;
        }
        const auto& previous = previous_projectiles_[index];
        const bool can_interpolate = previous.active &&
            previous.vertical == projectile.vertical &&
            previous.laser == projectile.laser;
        const auto lerp = [interpolation](const int from, const int to) {
            return static_cast<int>(std::lround(
                from + (to - from) * interpolation));
        };
        const int x = can_interpolate
            ? lerp(previous.x_pixels, projectile.x_pixels)
            : projectile.x_pixels;
        const int y = can_interpolate
            ? lerp(previous.y_pixels, projectile.y_pixels)
            : projectile.y_pixels;
        const int frame_index = projectile.laser
            ? (projectile.vertical ? vertical_laser_frame
                                   : horizontal_laser_frame)
            : (projectile.vertical ? vertical_shot_frame
                                   : horizontal_shot_frame);
        const int direction = projectile.facing_west ? 1 : 0;
        blit(hocus_frames_[direction][frame_index], frame,
             x - camera_x, y - camera_y);
    }
}

void GameLevel::draw_laser_indicator(DecodedImage& frame, const int camera_x,
                                     const int camera_y, const int player_x,
                                     const int player_y) const {
    if (player_draw_.invisibility_ticks != 0 ||
        player_draw_.laser_shots <= 0 ||
        !player_draw_.laser_indicator_visible) {
        return;
    }
    // 0BA5:539C-53CC caps CE94 at three and stacks HOCUS.SPR frame 14 at
    // fourteen-pixel intervals above Hocus.  The original X argument is one
    // Mode-X byte to the right of CF2A, i.e. four native pixels.
    const int count = std::min(player_draw_.laser_shots, 3);
    const int direction = player_draw_.facing_west ? 1 : 0;
    for (int index = 0; index < count; ++index) {
        blit(hocus_frames_[direction][vertical_laser_frame], frame,
             player_x - camera_x + 4,
             player_y - camera_y - 14 - index * 14);
    }
}

void GameLevel::draw_enemy_projectiles(DecodedImage& frame,
                                       const int camera_x, const int camera_y,
                                       const double interpolation) const {
    for (std::size_t index = 0; index < enemy_projectiles_.size(); ++index) {
        const auto& projectile = enemy_projectiles_[index];
        if (!projectile.active || !projectile.draw_visible ||
            projectile.owner_type < 0 || projectile.owner_type >=
                                                   static_cast<int>(enemy_frames_.size())) {
            continue;
        }
        const int direction = projectile.draw_facing_west ? 1 : 0;
        const auto& frames = enemy_frames_[static_cast<std::size_t>(
            projectile.owner_type)][direction];
        if (projectile.draw_sprite_frame < 0 || projectile.draw_sprite_frame >=
                                                static_cast<int>(frames.size())) {
            continue;
        }
        const auto& previous = previous_enemy_projectiles_[index];
        const bool can_interpolate = previous.active && previous.draw_visible &&
            previous.owner_type == projectile.owner_type;
        const auto lerp = [interpolation](const int from, const int to) {
            return static_cast<int>(std::lround(
                from + (to - from) * interpolation));
        };
        const int x = can_interpolate
            ? lerp(previous.draw_x_pixels, projectile.draw_x_pixels)
            : projectile.draw_x_pixels;
        const int y = can_interpolate
            ? lerp(previous.draw_y_pixels, projectile.draw_y_pixels)
            : projectile.draw_y_pixels;
        blit(frames[static_cast<std::size_t>(projectile.draw_sprite_frame)],
             frame, x - camera_x, y - camera_y);
    }
}

void GameLevel::draw_enemies(DecodedImage& frame, const int camera_x,
                             const int camera_y,
                             const double interpolation,
                             const int ui_x_offset) const {
    // 0BA5:0C5B converts current/original health to 78 VGA byte-columns and
    // paints three scanlines at (4,152). Each byte-column is four native
    // pixels because all VGA planes are enabled. Generic enemies call it
    // above 20 health; the two boss controllers call it unconditionally.
    const auto draw_health_bar = [this, &frame, ui_x_offset](
                                     const EnemyState& enemy) {
        const int columns = std::clamp(
            static_cast<int>((static_cast<long long>(enemy.health) * 78) /
                             enemy.max_health), 0, 78);
        for (int column = 0; column < columns; ++column) {
            const auto palette_index = static_cast<std::size_t>(
                0x6F - column / 10);
            if (palette_index >= hud_.palette.size()) {
                continue;
            }
            for (int y = 152; y < 155; ++y) {
                for (int plane = 0; plane < 4; ++plane) {
                    const int x = ui_x_offset + 4 + column * 4 + plane;
                    frame.pixels[static_cast<std::size_t>(y) * frame.width + x] =
                        hud_.palette[palette_index];
                }
            }
        }
    };

    for (std::size_t index = 0; index < enemies_.size(); ++index) {
        const auto& enemy = enemies_[index];
        if (!enemy.active) {
            continue;
        }
        // 0BA5:12AB calls the health-bar routine for this slot before its
        // spawn test and sprite path. Keep that per-slot order rather than
        // batching every bar ahead of every enemy.
        if (enemy.health_bar_visible && enemy.max_health > 0) {
            draw_health_bar(enemy);
        }
        const bool draw_facing = enemy.draw_state_valid
            ? enemy.draw_facing_west : enemy.facing_west;
        const int direction = draw_facing ? 1 : 0;
        if (enemy.spawn_ticks > 0) {
            // 0BA5:12C0 jumps directly to the Twinks/spawn-countdown tail at
            // 1EE6 while this word is non-zero. The five-frame Morph record
            // belongs to Hocus's teleporter states; spawning enemies are
            // invisible apart from the randomized Twinks cells.
            continue;
        }
        const auto& frames = enemy_frames_[enemy.type][direction];
        if (frames.empty()) {
            continue;
        }
        int frame_index = std::clamp(
            enemy.draw_state_valid ? enemy.draw_sprite_frame
                                   : enemy.sprite_frame,
            0, static_cast<int>(frames.size()) - 1);
        if (enemy.behaviour == 8) {
            const int boss_frame = enemy.draw_state_valid
                ? enemy.draw_sprite_frame : enemy.behaviour_frame;
            frame_index = std::clamp(boss_frame < 2
                                         ? boss_frame
                                         : boss_frame - 2,
                                     0, static_cast<int>(frames.size()) - 1);
        } else if (enemy.behaviour == 99) {
            frame_index = std::clamp(enemy.draw_state_valid
                                         ? enemy.draw_sprite_frame
                                         : enemy.behaviour_frame,
                                     0,
                                     static_cast<int>(frames.size()) - 1);
        }
        int world_x = enemy.draw_state_valid
            ? enemy.draw_x_pixels : enemy.x_pixels;
        int world_y = enemy.draw_state_valid
            ? enemy.draw_y_pixels : enemy.y_pixels;
        const auto& previous = previous_enemies_[index];
        if (previous.active && previous.type == enemy.type) {
            const int old_x = previous.draw_state_valid
                ? previous.draw_x_pixels : previous.x_pixels;
            const int old_y = previous.draw_state_valid
                ? previous.draw_y_pixels : previous.y_pixels;
            world_x = static_cast<int>(std::lround(
                old_x + (world_x - old_x) * interpolation));
            world_y = static_cast<int>(std::lround(
                old_y + (world_y - old_y) * interpolation));
        }
        const int draw_x = world_x - camera_x;
        const int draw_y = world_y - camera_y;
        if (enemy.hit_flash_masked && hud_.palette.size() > 0x70) {
            blit_solid_mask(frames[frame_index], frame, draw_x, draw_y,
                            hud_.palette[0x70]);
        } else {
            blit(frames[frame_index], frame, draw_x, draw_y);
        }
    }
}

void GameLevel::draw_visual_effects(DecodedImage& frame) const {
    // The outer runner calls 2FF3, 36F0, 1FAA, then 20B1. Their direct VGA
    // writes define this overlap order.
    for (const auto& burst : explosion_bursts_) {
        if (!burst.draw_visible) {
            continue;
        }
        for (const auto& particle : burst.particles) {
            if (particle.draw_x_pixels < 0 ||
                particle.draw_x_pixels >= viewport_width_ ||
                particle.draw_y_pixels < 0 ||
                particle.draw_y_pixels >= viewport_height ||
                particle.colour_index >= hud_.palette.size()) {
                continue;
            }
            frame.pixels[static_cast<std::size_t>(particle.draw_y_pixels) *
                             frame.width + particle.draw_x_pixels] =
                hud_.palette[particle.colour_index];
        }
    }
    for (const auto& sparkle : pickup_sparkles_) {
        if (!sparkle.draw_visible || sparkle.frame < 0 || sparkle.frame > 4) {
            continue;
        }
        blit(bullet_frames_[sparkle.mirrored ? 1 : 0]
                           [static_cast<std::size_t>(sparkle.frame)],
             frame, sparkle.draw_x_pixels, sparkle.draw_y_pixels);
    }
    for (const auto& twinkle : twinkles_) {
        if (!twinkle.draw_visible) {
            continue;
        }
        // 0BA5:1FF1-2098 walks 0,1,2,3,4,3,2,1,0 as the nine-tick
        // counter descends.
        const int frame_index = twinkle.draw_ticks >= 5
            ? 9 - twinkle.draw_ticks : twinkle.draw_ticks - 1;
        blit(twinkle_frames_[twinkle.mirrored ? 1 : 0]
                            [static_cast<std::size_t>(frame_index)],
             frame, twinkle.x_pixels, twinkle.y_pixels);
    }
    for (const auto& trail : spell_trails_) {
        if (!trail.visible || trail.draw_x_pixels < 0 ||
            trail.draw_x_pixels >= viewport_width_ ||
            trail.draw_y_pixels < 0 ||
            trail.draw_y_pixels >= viewport_height) {
            continue;
        }
        const auto colour = static_cast<std::size_t>(0x78 + trail.draw_age / 4);
        if (colour >= hud_.palette.size()) {
            continue;
        }
        frame.pixels[static_cast<std::size_t>(trail.draw_y_pixels) * frame.width +
                     trail.draw_x_pixels] = hud_.palette[colour];
    }
}

void GameLevel::draw_layer(const std::vector<std::uint8_t>& layer,
                                DecodedImage& frame,
                                int camera_x, int camera_y) const {
    constexpr int tiles_per_row = 20;
    const int first_column = floor_div(camera_x, tile_size);
    const int first_row = floor_div(camera_y, tile_size);
    const int offset_x = -(camera_x - first_column * tile_size);
    const int offset_y = -(camera_y - first_row * tile_size);

    for (int sy = 0; sy < viewport_height / tile_size + 2; ++sy) {
        const int map_y = first_row + sy;
        if (map_y < 0 || map_y >= map_height) {
            continue;
        }
        for (int sx = 0; sx < viewport_width_ / tile_size + 2; ++sx) {
            const int map_x = first_column + sx;
            if (map_x < 0 || map_x >= map_width) {
                continue;
            }
            const auto tile = layer[static_cast<std::size_t>(map_y) * map_width + map_x];
            if (tile == 0xFF) {
                continue;
            }
            const int source_x = (tile % tiles_per_row) * tile_size;
            const int source_y = (tile / tiles_per_row) * tile_size;
            if (source_y + tile_size > tiles_.height) {
                throw std::runtime_error("Level references a tile outside the tileset");
            }
            blit_region(tiles_, frame, source_x, source_y, tile_size, tile_size,
                        offset_x + sx * tile_size, offset_y + sy * tile_size);
        }
    }
}

LevelScene GameLevel::render(const double interpolation) const {
    if (interpolation < 0.0 || interpolation > 1.0) {
        throw std::out_of_range("Render interpolation must be in [0,1]");
    }
    const auto lerp = [interpolation](const int from, const int to) {
        return static_cast<int>(std::lround(
            from + (to - from) * interpolation));
    };
    const int player_x = lerp(previous_player_draw_.x_pixels,
                              player_draw_.x_pixels);
    const int player_y = lerp(previous_player_draw_.y_pixels,
                              player_draw_.y_pixels);
    const int camera_x = lerp(previous_camera_x_pixels_, camera_pixel_x());
    const int camera_y = lerp(previous_camera_y_pixels_, camera_pixel_y());

    const int ui_x_offset = (viewport_width_ - original_frame_width) / 2;
    DecodedImage frame;
    if (viewport_width_ == original_frame_width) {
        // Keep the registered path as the original image copy so the default
        // 320x200 raster remains byte-for-byte identical.
        frame = backdrop_;
    } else {
        frame.width = viewport_width_;
        frame.height = game_frame_height;
        frame.palette = backdrop_.palette;
        frame.pixels.resize(static_cast<std::size_t>(frame.width) *
                            frame.height);
        // BACKxx.PCX is a screen-fixed 320-pixel backdrop. Repeat it into the
        // two native side extensions while aligning the original image under
        // the centred 320-pixel HUD region.
        for (int y = 0; y < frame.height; ++y) {
            for (int x = 0; x < frame.width; ++x) {
                const int source_x =
                    (x - ui_x_offset + backdrop_.width) % backdrop_.width;
                frame.pixels[static_cast<std::size_t>(y) * frame.width + x] =
                    backdrop_.pixels[static_cast<std::size_t>(y) *
                                         backdrop_.width + source_x];
            }
        }
    }
    if (crystal_flash_ticks_ > 0) {
        // 0BA5:4DC9 calls 220F instead of presenting the two map layers. The
        // player, entities, and effects are drawn afterward on the flash page.
        const auto colour = hud_.palette[static_cast<std::size_t>(
            0x77 - crystal_flash_ticks_)];
        std::fill_n(frame.pixels.begin(), viewport_width_ * viewport_height,
                    colour);
    } else {
        draw_layer(draw_background_layer_, frame, camera_x, camera_y);
        draw_layer(draw_main_layer_, frame, camera_x, camera_y);
    }

    const int direction = player_draw_.facing_west ? 1 : 0;
    if (player_draw_.invisibility_ticks != 0) {
        // 0BA5:50B1 jumps directly to the CE8E countdown; neither the normal
        // player nor the teleporter morph is drawn while invisibility lasts.
    } else if (player_draw_.teleporter_ticks != 0) {
        int frame_index = 4;
        if (player_draw_.teleporter_ticks < 25) {
            frame_index = std::clamp(
                (player_draw_.teleporter_ticks - 1) / 5, 0, 4);
        } else if (player_draw_.teleporter_ticks > 25) {
            frame_index = std::clamp(
                (40 - player_draw_.teleporter_ticks) / 3, 0, 4);
        }
        blit(morph_frames_[direction][static_cast<std::size_t>(frame_index)],
             frame, player_x - camera_x, player_y - camera_y);
    } else if (player_draw_.damage_cooldown == 0 ||
               player_draw_.damage_cooldown % 2 == 0) {
        const auto& sprite = hocus_frames_[direction][player_draw_.frame];
        blit(sprite, frame, player_x - camera_x, player_y - camera_y);
    }
    draw_laser_indicator(frame, camera_x, camera_y, player_x, player_y);
    // These routines execute in this order at 0BA5:547A-5492. Hocus's own
    // projectile dispatcher is later at 5806 and therefore draws last.
    draw_enemy_projectiles(frame, camera_x, camera_y, interpolation);
    draw_enemies(frame, camera_x, camera_y, interpolation, ui_x_offset);
    draw_visual_effects(frame);
    draw_projectiles(frame, camera_x, camera_y, interpolation);
    // NEW_HUD.IMG is exactly 320 pixels wide. Repeat its stonework into the
    // side extensions, keeping the complete original HUD centred and intact.
    for (int y = 0; y < hud_.height; ++y) {
        for (int x = 0; x < frame.width; ++x) {
            const int source_x = (x - ui_x_offset + hud_.width) % hud_.width;
            frame.pixels[static_cast<std::size_t>(viewport_height + y) *
                             frame.width + x] =
                hud_.pixels[static_cast<std::size_t>(y) * hud_.width +
                            source_x];
        }
    }
    draw_hud_values(frame, ui_x_offset);
    draw_message(frame, ui_x_offset);
    return {std::move(frame), player_x, player_y, camera_x, camera_y};
}

} // namespace hocus
