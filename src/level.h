#pragma once

#include "dat_archive.h"
#include "pcx.h"
#include "sprite.h"

#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace hocus {

inline constexpr int original_frame_width = 320;
inline constexpr int widescreen_frame_width = 356;
inline constexpr int ultrawide_frame_width = 467;
inline constexpr int super_ultrawide_frame_width = 711;
inline constexpr int game_frame_height = 200;
inline constexpr int gameplay_viewport_height = 160;

struct LevelId {
    int episode{1};
    int number{1};

    bool operator==(const LevelId&) const = default;
};

struct InputState {
    bool left{};
    bool right{};
    bool jump{};
    bool action{};
    bool fire{};
    bool down{};
    bool scroll_up{};
    bool scroll_down{};
};

enum class CheatCode {
    full_health,
    both_keys,
    rapid_fire,
    laser_shots,
    midair_jump,
};

inline constexpr std::size_t cheat_code_count = 5;

struct PlayerState {
    // Confirmed original units: X increments represent 8 pixels; Y is pixels.
    int x_half_tiles{};
    int y_pixels{};
    bool facing_west{};
    bool jumping{};
    bool falling{};
    int jump_index{};
    int animation_tick{};
    bool moved_this_tick{};
    bool super_jump{};
    int firing_ticks{};
    bool firing_vertical{};
};

struct PlayerProgress {
    int health{100};
    int firepower{1};
    int crystals{};
    int total_crystals{};
    int treasures{};
    int total_treasures{};
    int score{};
    int damage_cooldown{};
    int invisibility_ticks{};
    int super_shot_ticks{};
    int laser_shots{};
    bool super_jump_available{};
    bool silver_key{};
    bool gold_key{};
};

// 0BA5:3596/35BB use only the first byte of the decimal conversion buffer for
// each crystal field. Values are nonnegative in every registered path.
[[nodiscard]] constexpr int dos_hud_first_decimal_digit(int value) noexcept {
    if (value < 0) {
        value = 0;
    }
    while (value >= 10) {
        value /= 10;
    }
    return value;
}

struct LevelScene {
    DecodedImage image;
    int player_pixel_x{};
    int player_pixel_y{};
    int camera_pixel_x{};
    int camera_pixel_y{};
};

struct ProjectileState {
    int x_pixels{};
    int y_pixels{};
    bool facing_west{};
    bool vertical{};
    bool laser{};
    bool active{};
};

struct EnemyState {
    int type{};
    int spawn_cell{};
    int x_pixels{};
    int y_pixels{};
    int health{};
    int max_health{};
    int behaviour{};
    int x_velocity_pixels{};
    int y_velocity_pixels{};
    int anchor_x_pixels{};
    int behaviour_frame{};
    int behaviour_phase{};
    int boss_stage{};
    int movement_ticks{};
    int vertical_state{-99};
    // The DOS enemy pool keeps these as three independent word arrays:
    // BF64 is the launch freeze/countdown, BF94 is selector one's attack-pose
    // timer, and BFB4/BFC4 gate the shared animation tail every other update.
    int attack_ticks{};
    int attack_pose_ticks{};
    int animation_delay_ticks{1};
    int animation_delay_reset_ticks{1};
    int spawn_ticks{};
    int hit_cooldown{};
    int animation_tick{};
    int sprite_frame{};
    int draw_x_pixels{};
    int draw_y_pixels{};
    int draw_sprite_frame{};
    bool facing_west{};
    bool draw_facing_west{};
    bool draw_state_valid{};
    bool attacking{};
    bool hit_flash_masked{};
    bool health_bar_visible{};
    bool active{};
};

struct EnemyProjectileState {
    int x_pixels{};
    int y_pixels{};
    int x_speed_pixels{};
    int y_speed_pixels{};
    int owner_type{};
    int animation_tick{};
    int delay_ticks{};
    int width{8};
    int height{8};
    int sprite_frame{};
    int last_sprite_frame{};
    int draw_x_pixels{};
    int draw_y_pixels{};
    int draw_sprite_frame{};
    bool facing_west{};
    bool draw_facing_west{};
    bool draw_visible{};
    bool horizontal_homing{};
    bool vertical_wobble{};
    bool active{};
};

// The original keeps these two short-lived visual systems in fixed pools.
// Pickup sparkles are screen-relative BULLIT.IMG cells; Twinks are the
// symmetric five-frame animation from SPRITES.SPR record 2.
struct PickupSparkleState {
    int x_pixels{};
    int y_pixels{};
    int frame{};
    int ticks{};
    int draw_x_pixels{};
    int draw_y_pixels{};
    bool mirrored{};
    bool draw_visible{};
};

struct TwinkleState {
    int x_pixels{};
    int y_pixels{};
    int ticks{};
    int draw_ticks{};
    bool mirrored{};
    bool draw_visible{};
};

struct ExplosionParticleState {
    int x_pixels{};
    int y_pixels{};
    int x_velocity{};
    int y_velocity{};
    int draw_x_pixels{};
    int draw_y_pixels{};
    std::uint8_t colour_index{};
};

struct ExplosionBurstState {
    int ticks{};
    std::array<ExplosionParticleState, 16> particles{};
    bool draw_visible{};
};

struct SpellTrailState {
    int x_pixels{};
    int y_pixels{};
    int age{};
    int draw_x_pixels{};
    int draw_y_pixels{};
    int draw_age{};
    bool visible{};
    bool active{};
};

class GameLevel {
public:
    explicit GameLevel(const DatArchive& archive, LevelId level = {},
                       int initial_score = 0, int skill = 0,
                       bool show_crystal_tip = false,
                       int viewport_width = original_frame_width);

    void tick(const InputState& input);
    void apply_cheat(CheatCode cheat) noexcept;
    void set_cheat_enabled(CheatCode cheat, bool enabled) noexcept;
    [[nodiscard]] bool cheat_enabled(CheatCode cheat) const noexcept;
    // interpolation=1 preserves the exact fixed-step DOS raster. Values in
    // [0,1] blend only visual positions between the preceding and current
    // simulation snapshots; game state and timing remain fixed-step.
    [[nodiscard]] LevelScene render(double interpolation = 1.0) const;
    [[nodiscard]] int viewport_width() const noexcept {
        return viewport_width_;
    }
    [[nodiscard]] const PlayerState& player() const noexcept { return player_; }
    [[nodiscard]] const PlayerProgress& progress() const noexcept { return progress_; }
    [[nodiscard]] const std::vector<std::string>& active_message() const noexcept {
        return active_message_;
    }
    void dismiss_message() noexcept { active_message_.clear(); }
    [[nodiscard]] bool modal_overlay_active() const noexcept {
        return crystal_tip_visible_ || !active_message_.empty();
    }
    void dismiss_modal_overlay() noexcept {
        crystal_tip_visible_ = false;
        active_message_.clear();
    }
    [[nodiscard]] const std::array<ProjectileState, 10>& projectiles() const noexcept {
        return projectiles_;
    }
    [[nodiscard]] int active_projectile_count() const noexcept {
        return active_projectile_count_;
    }
    [[nodiscard]] bool laser_indicator_visible() const noexcept {
        return laser_indicator_visible_;
    }
    [[nodiscard]] int laser_indicator_ticks() const noexcept {
        return laser_indicator_ticks_;
    }
    [[nodiscard]] int crystal_flash_ticks() const noexcept {
        return crystal_flash_ticks_;
    }
    [[nodiscard]] int super_jump_effect_ticks() const noexcept {
        return super_jump_effect_ticks_;
    }
    [[nodiscard]] int level_number_flash_ticks() const noexcept {
        return level_number_flash_ticks_;
    }
    [[nodiscard]] int random_index() const noexcept { return random_index_; }
    void set_random_index(int index) noexcept {
        random_index_ = ((index % 1000) + 1000) % 1000;
    }
    [[nodiscard]] const std::array<EnemyState, 8>& enemies() const noexcept {
        return enemies_;
    }
    [[nodiscard]] int active_enemy_count() const noexcept {
        return active_enemy_count_;
    }
    [[nodiscard]] const std::array<EnemyProjectileState, 8>&
    enemy_projectiles() const noexcept {
        return enemy_projectiles_;
    }
    [[nodiscard]] int active_enemy_projectile_count() const noexcept {
        return active_enemy_projectile_count_;
    }
    [[nodiscard]] const std::array<PickupSparkleState, 10>&
    pickup_sparkles() const noexcept {
        return pickup_sparkles_;
    }
    [[nodiscard]] const std::array<TwinkleState, 8>& twinkles() const noexcept {
        return twinkles_;
    }
    [[nodiscard]] const std::array<ExplosionBurstState, 8>&
    explosion_bursts() const noexcept {
        return explosion_bursts_;
    }
    [[nodiscard]] const std::array<SpellTrailState, 20>&
    spell_trails() const noexcept {
        return spell_trails_;
    }
    [[nodiscard]] int enemy_sprite_record_for_type(std::size_t type) const {
        if (type >= enemy_definitions_.size()) {
            throw std::out_of_range("Enemy type is out of range");
        }
        return enemy_definitions_[type].sprite_asset;
    }
    [[nodiscard]] int level_complete_ticks() const noexcept {
        return level_complete_ticks_;
    }
    [[nodiscard]] bool level_complete() const noexcept { return level_complete_; }
    [[nodiscard]] int elapsed_ticks() const noexcept { return elapsed_ticks_; }
    [[nodiscard]] int elapsed_seconds() const noexcept {
        return elapsed_ticks_ / 20;
    }
    [[nodiscard]] int damage_amount() const noexcept { return damage_amount_; }
    [[nodiscard]] int death_ticks() const noexcept { return death_ticks_; }
    [[nodiscard]] bool player_dead() const noexcept { return player_dead_; }
    [[nodiscard]] int return_x_half_tiles() const noexcept {
        return return_x_half_tiles_;
    }
    [[nodiscard]] int return_y_pixels() const noexcept {
        return return_y_pixels_;
    }
    [[nodiscard]] std::vector<int> take_sound_events();
    [[nodiscard]] int teleporter_ticks() const noexcept {
        return teleporter_ticks_;
    }
    [[nodiscard]] bool teleporting() const noexcept {
        return teleporter_ticks_ != 0;
    }
    [[nodiscard]] LevelId level_id() const noexcept { return level_id_; }
    [[nodiscard]] int backdrop_asset_index() const noexcept {
        return backdrop_asset_index_;
    }
    [[nodiscard]] int tileset_asset_index() const noexcept {
        return tileset_asset_index_;
    }
    [[nodiscard]] int music_asset_index() const noexcept {
        return music_asset_index_;
    }
    [[nodiscard]] std::string level_name() const;
    [[nodiscard]] bool solid(int tile_x, int tile_y) const noexcept;
    [[nodiscard]] std::uint16_t event_at(int tile_x, int tile_y) const noexcept;
    [[nodiscard]] std::uint8_t background_tile_at(int tile_x,
                                                  int tile_y) const noexcept;
    [[nodiscard]] std::uint8_t main_tile_at(int tile_x, int tile_y) const noexcept;
    [[nodiscard]] int camera_focus_rows() const noexcept {
        return camera_focus_rows_;
    }

    // Checkpoints and teleporters use this same placement path; exposing it
    // also lets deterministic tests exercise isolated map events.
    void place_player(int x_half_tiles, int y_pixels) noexcept;

private:
    struct CollisionFlags {
        bool left{};
        bool step_left{};
        bool right{};
        bool step_right{};
    };

    struct MessageRecord {
        int tile_x{};
        int tile_y{};
        std::vector<std::string> lines;
    };

    struct SwitchRecord {
        std::uint8_t mode{};
        std::array<std::uint16_t, 4> dependencies{};
        std::array<std::uint8_t, 4> expected_tiles{};
        int first_column{};
        int first_row{};
        int last_column{};
        int last_row{};
        int countdown{};
        bool activated{};
    };

    struct TeleporterRecord {
        std::uint16_t trigger_cell{};
        std::uint16_t destination_cell{};
    };

    struct KeyholeRecord {
        int required_key{};
        std::uint8_t replacement_tile{};
        int first_column{};
        int first_row{};
        int last_column{};
        int last_row{};
    };

    struct TileAnimationRecord {
        std::uint8_t first{};
        std::uint8_t last{};
        std::uint8_t mode{};
    };

    struct PlayerDrawState {
        int x_pixels{};
        int y_pixels{};
        int frame{};
        int teleporter_ticks{};
        int invisibility_ticks{};
        int damage_cooldown{};
        int laser_shots{};
        bool facing_west{};
        bool laser_indicator_visible{};
    };

    struct EnemyDefinition {
        std::array<int, 12> words{};
        int sprite_asset{};
        int health{};
        int active_behaviour{};
        bool available{};
    };

    struct EnemyTrigger {
        std::array<std::uint16_t, 8> types{};
        std::array<std::uint16_t, 8> spawn_cells{};
    };

    struct PendingEnemySpawn {
        int type{};
        int spawn_cell{};
        bool active{};
    };

    [[nodiscard]] CollisionFlags horizontal_collision_flags() const noexcept;
    [[nodiscard]] int current_player_frame() const noexcept;
    void capture_player_draw_state() noexcept;
    void capture_interpolation_state() noexcept;
    void update_vertical_motion();
    void update_horizontal_motion(const InputState& input);
    void update_elevator(const InputState& input);
    void eject_from_floor_if_needed() noexcept;
    void reset_camera() noexcept;
    void update_camera();
    void update_camera_focus(const InputState& input) noexcept;
    void shift_visual_effects_for_camera(int delta_x, int delta_y) noexcept;
    [[nodiscard]] int camera_pixel_x() const noexcept {
        return camera_x_half_tiles_ * 8;
    }
    [[nodiscard]] int camera_pixel_y() const noexcept {
        return camera_y_rows_ * 16;
    }
    void spawn_projectile(bool vertical);
    void enforce_enabled_cheats() noexcept;
    void update_projectiles();
    void process_enemy_trigger(std::uint16_t event, bool primary_sample);
    void spawn_pending_enemies();
    void spawn_episode_four_boss(int stage, bool initial_spawn);
    void initialise_enemy_motion(EnemyState& enemy);
    [[nodiscard]] bool spawn_enemy_projectile(const EnemyState& enemy,
                                              int width, int height,
                                              int delay_override = -1);
    void collide_enemy_projectiles_with_player();
    void update_enemy_projectiles();
    void collide_enemies_with_player();
    void update_enemies();
    void release_distant_enemies();
    void advance_enemy_animation(EnemyState& enemy);
    void kill_all_enemies();
    void collide_projectiles_with_enemies();
    void hit_enemy(EnemyState& enemy, ProjectileState& projectile);
    [[nodiscard]] bool enemy_collides_at(int x_pixels, int y_pixels,
                                         int width, int height) const noexcept;
    [[nodiscard]] int random_below(int upper_bound) noexcept;
    void emit_sound(int logical_sound);
    [[nodiscard]] bool update_level_completion();
    [[nodiscard]] bool update_death();
    void process_events(bool action_pressed);
    void process_item_event(std::size_t cell, std::uint16_t event,
                            bool& action_pressed, bool primary_sample);
    void process_teleporter_event(std::size_t cell, std::uint16_t event,
                                  bool primary_sample);
    [[nodiscard]] bool update_teleporter();
    void process_switch_event(std::size_t cell, std::uint16_t event,
                              bool& action_pressed, bool primary_sample);
    void process_gate_event(std::size_t cell, std::uint16_t event,
                            bool primary_sample);
    void update_tile_viewport();
    void spawn_pickup_sparkle(int kind);
    void spawn_twinkle(int x_pixels, int y_pixels);
    void spawn_explosion(int x_pixels, int y_pixels);
    void spawn_spell_trail(int x_pixels, int y_pixels);
    void update_visual_effects();
    void spawn_super_jump_twinkle();
    void show_wizard_message();
    void draw_layer(const std::vector<std::uint8_t>& layer,
                    DecodedImage& frame, int camera_x, int camera_y) const;
    void draw_text(DecodedImage& frame, const std::string& text,
                   int x, int y, std::uint32_t colour) const;
    void draw_hud_values(DecodedImage& frame, int ui_x_offset) const;
    void draw_message(DecodedImage& frame, int ui_x_offset) const;
    void draw_projectiles(DecodedImage& frame, int camera_x, int camera_y,
                          double interpolation) const;
    void draw_laser_indicator(DecodedImage& frame, int camera_x,
                              int camera_y, int player_x, int player_y) const;
    void draw_enemy_projectiles(DecodedImage& frame,
                                int camera_x, int camera_y,
                                double interpolation) const;
    void draw_enemies(DecodedImage& frame, int camera_x, int camera_y,
                      double interpolation, int ui_x_offset) const;
    void draw_visual_effects(DecodedImage& frame) const;

    LevelId level_id_;
    int level_index_{};
    int backdrop_asset_index_{};
    int tileset_asset_index_{};
    int music_asset_index_{};
    DecodedImage backdrop_;
    DecodedImage tiles_;
    DecodedImage hud_;
    DecodedImage hud_stuff_;
    DecodedImage crystal_tip_;
    std::array<std::vector<DecodedImage>, 2> hocus_frames_;
    std::array<std::vector<DecodedImage>, 2> twinkle_frames_;
    std::array<std::vector<DecodedImage>, 2> morph_frames_;
    std::array<std::array<std::vector<DecodedImage>, 2>, 10> enemy_frames_;
    std::array<SpriteRecordInfo, 10> enemy_sprite_info_;
    // BULLIT.IMG contains eight 16-pixel cells. The recovered effect routine
    // addresses cells 0..4 in either direction; the remaining cells are
    // redundant pre-flipped artwork. Hocus's spell is sprite frames 11..14.
    std::array<std::array<DecodedImage, 5>, 2> bullet_frames_;
    std::array<PickupSparkleState, 10> pickup_sparkles_;
    std::array<TwinkleState, 8> twinkles_;
    std::size_t next_twinkle_{};
    std::array<ExplosionBurstState, 8> explosion_bursts_;
    std::size_t next_explosion_burst_{};
    std::array<SpellTrailState, 20> spell_trails_;
    std::size_t next_spell_trail_{};
    std::array<ProjectileState, 10> projectiles_;
    std::array<EnemyState, 8> enemies_;
    std::array<PendingEnemySpawn, 32> pending_enemy_spawns_;
    std::array<EnemyProjectileState, 8> enemy_projectiles_;
    std::array<int, 1000> random_table_{};
    int random_index_{};
    std::vector<std::uint8_t> background_layer_;
    std::vector<std::uint8_t> main_layer_;
    std::vector<std::uint8_t> draw_background_layer_;
    std::vector<std::uint8_t> draw_main_layer_;
    std::vector<std::uint8_t> original_main_layer_;
    std::vector<std::uint8_t> pending_gate_layer_;
    std::vector<std::uint16_t> event_layer_;
    std::vector<std::uint8_t> font_mask_;
    std::vector<MessageRecord> messages_;
    std::array<TeleporterRecord, 10> teleporters_;
    std::array<SwitchRecord, 23> switches_;
    std::array<KeyholeRecord, 25> insertion_gates_;
    std::array<KeyholeRecord, 25> keyholes_;
    std::array<TileAnimationRecord, 240> tile_animations_;
    std::array<EnemyDefinition, 10> enemy_definitions_;
    std::array<EnemyTrigger, 250> enemy_triggers_;
    std::vector<std::string> active_message_;
    bool crystal_tip_visible_{};
    std::vector<int> sound_events_;
    std::uint8_t clear_background_tile_{};
    std::uint8_t switch_off_tile_{};
    std::uint8_t switch_on_tile_{};
    std::uint8_t breakable_main_tile_{};
    PlayerState player_;
    PlayerProgress progress_;
    PlayerProgress draw_progress_;
    PlayerDrawState player_draw_;
    PlayerDrawState previous_player_draw_;
    std::array<ProjectileState, 10> previous_projectiles_;
    std::array<EnemyState, 8> previous_enemies_;
    std::array<EnemyProjectileState, 8> previous_enemy_projectiles_;
    int previous_camera_x_pixels_{};
    int previous_camera_y_pixels_{};
    int active_projectile_count_{};
    int active_enemy_count_{};
    int active_enemy_projectile_count_{};
    int enemy_fire_random_modulus_{50};
    int spawn_sound_deadline_{};
    int skill_{};
    int damage_amount_{4};
    int camera_x_half_tiles_{};
    int camera_y_rows_{};
    int viewport_width_{original_frame_width};
    int camera_focus_rows_{5};
    int camera_up_hold_ticks_{};
    int camera_down_hold_ticks_{};
    int elapsed_ticks_{};
    int level_complete_ticks_{};
    int death_ticks_{};
    int teleporter_ticks_{};
    int teleporter_target_x_{};
    int teleporter_target_y_{};
    int saved_firepower_{1};
    std::array<bool, cheat_code_count> enabled_cheats_{};
    int super_jump_effect_ticks_{};
    int laser_indicator_ticks_{5};
    int crystal_flash_ticks_{};
    int level_number_flash_ticks_{};
    int draw_level_number_flash_ticks_{};
    int elevator_left_tile_{-1};
    int elevator_right_tile_{-1};
    int return_x_half_tiles_{};
    int return_y_pixels_{};
    int gate_countdown_{};
    bool level_complete_{};
    bool player_dead_{};
    bool action_down_{};
    bool fire_down_{};
    bool jump_down_{};
    bool laser_indicator_visible_{true};
    bool tile_animation_phase_{};
};

} // namespace hocus
