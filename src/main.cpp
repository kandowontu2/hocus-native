#include "audio.h"
#include "campaign.h"
#include "dat_archive.h"
#include "demo.h"
#include "input.h"
#include "level.h"
#include "native_settings.h"
#include "pcx.h"
#include "registered_executable.h"
#include "save.h"
#include "timing.h"
#include "ui.h"

#include <windows.h>
#include <mmsystem.h>
#include <dsound.h>
#include <xinput.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

hocus::DecodedImage g_title;
hocus::DecodedImage g_piracy;
hocus::DecodedImage g_apogee;
hocus::DecodedImage g_startup_source;
hocus::DecodedImage g_menu_logo;
hocus::DecodedImage g_menu_cursor_sheet;
hocus::DecodedImage g_volume_bar;
hocus::DecodedImage g_frame;
std::unique_ptr<hocus::DatArchive> g_archive;
std::unique_ptr<hocus::GameLevel> g_game;
hocus::LevelId g_selected_level;
bool g_show_level = false;
hocus::DecodedImage g_bottom;
std::vector<std::uint8_t> g_font_mask;
std::vector<std::uint32_t> g_game_palette;
std::vector<hocus::InputState> g_demo_frames;
std::size_t g_demo_frame_index{};
hocus::BorlandRandom g_borland_random;
ULONGLONG g_title_idle_started{};
bool g_demo_playing{};
bool g_level_music_needs_restart{};
bool g_startup_active{true};
hocus::StartupSequence g_startup_sequence;
ULONGLONG g_startup_last_tick{};

constexpr ULONGLONG timer_ticks_to_milliseconds(const ULONGLONG ticks) {
    return (ticks * 1000 + hocus::dos_timer_hz / 2) /
           hocus::dos_timer_hz;
}
// 06B8:247F waits 0x1194 ticks before credits and again before a demo.
constexpr ULONGLONG title_attract_stage_milliseconds =
    timer_ticks_to_milliseconds(0x1194);
// 06B8:4347 holds each of the two credit pages for 0x09C4 ticks.
constexpr ULONGLONG auto_credit_hold_milliseconds =
    timer_ticks_to_milliseconds(0x09C4);

enum class FrontendScreen {
    startup,
    main_menu,
    episode_menu,
    skill_menu,
    ordering,
    story,
    help,
    credits,
    high_scores,
    high_score_name,
    level_results,
    warp,
    ending,
    finale,
    game_paused,
    game_notice,
    cheat_menu,
    cheat_level_select,
    quit_confirmation,
    pause_menu,
    save_menu,
    restore_menu,
    save_name,
    options_menu,
    game_speed,
    volume_control,
    key_controls,
    joystick_calibration,
};

FrontendScreen g_frontend_screen = FrontendScreen::startup;
int g_menu_selection{};
int g_episode_selection{};
int g_skill_selection{1};
int g_page_index{};
int g_pause_selection{};
int g_slot_selection{};
int g_save_slot_selection{};
int g_restore_slot_selection{};
int g_options_selection{};
int g_cheat_selection{};
int g_cheat_level_selection{};
int g_cheat_episode{1};
int g_cheat_level{1};
int g_volume_selection{};
int g_key_selection{};
int g_menu_cursor_ticks{};
int g_game_speed{1};
int g_skill{1};
int g_campaign_score{};
int g_high_score_episode{};
int g_high_score_edit_rank{-1};
int g_ending_episode{};
int g_result_accuracy{};
int g_result_elapsed_seconds{};
int g_result_time_limit{};
int g_result_treasure_bonus{};
int g_result_time_bonus{};
ULONGLONG g_level_elapsed_ms{};
ULONGLONG g_level_started_at{};
bool g_slot_menu_from_pause{};
bool g_slot_menu_from_level{};
bool g_reference_return_to_pause{};
bool g_reference_return_to_level{};
bool g_options_return_to_pause{};
bool g_control_return_to_level{};
bool g_sound_enabled{true};
bool g_music_enabled{true};
enum class JoystickBackend {
    none,
    xinput,
    winmm,
};
bool g_joystick_enabled{};
JoystickBackend g_joystick_backend{JoystickBackend::none};
bool g_high_fps_mode{};
hocus::WidescreenMode g_widescreen_mode{hocus::WidescreenMode::off};
bool g_fullscreen{};
int g_sound_volume{15};
int g_music_volume{15};

enum class QuitTarget {
    application,
    main_menu,
};

enum class QuitReturn {
    title,
    pause_menu,
    level,
};

QuitTarget g_quit_target{QuitTarget::application};
QuitReturn g_quit_return{QuitReturn::title};
bool g_result_complete{true};
bool g_auto_credits{};
bool g_attract_credits_shown{};
bool g_capture_key{};
ULONGLONG g_auto_credit_started{};

enum class AutoCreditStage {
    initial_fade_out,
    page_fade_in,
    page_hold,
    page_fade_out,
};

AutoCreditStage g_auto_credit_stage{AutoCreditStage::initial_fade_out};
int g_auto_credit_fade_level{};
hocus::DecodedImage g_auto_credit_base;
hocus::DecodedImage g_auto_credit_fade_source;
std::string g_save_name;
std::string g_save_original_name;
std::string g_high_score_name;
std::vector<std::string> g_dos_menu_lines;
bool g_dos_menu_has_heading{};
int g_dos_menu_selection{};

enum class DosMenuFade {
    none,
    fade_in,
    fade_out,
};

enum class DosMenuExit {
    none,
    accept,
    cancel,
};

DosMenuFade g_dos_menu_fade{DosMenuFade::none};
DosMenuExit g_dos_menu_exit{DosMenuExit::none};
int g_dos_menu_fade_level{20};
hocus::DecodedImage g_dos_menu_fade_source;
hocus::DosMenuStarfield g_dos_menu_stars;

enum class PageFade {
    none,
    fade_in,
    fade_out,
};

enum class PageExit {
    none,
    previous,
    next,
    first,
    last,
    cancel,
    advance,
};

PageFade g_page_fade{PageFade::none};
PageExit g_page_exit{PageExit::none};
int g_page_fade_level{20};
bool g_page_has_stars{};
hocus::DecodedImage g_page_base;
hocus::DecodedImage g_page_fade_source;

enum class WarpStage {
    fade_in,
    whiten,
    fade_out,
};

WarpStage g_warp_stage{WarpStage::fade_in};
int g_warp_step{};
hocus::DecodedImage g_warp_base;
hocus::DecodedImage g_warp_white;
std::filesystem::path g_save_path;
std::filesystem::path g_native_settings_path;
std::unique_ptr<hocus::SaveFile> g_save_file;
WINDOWPLACEMENT g_windowed_placement{};
DWORD g_windowed_style{};
std::array<std::uint8_t, 8> g_dos_key_bindings = {
    15, 16, 2, 3, 13, 14, 11, 12,
};
std::array<int, 8> g_key_bindings{};
int g_control_blink_ticks{};
int g_volume_preview_ticks{};
std::uint16_t g_cheat_scan_sum{};
std::uint16_t g_joystick_x_low{};
std::uint16_t g_joystick_x_center{};
std::uint16_t g_joystick_x_high{};
std::uint16_t g_joystick_y_low{};
std::uint16_t g_joystick_y_center{};
std::uint16_t g_joystick_y_high{};
int g_joystick_fire_button{};
bool g_joystick_calibration_succeeded{};
std::array<bool, hocus::cheat_code_count> g_cheat_toggles{};

constexpr std::array<hocus::CheatCode, hocus::cheat_code_count> cheat_codes = {
    hocus::CheatCode::full_health,
    hocus::CheatCode::both_keys,
    hocus::CheatCode::rapid_fire,
    hocus::CheatCode::laser_shots,
    hocus::CheatCode::midair_jump,
};
constexpr std::array<std::string_view, hocus::cheat_code_count>
    cheat_menu_labels = {
        "FEELGOOD - Full health / invincibility",
        "BLAKE - Infinite silver and gold keys",
        "QUARK - Permanent rapid fire",
        "BANANA - Infinite laser shots",
        "JUMP IN MID-AIR - Jump while airborne",
    };

int active_gameplay_frame_width() noexcept {
    switch (g_widescreen_mode) {
    case hocus::WidescreenMode::ratio_16_9:
        return hocus::widescreen_frame_width;
    case hocus::WidescreenMode::ratio_21_9:
        return hocus::ultrawide_frame_width;
    case hocus::WidescreenMode::ratio_32_9:
        return hocus::super_ultrawide_frame_width;
    case hocus::WidescreenMode::off:
        return hocus::original_frame_width;
    }
    return hocus::original_frame_width;
}

std::string_view widescreen_mode_label() noexcept {
    switch (g_widescreen_mode) {
    case hocus::WidescreenMode::ratio_16_9: return "16:9";
    case hocus::WidescreenMode::ratio_21_9: return "21:9";
    case hocus::WidescreenMode::ratio_32_9: return "32:9";
    case hocus::WidescreenMode::off: return "OFF";
    }
    return "OFF";
}

void advance_widescreen_mode() noexcept {
    switch (g_widescreen_mode) {
    case hocus::WidescreenMode::off:
        g_widescreen_mode = hocus::WidescreenMode::ratio_16_9;
        break;
    case hocus::WidescreenMode::ratio_16_9:
        g_widescreen_mode = hocus::WidescreenMode::ratio_21_9;
        break;
    case hocus::WidescreenMode::ratio_21_9:
        g_widescreen_mode = hocus::WidescreenMode::ratio_32_9;
        break;
    case hocus::WidescreenMode::ratio_32_9:
        g_widescreen_mode = hocus::WidescreenMode::off;
        break;
    }
}

// 0BA5:4D67 indexes DS:14F8's {8,7,6} counter ticks. 1392:006A installs
// that counter at 140 Hz. Keep the rational periods instead of rounding each
// update independently: Slow alternates 57/58 ms and Fast alternates 42/43 ms,
// while Medium remains exactly 50 ms.
constexpr std::array<int, 3> game_speed_timer_ticks = {8, 7, 6};
hocus::DosGameTimerCadence g_game_timer_cadence;
hocus::DosGameTimerCadence g_retrace_timer_cadence;
hocus::DosFrontendJoystickRepeat g_frontend_joystick_repeat;
hocus::DosFrontendJoystickSample g_frontend_joystick_latched;
hocus::DosJoystickDirectionFilter g_joystick_direction_filter;
ULONGLONG g_game_last_update_at{};
constexpr UINT high_fps_timer_id = 2;
constexpr UINT high_fps_interval_milliseconds = 8; // Up to 125 presentations/s.

class TimerResolution final {
public:
    TimerResolution() noexcept
        : active_(timeBeginPeriod(1) == TIMERR_NOERROR) {}
    ~TimerResolution() {
        if (active_) {
            timeEndPeriod(1);
        }
    }

    TimerResolution(const TimerResolution&) = delete;
    TimerResolution& operator=(const TimerResolution&) = delete;

private:
    bool active_{};
};

struct StoryPicture {
    int asset;
    int x;
    int y;
};

void apply_game_timer(HWND window);
void load_selected_level(bool show_crystal_tip = false);
void resume_level(HWND window);

void schedule_retrace_timer(HWND window) {
    // One VGA frame is two ticks of the same 140 Hz DOS counter. Preserve the
    // 100 ms total across seven frames instead of running every frame at the
    // rounded-down 14 ms interval.
    KillTimer(window, 1);
    SetTimer(window, 1, static_cast<UINT>(
        g_retrace_timer_cadence.next_interval_ms(2)), nullptr);
}

constexpr std::array<StoryPicture, 10> story_pictures = {{
    {123, 4, 16}, {121, 50, 25}, {129, 4, 25}, {124, 4, 21},
    {121, 50, 25}, {-1, 0, 0}, {-1, 0, 0}, {-1, 0, 0},
    {-1, 0, 0}, {121, 50, 25},
}};
constexpr int help_page_count = 4;
constexpr std::array<int, 2> credit_assets = {23, 24};

constexpr std::array<int, 4> ending_first_assets = {62, 64, 66, 70};
constexpr std::array<int, 4> ending_page_counts = {2, 2, 4, 2};
constexpr std::array<std::array<StoryPicture, 4>, 4> ending_pictures = {{
    {{{128, 4, 21}, {121, 50, 25}, {-1, 0, 0}, {-1, 0, 0}}},
    {{{121, 50, 25}, {121, 50, 25}, {-1, 0, 0}, {-1, 0, 0}}},
    {{{127, 4, 42}, {121, 50, 25}, {-1, 0, 0}, {121, 50, 25}}},
    {{{126, 4, 19}, {-1, 0, 0}, {-1, 0, 0}, {-1, 0, 0}}},
}};

// HOCUS.EXE registered v1.1 DS:2BF0 and DS:2C10. The game addresses sound
// effects by logical ID; the executable tables map those IDs to DAT entries
// and assign the priorities used by the original eight-voice allocator.
constexpr std::array<std::size_t, 16> sound_assets = {
    620, 624, 626, 628, 634, 630, 636, 638,
    640, 642, 644, 612, 618, 622, 646, 648,
};
constexpr std::array<int, 16> sound_priorities = {
    14, 13, 12, 12, 18, 17, 14, 16,
    15, 15, 16, 19, 15, 14, 13, 11,
};
std::array<std::vector<std::uint8_t>, 16> g_sound_waves;
std::array<std::vector<std::uint8_t>, 16> g_sound_playback_waves;
std::vector<std::uint8_t> g_startup_sound_wave;
std::vector<std::uint8_t> g_startup_sound_playback_wave;
std::vector<std::uint8_t> g_warp_sound_wave;
std::vector<std::uint8_t> g_warp_sound_playback_wave;
class DosDigitalSoundMixer final {
public:
    ~DosDigitalSoundMixer() {
        stop_all();
        if (device_ != nullptr) {
            device_->Release();
        }
    }

    DosDigitalSoundMixer(const DosDigitalSoundMixer&) = delete;
    DosDigitalSoundMixer& operator=(const DosDigitalSoundMixer&) = delete;

    DosDigitalSoundMixer() = default;

    bool play(HWND window, const std::vector<std::uint8_t>& wave,
              const int priority) {
        if (wave.size() < 44 || !initialize(window)) {
            return false;
        }
        release_finished();

        Voice* selected = nullptr;
        for (auto& voice : voices_) {
            if (voice.buffer == nullptr) {
                selected = &voice;
                break;
            }
        }
        if (selected == nullptr) {
            // 197E:0613 scans the active list from its head and stops the
            // first voice whose priority is <= the incoming request. Its
            // linked list is insertion ordered, so this is the oldest such
            // voice; equal priority is replaceable.
            for (auto& voice : voices_) {
                if (voice.priority <= priority &&
                    (selected == nullptr || voice.order < selected->order)) {
                    selected = &voice;
                }
            }
        }
        if (selected == nullptr) {
            return false;
        }
        release(*selected);

        WAVEFORMATEX format{};
        format.wFormatTag = wave_u16(wave, 20);
        format.nChannels = wave_u16(wave, 22);
        format.nSamplesPerSec = wave_u32(wave, 24);
        format.nAvgBytesPerSec = wave_u32(wave, 28);
        format.nBlockAlign = wave_u16(wave, 32);
        format.wBitsPerSample = wave_u16(wave, 34);
        if (format.wFormatTag != WAVE_FORMAT_PCM || format.nChannels == 0 ||
            format.nSamplesPerSec == 0 || format.nBlockAlign == 0) {
            return false;
        }

        const auto data_size = static_cast<DWORD>(wave.size() - 44);
        DSBUFFERDESC description{};
        description.dwSize = sizeof(description);
        description.dwFlags = DSBCAPS_GLOBALFOCUS;
        description.dwBufferBytes = data_size;
        description.lpwfxFormat = &format;
        if (FAILED(device_->CreateSoundBuffer(
                &description, &selected->buffer, nullptr))) {
            selected->buffer = nullptr;
            return false;
        }

        void* first{};
        void* second{};
        DWORD first_size{};
        DWORD second_size{};
        if (FAILED(selected->buffer->Lock(
                0, data_size, &first, &first_size, &second, &second_size, 0))) {
            release(*selected);
            return false;
        }
        std::memcpy(first, wave.data() + 44, first_size);
        if (second_size != 0) {
            std::memcpy(second, wave.data() + 44 + first_size, second_size);
        }
        selected->buffer->Unlock(first, first_size, second, second_size);
        selected->priority = priority;
        selected->order = next_order_++;
        selected->buffer->SetCurrentPosition(0);
        if (FAILED(selected->buffer->Play(0, 0, 0))) {
            release(*selected);
            return false;
        }
        return true;
    }

    void stop_all() noexcept {
        for (auto& voice : voices_) {
            release(voice);
        }
    }

private:
    struct Voice {
        LPDIRECTSOUNDBUFFER buffer{};
        int priority{};
        std::uint64_t order{};
    };

    static std::uint16_t wave_u16(const std::vector<std::uint8_t>& wave,
                                  const std::size_t offset) {
        return static_cast<std::uint16_t>(wave[offset]) |
               static_cast<std::uint16_t>(wave[offset + 1] << 8U);
    }

    static std::uint32_t wave_u32(const std::vector<std::uint8_t>& wave,
                                  const std::size_t offset) {
        return static_cast<std::uint32_t>(wave[offset]) |
               (static_cast<std::uint32_t>(wave[offset + 1]) << 8U) |
               (static_cast<std::uint32_t>(wave[offset + 2]) << 16U) |
               (static_cast<std::uint32_t>(wave[offset + 3]) << 24U);
    }

    bool initialize(HWND window) {
        if (device_ != nullptr) {
            return true;
        }
        if (initialization_attempted_) {
            return false;
        }
        initialization_attempted_ = true;
        if (FAILED(DirectSoundCreate(nullptr, &device_, nullptr))) {
            device_ = nullptr;
            return false;
        }
        if (FAILED(device_->SetCooperativeLevel(window, DSSCL_NORMAL))) {
            device_->Release();
            device_ = nullptr;
            return false;
        }
        return true;
    }

    static void release(Voice& voice) noexcept {
        if (voice.buffer != nullptr) {
            voice.buffer->Stop();
            voice.buffer->Release();
        }
        voice = {};
    }

    void release_finished() noexcept {
        for (auto& voice : voices_) {
            if (voice.buffer == nullptr) {
                continue;
            }
            DWORD status{};
            if (FAILED(voice.buffer->GetStatus(&status)) ||
                (status & DSBSTATUS_PLAYING) == 0) {
                release(voice);
            }
        }
    }

    // The user's registered HOCUS.SAV stores eight at DS:4C90, which is
    // passed to 197E:0931 as the digital mixer voice limit.
    std::array<Voice, 8> voices_{};
    LPDIRECTSOUND device_{};
    std::uint64_t next_order_{};
    bool initialization_attempted_{};
};

DosDigitalSoundMixer g_sound_mixer;
MCIDEVICEID g_music_device{};
std::optional<std::size_t> g_music_asset;
bool g_music_looping{};
std::filesystem::path g_music_path;
JOYCAPSW g_joystick_caps{};
using XInputGetStateFunction = DWORD (WINAPI*)(DWORD, XINPUT_STATE*);
HMODULE g_xinput_module{};
XInputGetStateFunction g_xinput_get_state{};
DWORD g_xinput_user_index{};
std::uint16_t g_previous_xinput_buttons{};

constexpr std::array<int, 18> dos_key_virtual_keys = {
    VK_LSHIFT, VK_RSHIFT, VK_CONTROL, VK_MENU, VK_CAPITAL, VK_SPACE,
    VK_RETURN, VK_INSERT, VK_DELETE, VK_HOME, VK_END, VK_PRIOR, VK_NEXT,
    VK_UP, VK_DOWN, VK_LEFT, VK_RIGHT, VK_NUMPAD5,
};

void apply_dos_settings() {
    const auto settings = g_save_file->dos_settings();
    g_sound_enabled = settings.sound_enabled;
    g_music_enabled = settings.music_enabled;
    g_joystick_enabled = settings.joystick_enabled;
    g_game_speed = settings.game_speed;
    g_joystick_x_low = settings.joystick_x_low;
    g_joystick_x_center = settings.joystick_x_center;
    g_joystick_x_high = settings.joystick_x_high;
    g_joystick_y_low = settings.joystick_y_low;
    g_joystick_y_center = settings.joystick_y_center;
    g_joystick_y_high = settings.joystick_y_high;
    g_joystick_fire_button = settings.joystick_fire_button;
    g_dos_key_bindings = settings.key_bindings;
    g_sound_volume = settings.sound_volume;
    g_music_volume = settings.music_volume;
    for (std::size_t index = 0; index < g_key_bindings.size(); ++index) {
        g_key_bindings[index] = dos_key_virtual_keys[
            g_dos_key_bindings[index]];
    }
}

void write_dos_settings() {
    hocus::DosSettings settings;
    settings.sound_enabled = g_sound_enabled;
    settings.music_enabled = g_music_enabled;
    settings.joystick_enabled = g_joystick_enabled;
    settings.game_speed = g_game_speed;
    settings.joystick_x_low = g_joystick_x_low;
    settings.joystick_x_center = g_joystick_x_center;
    settings.joystick_x_high = g_joystick_x_high;
    settings.joystick_y_low = g_joystick_y_low;
    settings.joystick_y_center = g_joystick_y_center;
    settings.joystick_y_high = g_joystick_y_high;
    settings.joystick_fire_button = g_joystick_fire_button;
    settings.key_bindings = g_dos_key_bindings;
    settings.sound_volume = g_sound_volume;
    settings.music_volume = g_music_volume;
    g_save_file->set_dos_settings(settings);
    g_save_file->write(g_save_path);
}

void load_sound_effects() {
    for (std::size_t logical = 0; logical < sound_assets.size(); ++logical) {
        auto& wave = g_sound_waves[logical];
        wave = hocus::decode_voc_to_wav(g_archive->read(sound_assets[logical]));
    }
    // 0B97:00CA invokes the raw asset loader for SOUND01.VOC directly; this
    // startup cue is not part of the sixteen-entry logical gameplay table.
    g_startup_sound_wave = hocus::decode_voc_to_wav(g_archive->read(611));
    // 06B8:4DA7 addresses SOUND04.VOC directly after drawing WARP.PCX.
    g_warp_sound_wave = hocus::decode_voc_to_wav(g_archive->read(615));
    g_sound_playback_waves = g_sound_waves;
    g_startup_sound_playback_wave = g_startup_sound_wave;
    g_warp_sound_playback_wave = g_warp_sound_wave;
}

void rebuild_sound_playback_waves() {
    const auto scale_wave = [](std::vector<std::uint8_t>& wave) {
        // decode_voc_to_wav always emits unsigned eight-bit mono PCM with a
        // canonical 44-byte header, so scaling about the 128 midpoint gives
        // this native port a per-app volume without changing Windows' mixer.
        for (std::size_t sample = 44; sample < wave.size(); ++sample) {
            const int centred = static_cast<int>(wave[sample]) - 128;
            wave[sample] = static_cast<std::uint8_t>(std::clamp(
                128 + centred * g_sound_volume / 15, 0, 255));
        }
    };
    for (std::size_t logical = 0; logical < g_sound_waves.size(); ++logical) {
        g_sound_playback_waves[logical] = g_sound_waves[logical];
        scale_wave(g_sound_playback_waves[logical]);
    }
    g_startup_sound_playback_wave = g_startup_sound_wave;
    scale_wave(g_startup_sound_playback_wave);
    g_warp_sound_playback_wave = g_warp_sound_wave;
    scale_wave(g_warp_sound_playback_wave);
}

void play_pending_sounds(HWND window) {
    const auto events = g_game->take_sound_events();
    if (events.empty() || !g_sound_enabled) {
        return;
    }
    // 136B:000B forwards requests as they occur. The eight-voice allocator at
    // 197E:0613 handles capacity and priority; it does not collapse a game
    // phase to one winning effect.
    for (const int selected : events) {
        const auto index = static_cast<std::size_t>(selected);
        (void)g_sound_mixer.play(
            window, g_sound_playback_waves[index],
            sound_priorities[index]);
    }
}

void play_volume_preview_sound(HWND window) {
    if (!g_sound_enabled) {
        return;
    }
    // 06B8:158B advances RANDOM.DAT, takes modulo 16, and sends that logical
    // ID through the same logical sound facade once every 51 loop ticks.
    int selected = g_dos_menu_stars.next_random_below(16);
    if (selected < 0) {
        selected += 16;
    }
    const auto& wave =
        g_sound_playback_waves[static_cast<std::size_t>(selected)];
    (void)g_sound_mixer.play(
        window, wave,
        sound_priorities[static_cast<std::size_t>(selected)]);
}

void close_music() {
    if (g_music_device != 0) {
        mciSendStringW(L"close hocus_music", nullptr, 0, nullptr);
        g_music_device = 0;
    }
    g_music_asset.reset();
    g_music_looping = false;
}

void apply_music_volume() {
    if (g_music_device == 0) {
        return;
    }
    const auto command = L"setaudio hocus_music volume to " +
                         std::to_wstring(g_music_volume * 1000 / 15);
    // Some legacy MCI sequencers do not implement SETAUDIO. The call is kept
    // local to the open device and failing it must never alter system volume
    // or interrupt MIDI playback.
    (void)mciSendStringW(command.c_str(), nullptr, 0, nullptr);
}

bool play_music_asset(HWND window, const std::size_t asset,
                      const bool looping = true,
                      const bool force_restart = false) {
    if (!g_music_enabled) {
        close_music();
        return false;
    }
    if (!force_restart && g_music_device != 0 && g_music_asset == asset) {
        g_music_looping = looping;
        apply_music_volume();
        return true;
    }
    close_music();

    if (g_music_path.empty()) {
        g_music_path = std::filesystem::temp_directory_path() /
            (L"hocus-native-" + std::to_wstring(GetCurrentProcessId()) + L".mid");
    }
    const auto midi = g_archive->read(asset);
    {
        std::ofstream output(g_music_path, std::ios::binary | std::ios::trunc);
        if (!output) {
            return false;
        }
        output.write(reinterpret_cast<const char*>(midi.data()),
                     static_cast<std::streamsize>(midi.size()));
        if (!output) {
            return false;
        }
    }

    const auto open_command = L"open \"" + g_music_path.wstring() +
                              L"\" type sequencer alias hocus_music";
    if (mciSendStringW(open_command.c_str(), nullptr, 0, window) != 0) {
        return false;
    }
    g_music_device = mciGetDeviceIDW(L"hocus_music");
    if (g_music_device == 0) {
        close_music();
        return false;
    }
    apply_music_volume();
    if (mciSendStringW(L"play hocus_music notify", nullptr, 0, window) != 0) {
        close_music();
        return false;
    }
    g_music_asset = asset;
    g_music_looping = looping;
    return true;
}

std::wstring level_label(const hocus::LevelId level) {
    return L"E" + std::to_wstring(level.episode) + L"L" +
           std::to_wstring(level.number);
}

bool is_dos_menu_screen(const FrontendScreen screen) {
    switch (screen) {
    case FrontendScreen::main_menu:
    case FrontendScreen::episode_menu:
    case FrontendScreen::skill_menu:
    case FrontendScreen::pause_menu:
    case FrontendScreen::options_menu:
    case FrontendScreen::game_speed:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] hocus::DecodedImage compose_active_dos_menu(
    const int cursor_frame) {
    const auto rendered = hocus::render_dos_menu(
        g_menu_logo, g_bottom, g_menu_cursor_sheet, g_font_mask, g_game_palette,
        g_dos_menu_lines, g_dos_menu_has_heading, g_dos_menu_selection,
        cursor_frame);
    auto image = rendered.image;
    if (cursor_frame >= 0 && g_dos_menu_stars.initialized()) {
        g_dos_menu_stars.draw(image);
    }
    return image;
}

void render_active_dos_menu(HWND window) {
    g_frame = compose_active_dos_menu((g_menu_cursor_ticks / 5) % 8);
    InvalidateRect(window, nullptr, FALSE);
}

void show_dos_menu(HWND window, const FrontendScreen screen,
                   std::vector<std::string> lines, const bool has_heading,
                   const int selection, const wchar_t* title) {
    g_show_level = false;
    g_frontend_screen = screen;
    g_dos_menu_lines = std::move(lines);
    g_dos_menu_has_heading = has_heading;
    g_dos_menu_selection = selection;
    g_menu_cursor_ticks = 0;
    g_dos_menu_fade = DosMenuFade::fade_in;
    g_dos_menu_exit = DosMenuExit::none;
    g_dos_menu_fade_level = 0;
    g_dos_menu_fade_source = compose_active_dos_menu(-1);
    g_frame = hocus::render_vga_fade(g_dos_menu_fade_source, {0, 20});
    schedule_retrace_timer(window); // 06B8:0103 waits one VGA retrace.
    SetWindowTextW(window, title);
    InvalidateRect(window, nullptr, FALSE);
}

void change_dos_menu_selection(HWND window, int& persisted_selection,
                               const int selection) {
    persisted_selection = selection;
    g_dos_menu_selection = selection;
    render_active_dos_menu(window);
}

int* active_dos_menu_selection() {
    switch (g_frontend_screen) {
    case FrontendScreen::main_menu: return &g_menu_selection;
    case FrontendScreen::episode_menu: return &g_episode_selection;
    case FrontendScreen::skill_menu: return &g_skill_selection;
    case FrontendScreen::pause_menu: return &g_pause_selection;
    case FrontendScreen::options_menu: return &g_options_selection;
    case FrontendScreen::game_speed: return &g_game_speed;
    default: return nullptr;
    }
}

bool navigate_dos_menu(HWND window, const WPARAM key) {
    if (!is_dos_menu_screen(g_frontend_screen)) {
        return false;
    }
    int* const persisted = active_dos_menu_selection();
    if (persisted == nullptr) {
        return false;
    }
    const int item_count = static_cast<int>(g_dos_menu_lines.size()) -
                           (g_dos_menu_has_heading ? 1 : 0);
    if (key == VK_UP || key == VK_DOWN) {
        const int adjustment = key == VK_UP ? -1 : 1;
        change_dos_menu_selection(
            window, *persisted,
            (*persisted + adjustment + item_count) % item_count);
        return true;
    }
    if (key < 32 || key > 126) {
        return false;
    }
    const auto pressed = static_cast<unsigned char>(
        std::toupper(static_cast<unsigned char>(key)));
    const std::size_t first_item = g_dos_menu_has_heading ? 1 : 0;
    for (std::size_t index = first_item; index < g_dos_menu_lines.size();
         ++index) {
        if (g_dos_menu_lines[index].empty()) {
            continue;
        }
        const auto shortcut = static_cast<unsigned char>(
            hocus::dos_menu_shortcut_key(g_dos_menu_lines[index]));
        if (pressed == shortcut) {
            change_dos_menu_selection(
                window, *persisted, static_cast<int>(index - first_item));
            return true;
        }
    }
    return false;
}

void begin_dos_menu_exit(HWND window, const DosMenuExit exit) {
    if (g_dos_menu_fade != DosMenuFade::none ||
        exit == DosMenuExit::none) {
        return;
    }
    g_dos_menu_exit = exit;
    g_dos_menu_fade = DosMenuFade::fade_out;
    g_dos_menu_fade_level = 20;
    g_dos_menu_fade_source = g_frame;
    schedule_retrace_timer(window);
}

void update_title_screen(HWND window) {
    std::vector<std::string> options = {
        "Begin a new game", "Restore an old game", "Ordering Information",
        "Instructions", "Legends and hints~", "Change game options",
        "High scores",
        std::string("HIGH &FPS MODE: ") + (g_high_fps_mode ? "ON" : "OFF"),
        std::string("&WIDESCREEN MODE: ") +
            std::string(widescreen_mode_label()),
        "Quit - return to DOS",
    };
    options[2].push_back('~');
    g_show_level = false;
    g_demo_playing = false;
    g_demo_frames.clear();
    g_demo_frame_index = 0;
    g_auto_credits = false;
    g_attract_credits_shown = false;
    show_dos_menu(window, FrontendScreen::main_menu, std::move(options), false,
                  g_menu_selection, L"Hocus Native - Main Menu");
    play_music_asset(window, 602); // TITLE.MID
}

void update_episode_screen(HWND window) {
    std::vector<std::string> episodes = {
        "Which game do you want to play?", "Time Tripping",
        "Shattered Worlds", "Warped and Weary", "Destination Home",
    };
    show_dos_menu(window, FrontendScreen::episode_menu, std::move(episodes),
                  true, g_episode_selection,
                  L"Hocus Native - Select Episode");
}

void update_skill_screen(HWND window) {
    std::vector<std::string> skills = {
        "Choose a skill level", "Easy game - good for beginners",
        "Moderate - a resonable challenge", "Hard - the ultimate battle!",
    };
    show_dos_menu(window, FrontendScreen::skill_menu, std::move(skills), true,
                  g_skill_selection, L"Hocus Native - Select Skill");
}

std::array<int, help_page_count> active_help_assets() {
    // 06B8:44CC selects HELP1_J.PCX when joystick control is enabled and
    // HELP1_K.PCX otherwise, then always shows HELP2, GRAVIS, and HELP3.
    return g_joystick_enabled
        ? std::array<int, help_page_count>{26, 27, 28, 29}
        : std::array<int, help_page_count>{25, 27, 28, 29};
}

void begin_page_fade_in(HWND window, const bool has_stars = true) {
    g_page_base = g_frame;
    g_page_fade_source = g_page_base;
    g_page_has_stars = has_stars;
    g_page_exit = PageExit::none;
    g_page_fade = PageFade::fade_in;
    g_page_fade_level = 0;
    g_frame = hocus::render_vga_fade(g_page_fade_source, {0, 20});
    schedule_retrace_timer(window);
    InvalidateRect(window, nullptr, FALSE);
}

void update_story_screen(HWND window) {
    const auto page = hocus::decode_text_page(
        g_archive->read(52 + static_cast<std::size_t>(g_page_index)));
    const auto placement = story_pictures[static_cast<std::size_t>(g_page_index)];
    std::optional<hocus::DecodedImage> picture;
    if (placement.asset >= 0) {
        picture = hocus::decode_planar_img(
            g_archive->read(static_cast<std::size_t>(placement.asset)),
            g_game_palette);
    }
    g_frame = hocus::render_text_page(
        page, g_font_mask, g_game_palette, g_bottom,
        {hocus::TextPageLayout::illustrated,
         picture ? &*picture : nullptr, placement.x, placement.y,
         g_page_index, 10});
    g_show_level = false;
    g_frontend_screen = FrontendScreen::story;
    SetWindowTextW(window, L"Hocus Native - The Story of Hocus Pocus");
    begin_page_fade_in(window);
}

void update_reference_screen(HWND window) {
    const bool help = g_frontend_screen == FrontendScreen::help;
    const auto help_assets = active_help_assets();
    const auto asset = help ? help_assets[static_cast<std::size_t>(g_page_index)]
                            : credit_assets[static_cast<std::size_t>(g_page_index)];
    g_frame = hocus::decode_pcx(g_archive->read(static_cast<std::size_t>(asset)));
    const auto title = help ? L"Hocus Native - Instructions"
                            : L"Hocus Native - Credits";
    SetWindowTextW(window, title);
    g_show_level = false;
    begin_page_fade_in(window);
}

void update_ordering_screen(HWND window) {
    constexpr int page_count = 9;
    const auto page = hocus::decode_text_page(
        g_archive->read(41 + static_cast<std::size_t>(g_page_index)));
    g_frame = hocus::render_text_page(
        page, g_font_mask, g_game_palette, g_bottom,
        {hocus::TextPageLayout::plain, &g_menu_logo, 0, 0,
         g_page_index, page_count});
    g_show_level = false;
    g_frontend_screen = FrontendScreen::ordering;
    SetWindowTextW(window, L"Hocus Native - Ordering Information");
    begin_page_fade_in(window);
}

bool is_high_score_screen(const FrontendScreen screen) {
    return screen == FrontendScreen::high_scores ||
           screen == FrontendScreen::high_score_name;
}

hocus::DecodedImage compose_active_high_scores(const bool with_stars = true) {
    const auto scores = g_save_file->high_scores(
        static_cast<std::size_t>(g_high_score_episode));
    std::array<hocus::DosHighScoreEntry, hocus::SaveFile::high_score_count>
        entries;
    for (std::size_t rank = 0; rank < scores.size(); ++rank) {
        entries[rank] = {scores[rank].name, scores[rank].score};
    }
    const bool editing = g_frontend_screen == FrontendScreen::high_score_name;
    if (editing) {
        entries[static_cast<std::size_t>(g_high_score_edit_rank)].name =
            g_high_score_name;
    }
    auto frame = hocus::render_dos_high_scores(
        g_menu_logo, g_bottom, g_font_mask, g_game_palette,
        g_high_score_episode, entries, editing ? g_high_score_edit_rank : -1,
        editing ? "Enter name then press ENTER"
                : "Press any key to continue");
    if (with_stars && g_dos_menu_stars.initialized()) {
        g_dos_menu_stars.draw(frame);
    }
    return frame;
}

void render_active_high_scores(HWND window) {
    g_frame = compose_active_high_scores();
    InvalidateRect(window, nullptr, FALSE);
}

void update_high_scores_screen(HWND window) {
    g_show_level = false;
    g_frontend_screen = FrontendScreen::high_scores;
    g_frame = compose_active_high_scores(false);
    SetWindowTextW(window, L"Hocus Native - High Scores");
    begin_page_fade_in(window);
}

void update_high_score_name_screen(HWND window) {
    g_show_level = false;
    g_frontend_screen = FrontendScreen::high_score_name;
    g_frame = compose_active_high_scores(false);
    SetWindowTextW(window, L"Hocus Native - Enter High Score Name");
    begin_page_fade_in(window);
}

void finish_episode(HWND window) {
    // 06B8:428A restores TITLE.MID after KISS.MID and before the Episode Four
    // high-score check.  The other episode paths already have this track from
    // 0BA5:69C5, so this is also an idempotent boundary guarantee.
    play_music_asset(window, 602);
    g_high_score_episode = g_ending_episode;
    const auto rank = g_save_file->qualifying_high_score_rank(
            static_cast<std::size_t>(g_high_score_episode),
            static_cast<std::uint32_t>(g_campaign_score));
    if (rank) {
        g_high_score_edit_rank = static_cast<int>(*rank);
        g_high_score_name.clear();
        g_save_file->insert_high_score(
            static_cast<std::size_t>(g_high_score_episode), "",
            static_cast<std::uint32_t>(g_campaign_score));
        update_high_score_name_screen(window);
    } else {
        update_title_screen(window);
    }
}

void update_ending_screen(HWND window) {
    const auto episode = static_cast<std::size_t>(g_ending_episode);
    const auto page = hocus::decode_text_page(g_archive->read(
        static_cast<std::size_t>(ending_first_assets[episode] + g_page_index)));
    const auto placement =
        ending_pictures[episode][static_cast<std::size_t>(g_page_index)];
    std::optional<hocus::DecodedImage> picture;
    if (placement.asset >= 0) {
        picture = hocus::decode_planar_img(
            g_archive->read(static_cast<std::size_t>(placement.asset)),
            g_game_palette);
    }
    g_frame = hocus::render_text_page(
        page, g_font_mask, g_game_palette, g_bottom,
        {hocus::TextPageLayout::illustrated,
         picture ? &*picture : nullptr, placement.x, placement.y,
         g_page_index, ending_page_counts[episode]});
    g_show_level = false;
    g_frontend_screen = FrontendScreen::ending;
    SetWindowTextW(window, L"Hocus Native - Episode Complete");
    begin_page_fade_in(window);
}

void update_finale_screen(HWND window) {
    g_frame = hocus::decode_pcx(g_archive->read(72)); // KISS.PCX
    g_show_level = false;
    g_frontend_screen = FrontendScreen::finale;
    play_music_asset(window, 610); // KISS.MID
    SetWindowTextW(window, L"Hocus Native - Destination Home Complete");
    begin_page_fade_in(window, false);
}

void update_level_results_screen(HWND window) {
    g_show_level = false;
    g_frontend_screen = FrontendScreen::level_results;
    // 0BA5:69C5 starts TITLE.MID immediately before either the successful or
    // failed 06B8:4889 results page.  The level MIDI must not continue under
    // this screen; resume_level() restores the level's own track afterward.
    play_music_asset(window, 602);
    const auto& progress = g_game->progress();
    g_frame = hocus::render_dos_level_results(
        g_font_mask, g_game_palette, g_result_complete,
        g_selected_level.number, progress.treasures,
        progress.total_treasures, g_result_accuracy, g_skill,
        g_result_time_limit, g_result_elapsed_seconds);
    SetWindowTextW(window, L"Hocus Native - Level Results");
    begin_page_fade_in(window);
}

void begin_level_results(HWND window) {
    g_result_complete = true;
    const auto now = GetTickCount64();
    if (now >= g_level_started_at) {
        g_level_elapsed_ms = now - g_level_started_at;
    }
    const auto& progress = g_game->progress();
    const auto results = hocus::calculate_level_results(
        g_selected_level, g_skill, progress.treasures,
        progress.total_treasures,
        static_cast<int>(g_level_elapsed_ms / 1000));
    g_result_accuracy = results.treasure_accuracy;
    g_result_elapsed_seconds = results.elapsed_seconds;
    g_result_time_limit = results.time_limit;
    g_result_treasure_bonus = results.treasure_bonus;
    g_result_time_bonus = results.time_bonus;
    g_campaign_score = std::max(
        0, progress.score + g_result_treasure_bonus + g_result_time_bonus);
    update_level_results_screen(window);
}

void begin_failed_level_results(HWND window) {
    // 0BA5:69F9 calls 06B8:4889 with a zero completion flag only after the
    // 90-tick death sequence reaches one. Manual Abandon follows a separate
    // immediate-reload branch and never presents this page.
    g_result_complete = false;
    const auto now = GetTickCount64();
    if (now >= g_level_started_at) {
        g_level_elapsed_ms = now - g_level_started_at;
    }
    const auto& progress = g_game->progress();
    g_result_accuracy = progress.total_treasures == 0
        ? 0 : progress.treasures * 100 / progress.total_treasures;
    g_result_elapsed_seconds = static_cast<int>(g_level_elapsed_ms / 1000);
    g_result_time_limit = 0;
    g_result_treasure_bonus = 0;
    g_result_time_bonus = 0;
    update_level_results_screen(window);
}

void begin_warp_transition(HWND window) {
    // 06B8:4D76 decodes WARP.PCX without programming its palette, starts the
    // raw SOUND04.VOC cue, fades in for 60 retraces, raises every DAC channel
    // toward white for 70 retraces, then fades the white palette out in 20.
    g_show_level = false;
    g_frontend_screen = FrontendScreen::warp;
    g_warp_base = hocus::decode_pcx(g_archive->read(4));
    if (g_warp_base.width != 320 || g_warp_base.height != 200) {
        throw std::runtime_error("Unexpected WARP.PCX dimensions");
    }
    g_warp_stage = WarpStage::fade_in;
    g_warp_step = 0;
    g_frame = hocus::render_vga_fade(g_warp_base, {0, 60});
    if (g_sound_enabled && !g_warp_sound_playback_wave.empty()) {
        (void)g_sound_mixer.play(window, g_warp_sound_playback_wave, 1);
    }
    schedule_retrace_timer(window);
    SetWindowTextW(window, L"Hocus Native - Warp");
    InvalidateRect(window, nullptr, FALSE);
}

void update_warp_transition(HWND window) {
    switch (g_warp_stage) {
    case WarpStage::fade_in:
        g_warp_step = std::min(g_warp_step + 1, 60);
        g_frame = hocus::render_vga_fade(
            g_warp_base, {g_warp_step, 60});
        if (g_warp_step == 60) {
            g_warp_stage = WarpStage::whiten;
            g_warp_step = 0;
        }
        break;
    case WarpStage::whiten:
        g_warp_step = std::min(g_warp_step + 1, 70);
        g_frame = hocus::render_vga_whiten(g_warp_base, g_warp_step);
        if (g_warp_step == 70) {
            g_warp_white = g_frame;
            g_warp_stage = WarpStage::fade_out;
            g_warp_step = 20;
        }
        break;
    case WarpStage::fade_out:
        g_warp_step = std::max(g_warp_step - 1, 0);
        g_frame = hocus::render_vga_fade(
            g_warp_white, {g_warp_step, 20});
        if (g_warp_step == 0) {
            g_page_index = 0;
            update_ending_screen(window);
            return;
        }
        break;
    }
    InvalidateRect(window, nullptr, FALSE);
}

void finish_level_results(HWND window) {
    if (!g_result_complete) {
        load_selected_level();
        resume_level(window);
    } else if (g_selected_level.number < 9) {
        ++g_selected_level.number;
        load_selected_level();
        resume_level(window);
    } else {
        g_ending_episode = g_selected_level.episode - 1;
        begin_warp_transition(window);
    }
}

void start_auto_credits(HWND window) {
    g_auto_credits = true;
    g_page_index = 0;
    g_show_level = false;
    g_frontend_screen = FrontendScreen::credits;
    g_auto_credit_stage = AutoCreditStage::initial_fade_out;
    g_auto_credit_fade_level = 20;
    g_auto_credit_fade_source = g_frame;
    schedule_retrace_timer(window);
    SetWindowTextW(window, L"Hocus Native - Credits");
}

void return_from_auto_credits(HWND window) {
    update_title_screen(window);
    g_attract_credits_shown = true;
}

void load_auto_credit_page(HWND window) {
    g_auto_credit_base = hocus::decode_pcx(g_archive->read(
        credit_assets[static_cast<std::size_t>(g_page_index)]));
    g_auto_credit_fade_source = g_auto_credit_base;
    g_auto_credit_stage = AutoCreditStage::page_fade_in;
    g_auto_credit_fade_level = 0;
    g_frame = hocus::render_vga_fade(g_auto_credit_fade_source, {0, 20});
    InvalidateRect(window, nullptr, FALSE);
}

void begin_auto_credit_page_exit() {
    if (!g_auto_credits ||
        g_auto_credit_stage != AutoCreditStage::page_hold) {
        return;
    }
    g_auto_credit_stage = AutoCreditStage::page_fade_out;
    g_auto_credit_fade_level = 15;
    g_auto_credit_fade_source = g_frame;
}

void update_auto_credits(HWND window) {
    switch (g_auto_credit_stage) {
    case AutoCreditStage::initial_fade_out:
        if (--g_auto_credit_fade_level > 0) {
            g_frame = hocus::render_vga_fade(
                g_auto_credit_fade_source,
                {g_auto_credit_fade_level, 20});
            InvalidateRect(window, nullptr, FALSE);
        } else {
            load_auto_credit_page(window);
        }
        break;
    case AutoCreditStage::page_fade_in:
        if (++g_auto_credit_fade_level < 20) {
            g_frame = hocus::render_vga_fade(
                g_auto_credit_fade_source,
                {g_auto_credit_fade_level, 20});
            InvalidateRect(window, nullptr, FALSE);
        } else {
            g_auto_credit_fade_level = 20;
            g_auto_credit_stage = AutoCreditStage::page_hold;
            g_auto_credit_started = GetTickCount64();
            g_frame = g_auto_credit_base;
            g_dos_menu_stars.tick();
            g_dos_menu_stars.draw(g_frame);
            InvalidateRect(window, nullptr, FALSE);
        }
        break;
    case AutoCreditStage::page_hold:
        g_frame = g_auto_credit_base;
        g_dos_menu_stars.tick();
        g_dos_menu_stars.draw(g_frame);
        InvalidateRect(window, nullptr, FALSE);
        if (GetTickCount64() - g_auto_credit_started >=
            auto_credit_hold_milliseconds) {
            begin_auto_credit_page_exit();
        }
        break;
    case AutoCreditStage::page_fade_out:
        if (--g_auto_credit_fade_level > 0) {
            g_frame = hocus::render_vga_fade(
                g_auto_credit_fade_source,
                {g_auto_credit_fade_level, 15});
            InvalidateRect(window, nullptr, FALSE);
        } else if (++g_page_index <
                   static_cast<int>(credit_assets.size())) {
            load_auto_credit_page(window);
        } else {
            return_from_auto_credits(window);
        }
        break;
    }
}

void load_selected_level(const bool show_crystal_tip) {
    g_game = std::make_unique<hocus::GameLevel>(
        *g_archive, g_selected_level, g_campaign_score, g_skill,
        show_crystal_tip, active_gameplay_frame_width());
    for (std::size_t index = 0; index < cheat_codes.size(); ++index) {
        g_game->set_cheat_enabled(cheat_codes[index],
                                  g_cheat_toggles[index]);
    }
    // 0BA5:4D0D resets the shared RANDOM.DAT cursor at every level entry.
    g_dos_menu_stars.set_random_index(0);
    g_level_elapsed_ms = 0;
    // 0BA5:4D36 resets the independent 140 Hz counter. That counter keeps
    // advancing while pause, help, save, and message loops are displayed;
    // level-result time therefore uses one uninterrupted monotonic epoch.
    g_level_started_at = GetTickCount64();
    g_game_last_update_at = g_level_started_at;
    g_game_timer_cadence.reset();
    // 136B:01FF reloads and submits the MIDI on every new level entry. Modal
    // front-end returns stay inside the owning level loop and must not restart
    // it, so defer this distinction until resume_level().
    g_level_music_needs_restart = true;
}

double current_render_interpolation() {
    if (!g_high_fps_mode || g_game_last_update_at == 0) {
        return 1.0;
    }
    const auto now = GetTickCount64();
    const auto elapsed = now >= g_game_last_update_at
        ? now - g_game_last_update_at : 0;
    const auto timer_ticks = game_speed_timer_ticks[
        static_cast<std::size_t>(g_game_speed)];
    return std::clamp(
        static_cast<double>(elapsed * hocus::dos_timer_hz) /
            static_cast<double>(timer_ticks * 1000),
        0.0, 1.0);
}

void update_level(HWND window) {
    const auto level = g_game->render(current_render_interpolation());
    g_frame = level.image;
    const auto& progress = g_game->progress();
    const auto title = L"Hocus Native - " +
                       std::wstring(g_demo_playing ? L"DEMO - " : L"") +
                       level_label(g_game->level_id()) +
                       L"  HP " +
                       std::to_wstring(progress.health) + L"  Crystals " +
                       std::to_wstring(progress.crystals) + L"/" +
                       std::to_wstring(progress.total_crystals) + L"  Score " +
                       std::to_wstring(progress.score) +
                       (g_game->level_complete() ? L"  LEVEL COMPLETE" : L"") +
                       (g_demo_playing
                            ? L"  (press any key to return)"
                            : L"  (Esc menu)");
    SetWindowTextW(window, title.c_str());
    InvalidateRect(window, nullptr, FALSE);
}

void start_demo(HWND window) {
    // 0548:0570 calls Borland rand() and takes the signed-long remainder by
    // five. rand() is nonnegative, so the native unsigned remainder is exact.
    const auto demo_index =
        static_cast<std::size_t>(g_borland_random.next() % 5u);
    g_demo_frames = hocus::decode_demo(g_archive->read(18 + demo_index));
    g_demo_frame_index = 0;
    g_game = std::make_unique<hocus::GameLevel>(
        *g_archive, hocus::demo_level(demo_index), 0, 1, false,
        active_gameplay_frame_width());
    g_dos_menu_stars.set_random_index(0);
    g_demo_playing = true;
    g_game_last_update_at = GetTickCount64();
    g_game_timer_cadence.reset();
    apply_game_timer(window);
    g_show_level = true;
    play_music_asset(window, g_game->music_asset_index(), true, true);
    update_level(window);
}

void resume_level(HWND window) {
    // Menu stars and option previews consume the same DOS random stream while
    // gameplay is paused; resume from their final cursor rather than forking
    // a second sequence.
    g_game->set_random_index(g_dos_menu_stars.random_index());
    g_show_level = true;
    apply_game_timer(window);
    play_music_asset(window, g_game->music_asset_index(), true,
                     std::exchange(g_level_music_needs_restart, false));
    update_level(window);
}

std::vector<std::string> cheat_menu_lines() {
    std::vector<std::string> lines;
    lines.reserve(cheat_menu_labels.size() + 2);
    lines.emplace_back("Ctrl+Alt+F1 Cheat Menu");
    for (std::size_t index = 0; index < cheat_menu_labels.size(); ++index) {
        auto line = std::string(cheat_menu_labels[index]);
        line += g_cheat_toggles[index] ? " [ON]" : " [OFF]";
        lines.push_back(std::move(line));
    }
    lines.emplace_back("CHAPTER/STAGE SELECT - Warp to any level");
    return lines;
}

hocus::DecodedImage compose_cheat_menu() {
    const auto lines = cheat_menu_lines();
    auto image = hocus::render_dos_menu(
        g_menu_logo, g_bottom, g_menu_cursor_sheet, g_font_mask,
        g_game_palette, lines, true,
        g_cheat_selection, 0,
        "UP/DOWN/1-6 move - ENTER select - ESC close").image;
    if (g_dos_menu_stars.initialized()) {
        g_dos_menu_stars.draw(image);
    }
    return image;
}

void render_cheat_menu(HWND window) {
    g_frame = compose_cheat_menu();
    InvalidateRect(window, nullptr, FALSE);
}

void show_cheat_menu(HWND window) {
    if (!g_game || g_demo_playing) {
        return;
    }
    g_show_level = false;
    g_frontend_screen = FrontendScreen::cheat_menu;
    KillTimer(window, 1);
    if (g_joystick_enabled) {
        schedule_retrace_timer(window);
    }
    SetWindowTextW(window, L"Hocus Native - Cheat Menu");
    render_cheat_menu(window);
}

void close_cheat_menu(HWND window) {
    if (g_game) {
        resume_level(window);
    }
}

std::vector<std::string> cheat_level_select_lines() {
    return {
        "Chapter / Stage Select",
        "CHAPTER: " + std::to_string(g_cheat_episode),
        "STAGE: " + std::to_string(g_cheat_level),
        "WARP TO E" + std::to_string(g_cheat_episode) + "L" +
            std::to_string(g_cheat_level),
    };
}

void render_cheat_level_select(HWND window) {
    const auto lines = cheat_level_select_lines();
    g_frame = hocus::render_dos_menu(
        g_menu_logo, g_bottom, g_menu_cursor_sheet, g_font_mask,
        g_game_palette, lines, true, g_cheat_level_selection, 0,
        "ARROWS change - ENTER select - ESC back").image;
    if (g_dos_menu_stars.initialized()) {
        g_dos_menu_stars.draw(g_frame);
    }
    InvalidateRect(window, nullptr, FALSE);
}

void show_cheat_level_select(HWND window) {
    g_cheat_episode = g_selected_level.episode;
    g_cheat_level = g_selected_level.number;
    g_cheat_level_selection = 0;
    g_frontend_screen = FrontendScreen::cheat_level_select;
    SetWindowTextW(window, L"Hocus Native - Chapter / Stage Select");
    render_cheat_level_select(window);
}

void return_to_cheat_menu(HWND window) {
    g_frontend_screen = FrontendScreen::cheat_menu;
    SetWindowTextW(window, L"Hocus Native - Cheat Menu");
    render_cheat_menu(window);
}

void change_cheat_level_value(HWND window, const int direction) {
    if (g_cheat_level_selection == 0) {
        g_cheat_episode =
            (g_cheat_episode - 1 + direction + 4) % 4 + 1;
    } else if (g_cheat_level_selection == 1) {
        g_cheat_level = (g_cheat_level - 1 + direction + 9) % 9 + 1;
    }
    render_cheat_level_select(window);
}

void warp_to_cheat_level(HWND window) {
    // A cheat warp starts a fresh level but keeps points already earned in
    // the current run, rather than reverting to the entry score cached for
    // Restart Level.
    g_campaign_score = g_game->progress().score;
    g_selected_level = {g_cheat_episode, g_cheat_level};
    load_selected_level();
    resume_level(window);
}

void toggle_selected_cheat(HWND window) {
    if (g_cheat_selection == static_cast<int>(hocus::cheat_code_count)) {
        show_cheat_level_select(window);
        return;
    }
    const auto index = static_cast<std::size_t>(g_cheat_selection);
    g_cheat_toggles[index] = !g_cheat_toggles[index];
    g_game->set_cheat_enabled(cheat_codes[index], g_cheat_toggles[index]);
    render_cheat_menu(window);
}

void toggle_cheat_menu(HWND window) {
    if (!g_show_level &&
        (g_frontend_screen == FrontendScreen::cheat_menu ||
         g_frontend_screen == FrontendScreen::cheat_level_select)) {
        close_cheat_menu(window);
    } else if (g_show_level && g_game && !g_demo_playing) {
        show_cheat_menu(window);
    }
}

void show_game_overlay(HWND window, const FrontendScreen screen,
                       const std::string_view text, const int y) {
    // 0BA5:3751/37FC render directly onto the alternate gameplay page: a
    // palette-index-1 shadow at (+1,+1), then index 0x68 at the exact row.
    g_show_level = false;
    g_frontend_screen = screen;
    const int x = (320 - hocus::font_text_width(g_font_mask, text)) / 2;
    hocus::draw_font_text(g_frame, g_font_mask, text, x + 1, y + 1,
                          g_frame.palette[1]);
    hocus::draw_font_text(g_frame, g_font_mask, text, x, y,
                          g_frame.palette[0x68]);
    KillTimer(window, 1);
    if (g_joystick_enabled) {
        // 0BA5:3751/37FC keep polling the two joystick buttons while the
        // keyboard is idle, so these modal notices need a retrace timer.
        schedule_retrace_timer(window);
    }
    SetWindowTextW(window, screen == FrontendScreen::game_paused
        ? L"Hocus Native - Game Paused" : L"Hocus Native");
    InvalidateRect(window, nullptr, FALSE);
}

void show_quit_confirmation(HWND window, const QuitTarget target,
                            const QuitReturn cancel_return) {
    // 06B8:2EE1 uses the two-line 0E52 screen and accepts only Y, N, or Esc.
    g_quit_target = target;
    g_quit_return = cancel_return;
    g_show_level = false;
    g_frontend_screen = FrontendScreen::quit_confirmation;
    g_frame = hocus::render_dos_confirmation_screen(
        g_font_mask, g_game_palette,
        "Reminder: Save your game before quitting", "Quit game?");
    SetWindowTextW(window, L"Hocus Native - Quit Game?");
    begin_page_fade_in(window, false);
}

void update_pause_screen(HWND window) {
    std::vector<std::string> options = {
        "How to play Hocus Pocus",
        "Abandon level & restart",
        "Save this game",
        "Restore an old game",
        "Change game options",
        "Back to the action!",
        "Quit to Main Menu",
    };
    show_dos_menu(window, FrontendScreen::pause_menu, std::move(options), false,
                  g_pause_selection, L"Hocus Native - Game Paused");
}

bool is_slot_screen(const FrontendScreen screen) {
    return screen == FrontendScreen::save_menu ||
           screen == FrontendScreen::restore_menu ||
           screen == FrontendScreen::save_name;
}

std::array<std::string, hocus::SaveFile::slot_count> slot_names() {
    std::array<std::string, hocus::SaveFile::slot_count> names;
    for (std::size_t index = 0; index < hocus::SaveFile::slot_count; ++index) {
        names[index] = g_save_file->slot(index).name;
    }
    if (g_frontend_screen == FrontendScreen::save_name) {
        names[static_cast<std::size_t>(g_slot_selection)] = g_save_name;
    }
    return names;
}

hocus::DecodedImage compose_active_slot_screen(const int cursor_frame) {
    const bool saving = g_frontend_screen != FrontendScreen::restore_menu;
    const auto names = slot_names();
    auto rendered = hocus::render_dos_slot_screen(
        g_menu_logo, g_bottom, g_menu_cursor_sheet, g_font_mask, g_game_palette,
        saving ? "Select SAVE slot" : "Select RESTORE slot", names,
        g_slot_selection, cursor_frame,
        g_frontend_screen == FrontendScreen::save_name);
    auto image = std::move(rendered.image);
    if (cursor_frame >= 0 && g_dos_menu_stars.initialized()) {
        g_dos_menu_stars.draw(image);
    }
    return image;
}

void render_active_slot_screen(HWND window) {
    g_frame = compose_active_slot_screen((g_menu_cursor_ticks / 5) % 8);
    InvalidateRect(window, nullptr, FALSE);
}

void update_slot_screen(HWND window, const bool saving) {
    g_show_level = false;
    g_frontend_screen = saving ? FrontendScreen::save_menu
                               : FrontendScreen::restore_menu;
    // 06B8:2721 and 2CCF restore two different persistent cursor words
    // (DS:19D8 for Save and DS:19DA for Restore).
    g_slot_selection = saving ? g_save_slot_selection
                              : g_restore_slot_selection;
    g_menu_cursor_ticks = 0;
    g_frame = compose_active_slot_screen(-1);
    SetWindowTextW(window, saving ? L"Hocus Native - Save Game"
                                  : L"Hocus Native - Restore Game");
    begin_page_fade_in(window);
}

void update_save_name_screen(HWND window) {
    g_show_level = false;
    g_frontend_screen = FrontendScreen::save_name;
    SetWindowTextW(window, L"Hocus Native - Name Save Game");
    render_active_slot_screen(window);
}

void apply_game_timer(HWND window) {
    KillTimer(window, 1);
    KillTimer(window, high_fps_timer_id);
    if (g_high_fps_mode) {
        SetTimer(window, high_fps_timer_id,
                 high_fps_interval_milliseconds, nullptr);
    }
    const auto timer_ticks = game_speed_timer_ticks[
        static_cast<std::size_t>(g_game_speed)];
    const auto now = GetTickCount64();
    const auto elapsed = now >= g_game_last_update_at
        ? now - g_game_last_update_at : 0;
    if (g_game_last_update_at != 0 &&
        elapsed * hocus::dos_timer_hz >=
            static_cast<ULONGLONG>(timer_ticks) * 1000) {
        // The DOS counter keeps advancing in modal pause/help/save loops. On
        // return, 0BA5:4D7A therefore falls through immediately, performs one
        // update, and resets its baseline (it never catches up several ticks).
        SetTimer(window, 1, 1, nullptr);
        PostMessageW(window, WM_TIMER, 1, 0);
        return;
    }

    const auto interval = static_cast<UINT>(
        g_game_timer_cadence.next_interval_ms(timer_ticks));
    SetTimer(window, 1, interval, nullptr);
}

void advance_game_timer(HWND window) {
    g_game_last_update_at = GetTickCount64();
    KillTimer(window, 1);
    const auto timer_ticks = game_speed_timer_ticks[
        static_cast<std::size_t>(g_game_speed)];
    const auto interval = static_cast<UINT>(
        g_game_timer_cadence.next_interval_ms(timer_ticks));
    SetTimer(window, 1, interval, nullptr);
}

void render_startup_frame(HWND window) {
    g_frame = hocus::render_vga_fade(g_startup_source,
                                      g_startup_sequence.fade());
    InvalidateRect(window, nullptr, FALSE);
}

void enter_startup_stage(HWND window, const hocus::StartupStage stage) {
    switch (stage) {
    case hocus::StartupStage::piracy_fade_in:
        g_startup_source = g_piracy;
        break;
    case hocus::StartupStage::apogee_fade_in:
        g_startup_source = g_apogee;
        (void)play_music_asset(window, 599, false); // FANFARE.MID
        break;
    case hocus::StartupStage::title_fade_in:
        g_startup_source = g_title;
        (void)play_music_asset(window, 602, true); // TITLE.MID
        break;
    case hocus::StartupStage::title_hold:
        if (g_sound_enabled && !g_startup_sound_playback_wave.empty()) {
            (void)g_sound_mixer.play(
                window, g_startup_sound_playback_wave, 1);
        }
        break;
    case hocus::StartupStage::done:
        g_startup_active = false;
        update_title_screen(window);
        return;
    case hocus::StartupStage::initial_fade_out:
    case hocus::StartupStage::piracy_hold:
    case hocus::StartupStage::piracy_fade_out:
    case hocus::StartupStage::apogee_hold:
    case hocus::StartupStage::apogee_fade_out:
    case hocus::StartupStage::title_fade_out:
        break;
    }
    render_startup_frame(window);
}

void begin_startup(HWND window) {
    // The screen is black after the video/system initialization preceding
    // 0B97:0001. Its first 40-step fade therefore preserves black before the
    // PIRACY.PCX pixels are installed under a zero palette.
    g_startup_sequence = {};
    g_startup_active = true;
    g_frontend_screen = FrontendScreen::startup;
    g_show_level = false;
    g_startup_source = g_title;
    std::fill(g_startup_source.pixels.begin(),
              g_startup_source.pixels.end(), 0);
    std::fill(g_startup_source.palette.begin(),
              g_startup_source.palette.end(), 0);
    g_startup_last_tick = GetTickCount64();
    SetWindowTextW(window, L"Hocus Native - registered v1.1");
    schedule_retrace_timer(window);
    render_startup_frame(window);
}

void update_startup(HWND window) {
    const auto now = GetTickCount64();
    const auto elapsed = now >= g_startup_last_tick
        ? std::min<ULONGLONG>(now - g_startup_last_tick,
                              static_cast<ULONGLONG>(
                                  std::numeric_limits<int>::max()))
        : 0;
    g_startup_last_tick = now;
    const auto previous = g_startup_sequence.stage();
    g_startup_sequence.tick(static_cast<int>(elapsed));
    if (g_startup_sequence.stage() != previous) {
        enter_startup_stage(window, g_startup_sequence.stage());
    } else {
        render_startup_frame(window);
    }
}

void advance_startup(HWND window) {
    if (!g_startup_active) {
        return;
    }
    // Each 1392:0108 call is interruptible. Input advances only the current
    // piracy, Apogee, or title wait; the caller still performs every later
    // fade and screen in 0B97:0001.
    const auto previous = g_startup_sequence.stage();
    g_startup_sequence.press_any_key();
    if (g_startup_sequence.stage() != previous) {
        enter_startup_stage(window, g_startup_sequence.stage());
    }
}

bool load_xinput_api() {
    if (g_xinput_get_state != nullptr) {
        return true;
    }
    constexpr std::array<const wchar_t*, 3> xinput_libraries = {
        L"xinput1_4.dll", L"xinput1_3.dll", L"xinput9_1_0.dll",
    };
    for (const auto* library : xinput_libraries) {
        const auto module = LoadLibraryW(library);
        if (module == nullptr) {
            continue;
        }
        const auto procedure = GetProcAddress(module, "XInputGetState");
        XInputGetStateFunction get_state{};
        static_assert(sizeof(get_state) == sizeof(procedure));
        std::memcpy(&get_state, &procedure, sizeof(get_state));
        if (get_state != nullptr) {
            g_xinput_module = module;
            g_xinput_get_state = get_state;
            return true;
        }
        FreeLibrary(module);
    }
    return false;
}

bool select_xinput_controller(XINPUT_STATE* initial_state = nullptr) {
    if (!load_xinput_api()) {
        return false;
    }
    for (DWORD index = 0; index < XUSER_MAX_COUNT; ++index) {
        XINPUT_STATE state{};
        if (g_xinput_get_state(index, &state) != ERROR_SUCCESS) {
            continue;
        }
        g_xinput_user_index = index;
        g_joystick_backend = JoystickBackend::xinput;
        g_previous_xinput_buttons = state.Gamepad.wButtons;
        if (initial_state != nullptr) {
            *initial_state = state;
        }
        return true;
    }
    return false;
}

hocus::XInputControllerSample xinput_sample(const XINPUT_STATE& state) {
    return {
        state.Gamepad.sThumbLX,
        state.Gamepad.sThumbLY,
        state.Gamepad.bRightTrigger,
        state.Gamepad.wButtons,
    };
}

std::optional<hocus::XInputControllerSample> read_xinput_controller_sample() {
    if (!g_joystick_enabled ||
        g_joystick_backend != JoystickBackend::xinput ||
        g_xinput_get_state == nullptr) {
        return std::nullopt;
    }
    XINPUT_STATE state{};
    if (g_xinput_get_state(g_xinput_user_index, &state) == ERROR_SUCCESS ||
        select_xinput_controller(&state)) {
        return xinput_sample(state);
    }
    g_joystick_enabled = false;
    g_joystick_backend = JoystickBackend::none;
    g_previous_xinput_buttons = 0;
    return std::nullopt;
}

bool set_joystick_enabled(const bool enabled) {
    if (!enabled) {
        g_joystick_enabled = false;
        g_joystick_backend = JoystickBackend::none;
        g_previous_xinput_buttons = 0;
        g_joystick_direction_filter.reset();
        return true;
    }
    if (select_xinput_controller()) {
        g_joystick_enabled = true;
        g_joystick_direction_filter.reset();
        return true;
    }
    JOYINFOEX state{};
    state.dwSize = sizeof(state);
    state.dwFlags = JOY_RETURNALL;
    if (joyGetDevCapsW(JOYSTICKID1, &g_joystick_caps,
                       sizeof(g_joystick_caps)) != JOYERR_NOERROR ||
        joyGetPosEx(JOYSTICKID1, &state) != JOYERR_NOERROR) {
        g_joystick_enabled = false;
        g_joystick_backend = JoystickBackend::none;
        return false;
    }
    g_joystick_enabled = true;
    g_joystick_backend = JoystickBackend::winmm;
    g_joystick_direction_filter.reset();
    return true;
}

std::optional<hocus::DosFrontendJoystickSample>
read_frontend_joystick_sample();

std::optional<hocus::DosFrontendJoystickSample>
read_frontend_joystick_sample() {
    if (!g_joystick_enabled) {
        return std::nullopt;
    }
    if (g_joystick_backend == JoystickBackend::xinput) {
        const auto sample = read_xinput_controller_sample();
        if (!sample) {
            return std::nullopt;
        }
        g_previous_xinput_buttons = sample->buttons;
        return g_joystick_direction_filter.filter(
            hocus::xinput_frontend_joystick_sample(*sample));
    }
    JOYINFOEX state{};
    state.dwSize = sizeof(state);
    state.dwFlags = JOY_RETURNX | JOY_RETURNY | JOY_RETURNBUTTONS;
    if (joyGetPosEx(JOYSTICKID1, &state) != JOYERR_NOERROR) {
        g_joystick_enabled = false;
        g_joystick_backend = JoystickBackend::none;
        return std::nullopt;
    }
    const auto x_range = g_joystick_caps.wXmax - g_joystick_caps.wXmin;
    const auto y_range = g_joystick_caps.wYmax - g_joystick_caps.wYmin;
    const auto x_low = g_joystick_x_low < g_joystick_x_high
        ? static_cast<DWORD>(g_joystick_x_low)
        : g_joystick_caps.wXmin + x_range / 3;
    const auto x_high = g_joystick_x_low < g_joystick_x_high
        ? static_cast<DWORD>(g_joystick_x_high)
        : g_joystick_caps.wXmin + x_range * 2 / 3;
    const auto y_low = g_joystick_y_low < g_joystick_y_high
        ? static_cast<DWORD>(g_joystick_y_low)
        : g_joystick_caps.wYmin + y_range / 3;
    const auto y_high = g_joystick_y_low < g_joystick_y_high
        ? static_cast<DWORD>(g_joystick_y_high)
        : g_joystick_caps.wYmin + y_range * 2 / 3;
    return g_joystick_direction_filter.filter({
        state.dwXpos <= x_low,
        state.dwXpos >= x_high,
        state.dwYpos <= y_low,
        state.dwYpos >= y_high,
        (state.dwButtons & JOY_BUTTON1) != 0,
        (state.dwButtons & JOY_BUTTON2) != 0,
        (state.dwButtons & JOY_BUTTON3) != 0,
        (state.dwButtons & JOY_BUTTON4) != 0,
    });
}

bool frontend_needs_joystick_poll() {
    if (!g_joystick_enabled ||
        g_dos_menu_fade != DosMenuFade::none ||
        g_page_fade != PageFade::none ||
        g_frontend_screen == FrontendScreen::warp) {
        return false;
    }
    if (g_startup_active) {
        const auto stage = g_startup_sequence.stage();
        return stage == hocus::StartupStage::piracy_hold ||
               stage == hocus::StartupStage::apogee_hold ||
               stage == hocus::StartupStage::title_hold;
    }
    if (g_auto_credits) {
        return g_auto_credit_stage == AutoCreditStage::page_hold;
    }
    if (g_show_level) {
        return g_game && g_game->modal_overlay_active();
    }
    if (is_dos_menu_screen(g_frontend_screen)) {
        return true;
    }
    switch (g_frontend_screen) {
    case FrontendScreen::ordering:
    case FrontendScreen::story:
    case FrontendScreen::help:
    case FrontendScreen::high_scores:
    case FrontendScreen::level_results:
    case FrontendScreen::ending:
    case FrontendScreen::finale:
    case FrontendScreen::game_paused:
    case FrontendScreen::game_notice:
    case FrontendScreen::cheat_menu:
    case FrontendScreen::cheat_level_select:
    case FrontendScreen::save_menu:
    case FrontendScreen::restore_menu:
    case FrontendScreen::volume_control:
        return true;
    default:
        return false;
    }
}

void dispatch_frontend_joystick(HWND window) {
    if (!frontend_needs_joystick_poll()) {
        return;
    }
    // 06B8:000C does not sample the hardware while DS:1AF8 is nonzero;
    // callers that inspect its global button words therefore see the last
    // sampled state during those ten suppressed calls.
    int code{};
    if (g_frontend_joystick_repeat.cooldown() == 0) {
        const auto sample = read_frontend_joystick_sample();
        if (!sample) {
            return;
        }
        g_frontend_joystick_latched = *sample;
        code = g_frontend_joystick_repeat.poll(*sample);
    } else {
        code = g_frontend_joystick_repeat.poll(
            g_frontend_joystick_latched);
    }
    const bool any_button = g_frontend_joystick_latched.button_one ||
                            g_frontend_joystick_latched.button_two;

    const bool buttons_are_any_key =
        g_startup_active || g_auto_credits || g_show_level ||
        g_frontend_screen == FrontendScreen::help ||
        g_frontend_screen == FrontendScreen::high_scores ||
        g_frontend_screen == FrontendScreen::level_results ||
        g_frontend_screen == FrontendScreen::finale ||
        g_frontend_screen == FrontendScreen::game_paused ||
        g_frontend_screen == FrontendScreen::game_notice;
    if (buttons_are_any_key) {
        if (any_button) {
            PostMessageW(window, WM_KEYDOWN, VK_RETURN, 0);
        }
        return;
    }

    constexpr std::array<WPARAM, 7> virtual_keys = {
        0, VK_UP, VK_DOWN, VK_RETURN, VK_RIGHT, VK_LEFT, VK_ESCAPE,
    };
    if (code > 0 && code < static_cast<int>(virtual_keys.size())) {
        PostMessageW(window, WM_KEYDOWN,
                     virtual_keys[static_cast<std::size_t>(code)], 0);
    }
}

void update_volume_screen(HWND window) {
    g_show_level = false;
    g_frontend_screen = FrontendScreen::volume_control;
    g_volume_selection = 0;
    g_control_blink_ticks = 0;
    g_volume_preview_ticks = 0;
    g_frame = hocus::render_dos_volume_screen(
        g_volume_bar, g_font_mask, g_game_palette,
        g_sound_volume, g_music_volume, g_volume_selection, true);
    SetWindowTextW(window, L"Hocus Native - Volume Control");
    begin_page_fade_in(window);
}

void update_key_controls_screen(HWND window) {
    g_show_level = false;
    g_frontend_screen = FrontendScreen::key_controls;
    g_frame = hocus::render_dos_key_screen(
        g_font_mask, g_game_palette, g_dos_key_bindings,
        g_capture_key, g_key_selection);
    SetWindowTextW(window, L"Hocus Native - Define Key Controls");
    begin_page_fade_in(window);
}

void update_joystick_calibration_screen(HWND window) {
    g_show_level = false;
    g_frontend_screen = FrontendScreen::joystick_calibration;
    g_joystick_calibration_succeeded = false;
    g_frame = hocus::render_dos_message_screen(
        g_font_mask, g_game_palette,
        "Move joystick to center and press FIRE button", 5, "ESC to exit");
    SetWindowTextW(window, L"Hocus Native - Center Joystick");
    begin_page_fade_in(window);
}

bool is_control_screen(const FrontendScreen screen) {
    return screen == FrontendScreen::volume_control ||
           screen == FrontendScreen::key_controls ||
           screen == FrontendScreen::joystick_calibration;
}

void render_active_control_screen(HWND window, const bool advance_stars) {
    if (g_frontend_screen == FrontendScreen::volume_control) {
        g_page_base = hocus::render_dos_volume_screen(
            g_volume_bar, g_font_mask, g_game_palette,
            g_sound_volume, g_music_volume, g_volume_selection,
            g_control_blink_ticks < 10);
    } else if (g_frontend_screen == FrontendScreen::key_controls) {
        g_page_base = hocus::render_dos_key_screen(
            g_font_mask, g_game_palette, g_dos_key_bindings,
            g_capture_key, g_key_selection);
    } else {
        g_page_base = hocus::render_dos_message_screen(
            g_font_mask, g_game_palette,
            "Move joystick to center and press FIRE button", 5, "ESC to exit");
    }
    g_frame = g_page_base;
    if (g_dos_menu_stars.initialized()) {
        if (advance_stars) {
            g_dos_menu_stars.tick();
        }
        g_dos_menu_stars.draw(g_frame);
    }
    InvalidateRect(window, nullptr, FALSE);
}

void update_options_screen(HWND window) {
    const std::string joystick_status = !g_joystick_enabled
        ? "off"
        : g_joystick_backend == JoystickBackend::xinput
            ? "XInput"
            : "on ";
    std::vector<std::string> options = {
        std::string("Sound is now ") + (g_sound_enabled ? "on " : "off"),
        std::string("Music is now ") + (g_music_enabled ? "on " : "off"),
        "Volume control",
        std::string("Joystick is now ") + joystick_status,
        "Game playing speed",
        "Define key controls",
    };
    show_dos_menu(window, FrontendScreen::options_menu, std::move(options),
                  false, g_options_selection,
                  L"Hocus Native - Game Options");
}

void update_game_speed_screen(HWND window) {
    std::vector<std::string> options = {
        "Select Game Speed", "Slow", "Medium", "Fast",
    };
    show_dos_menu(window, FrontendScreen::game_speed, std::move(options), true,
                  g_game_speed, L"Hocus Native - Select Game Speed");
}

void return_from_slot_screen(HWND window) {
    if (g_slot_menu_from_level) {
        g_slot_menu_from_level = false;
        resume_level(window);
    } else if (g_slot_menu_from_pause) {
        // Pause dispatch targets 06B8:469B/4689 both leave the owning 4600
        // loop after the save/restore UI returns, including a canceled slot
        // screen. The direct F2/F3 wrappers have the same gameplay return.
        g_slot_menu_from_pause = false;
        resume_level(window);
    } else {
        update_title_screen(window);
    }
}

void restore_selected_slot(HWND window) {
    const auto slot = g_save_file->slot(
        static_cast<std::size_t>(g_slot_selection));
    if (!slot.occupied) {
        update_slot_screen(window, false);
        return;
    }
    g_selected_level = slot.level;
    g_skill = slot.skill;
    g_skill_selection = slot.skill;
    g_campaign_score = static_cast<int>(std::min<std::uint32_t>(
        slot.score, static_cast<std::uint32_t>(std::numeric_limits<int>::max())));
    g_slot_menu_from_level = false;
    g_slot_menu_from_pause = false;
    load_selected_level();
    resume_level(window);
}

void commit_save(HWND window) {
    // 06B8:2B41 copies DS:704E/7050, the score snapshot taken on level entry.
    // Live DS:7052/7054 includes points earned in the current attempt and is
    // deliberately not banked by a mid-level save.
    const auto score = static_cast<std::uint32_t>(
        std::max(0, g_campaign_score));
    g_save_file->set_slot(static_cast<std::size_t>(g_slot_selection),
                          g_save_name, g_selected_level, g_skill, score);
    g_save_file->write(g_save_path);
    if (g_slot_menu_from_level) {
        g_slot_menu_from_level = false;
        resume_level(window);
    } else if (g_slot_menu_from_pause) {
        g_slot_menu_from_pause = false;
        resume_level(window);
    } else {
        update_title_screen(window);
    }
}

constexpr int hocus_dat_resource_id = 101;

std::vector<std::uint8_t> load_embedded_hocus_dat(HINSTANCE instance) {
    const auto resource = FindResourceW(
        instance, MAKEINTRESOURCEW(hocus_dat_resource_id), RT_RCDATA);
    if (resource == nullptr) {
        throw std::runtime_error("The embedded HOCUS.DAT resource is missing");
    }
    const auto size = SizeofResource(instance, resource);
    const auto loaded = LoadResource(instance, resource);
    const auto* data = static_cast<const std::uint8_t*>(LockResource(loaded));
    if (size == 0 || loaded == nullptr || data == nullptr) {
        throw std::runtime_error("The embedded HOCUS.DAT resource is invalid");
    }
    return {data, data + size};
}

std::filesystem::path native_module_directory() {
    wchar_t module_path[32768]{};
    const auto length = GetModuleFileNameW(
        nullptr, module_path, static_cast<DWORD>(std::size(module_path)));
    if (length == 0 || length >= std::size(module_path)) {
        throw std::runtime_error(
            "Cannot determine the hocus_native.exe program folder");
    }
    return std::filesystem::path(module_path).parent_path();
}

std::filesystem::path required_hocus_exe_path() {
    return native_module_directory() / L"HOCUS.EXE";
}

std::filesystem::path native_save_path() {
    // Save lookup remains confined to the program folder and never searches
    // parent/source directories.
    return native_module_directory() / L"HOCUS.SAV";
}

std::filesystem::path native_settings_path() {
    return native_module_directory() / L"HOCUS_NATIVE.CFG";
}

void resize_window_for_frame_aspect(HWND window) {
    if (g_fullscreen || IsZoomed(window)) {
        return;
    }
    RECT client{};
    if (!GetClientRect(window, &client)) {
        return;
    }
    int client_height = client.bottom - client.top;
    if (client_height <= 0) {
        return;
    }
    const int logical_width = active_gameplay_frame_width();
    const auto style = static_cast<DWORD>(GetWindowLongPtrW(window, GWL_STYLE));
    const auto ex_style =
        static_cast<DWORD>(GetWindowLongPtrW(window, GWL_EXSTYLE));
    RECT decoration{0, 0, 0, 0};
    if (!AdjustWindowRectEx(&decoration, style, FALSE, ex_style)) {
        return;
    }
    const int decoration_width = decoration.right - decoration.left;
    const int decoration_height = decoration.bottom - decoration.top;
    MONITORINFO monitor_info{};
    monitor_info.cbSize = sizeof(MONITORINFO);
    const auto monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
    if (GetMonitorInfoW(monitor, &monitor_info)) {
        const int maximum_client_width =
            monitor_info.rcWork.right - monitor_info.rcWork.left -
            decoration_width;
        const int maximum_client_height =
            monitor_info.rcWork.bottom - monitor_info.rcWork.top -
            decoration_height;
        client_height = std::min(client_height, maximum_client_height);
        if (MulDiv(client_height, logical_width,
                   hocus::game_frame_height) > maximum_client_width) {
            client_height = MulDiv(maximum_client_width,
                                   hocus::game_frame_height,
                                   logical_width);
        }
    }
    client_height = std::max(1, client_height);
    RECT desired{0, 0,
                 MulDiv(client_height, logical_width,
                        hocus::game_frame_height),
                 client_height};
    if (!AdjustWindowRectEx(&desired, style, FALSE, ex_style)) {
        return;
    }
    SetWindowPos(window, nullptr, 0, 0,
                 desired.right - desired.left,
                 desired.bottom - desired.top,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

bool confirm_quit_application(HWND window) {
    return MessageBoxA(window, "Quit game?", "Hocus Native",
                       MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) == IDYES;
}

void accept_dos_menu(HWND window) {
    switch (g_frontend_screen) {
    case FrontendScreen::main_menu:
        if (g_menu_selection == 0) {
            update_episode_screen(window);
        } else if (g_menu_selection == 1) {
            g_slot_menu_from_pause = false;
            update_slot_screen(window, false);
        } else if (g_menu_selection == 2) {
            g_page_index = 0;
            update_ordering_screen(window);
        } else if (g_menu_selection == 3) {
            g_frontend_screen = FrontendScreen::help;
            g_reference_return_to_pause = false;
            g_page_index = 0;
            update_reference_screen(window);
        } else if (g_menu_selection == 4) {
            g_page_index = 0;
            update_story_screen(window);
        } else if (g_menu_selection == 5) {
            g_options_return_to_pause = false;
            update_options_screen(window);
        } else if (g_menu_selection == 6) {
            g_high_score_episode = 0;
            update_high_scores_screen(window);
        } else if (g_menu_selection == 7) {
            g_high_fps_mode = !g_high_fps_mode;
            hocus::write_native_settings(
                g_native_settings_path,
                {g_high_fps_mode, g_widescreen_mode});
            update_title_screen(window);
        } else if (g_menu_selection == 8) {
            advance_widescreen_mode();
            hocus::write_native_settings(
                g_native_settings_path,
                {g_high_fps_mode, g_widescreen_mode});
            resize_window_for_frame_aspect(window);
            update_title_screen(window);
        } else {
            // The original group-zero selection seven dispatches directly to
            // 0548:08B2. The two native presentation toggles are inserted
            // before it, leaving Quit as the final entry.
            DestroyWindow(window);
        }
        return;
    case FrontendScreen::episode_menu:
        g_selected_level = {g_episode_selection + 1, 1};
        // Group four's default word at DS:1A80 is fixed at one; unlike the
        // other menu groups, 06B8:2203 never persists the skill cursor.
        g_skill_selection = 1;
        update_skill_screen(window);
        return;
    case FrontendScreen::skill_menu:
        g_skill = g_skill_selection;
        g_campaign_score = 0;
        load_selected_level(g_selected_level.episode == 1 &&
                            g_selected_level.number == 1);
        resume_level(window);
        return;
    case FrontendScreen::pause_menu:
        if (g_pause_selection == 0) {
            g_reference_return_to_pause = true;
            g_frontend_screen = FrontendScreen::help;
            g_page_index = 0;
            update_reference_screen(window);
        } else if (g_pause_selection == 1) {
            // 06B8:4683 returns selector one to 0BA5:6757. The level runner
            // restores the entry score at 6A1D and jumps to its load path;
            // no results page is presented.
            load_selected_level();
            resume_level(window);
        } else if (g_pause_selection == 2) {
            g_slot_menu_from_pause = true;
            update_slot_screen(window, true);
        } else if (g_pause_selection == 3) {
            g_slot_menu_from_pause = true;
            update_slot_screen(window, false);
        } else if (g_pause_selection == 4) {
            g_options_return_to_pause = true;
            update_options_screen(window);
        } else if (g_pause_selection == 5) {
            resume_level(window);
        } else {
            show_quit_confirmation(window, QuitTarget::main_menu,
                                   QuitReturn::pause_menu);
        }
        return;
    case FrontendScreen::options_menu:
        if (g_options_selection == 0) {
            g_sound_enabled = !g_sound_enabled;
            update_options_screen(window);
        } else if (g_options_selection == 1) {
            g_music_enabled = !g_music_enabled;
            if (!g_music_enabled) {
                close_music();
            } else if (g_options_return_to_pause) {
                play_music_asset(window, g_game->music_asset_index());
            } else {
                play_music_asset(window, 602);
            }
            update_options_screen(window);
        } else if (g_options_selection == 2) {
            g_volume_selection = 0;
            update_volume_screen(window);
        } else if (g_options_selection == 3) {
            if (g_joystick_enabled) {
                (void)set_joystick_enabled(false);
                update_options_screen(window);
            } else if (!set_joystick_enabled(true)) {
                update_options_screen(window);
            } else if (g_joystick_backend == JoystickBackend::xinput) {
                update_options_screen(window);
            } else {
                update_joystick_calibration_screen(window);
            }
        } else if (g_options_selection == 4) {
            update_game_speed_screen(window);
        } else {
            g_key_selection = 0;
            g_capture_key = false;
            update_key_controls_screen(window);
        }
        return;
    case FrontendScreen::game_speed:
        apply_game_timer(window);
        update_options_screen(window);
        return;
    default:
        return;
    }
}

void cancel_dos_menu(HWND window) {
    switch (g_frontend_screen) {
    case FrontendScreen::main_menu:
        // Menu group zero suppresses Escape at 06B8:2433-2449.
        render_active_dos_menu(window);
        break;
    case FrontendScreen::pause_menu:
        resume_level(window);
        break;
    case FrontendScreen::options_menu:
        write_dos_settings();
        if (g_options_return_to_pause) {
            update_pause_screen(window);
        } else {
            update_title_screen(window);
        }
        break;
    case FrontendScreen::game_speed:
        update_options_screen(window);
        break;
    case FrontendScreen::episode_menu:
    case FrontendScreen::skill_menu:
        // Both wrappers propagate FF to the outer 0548 main-menu loop.
        update_title_screen(window);
        break;
    default:
        update_title_screen(window);
        break;
    }
}

void update_dos_menu_fade(HWND window) {
    if (g_dos_menu_fade == DosMenuFade::fade_in) {
        ++g_dos_menu_fade_level;
        if (g_dos_menu_fade_level >= 20) {
            g_dos_menu_fade_level = 20;
            g_dos_menu_fade = DosMenuFade::none;
            if (g_frontend_screen == FrontendScreen::main_menu) {
                // 06B8:225F captures the attract-mode baseline only after
                // 24C3 has completed the title menu's 20-step fade-in.
                g_title_idle_started = GetTickCount64();
            }
            g_dos_menu_stars.tick();
            render_active_dos_menu(window);
        } else {
            g_frame = hocus::render_vga_fade(
                g_dos_menu_fade_source, {g_dos_menu_fade_level, 20});
            InvalidateRect(window, nullptr, FALSE);
        }
        return;
    }
    if (g_dos_menu_fade != DosMenuFade::fade_out) {
        return;
    }
    --g_dos_menu_fade_level;
    if (g_dos_menu_fade_level > 0) {
        g_frame = hocus::render_vga_fade(
            g_dos_menu_fade_source, {g_dos_menu_fade_level, 20});
        InvalidateRect(window, nullptr, FALSE);
        return;
    }

    g_dos_menu_fade_level = 0;
    g_frame = hocus::render_vga_fade(g_dos_menu_fade_source, {0, 20});
    InvalidateRect(window, nullptr, FALSE);
    const auto exit = std::exchange(g_dos_menu_exit, DosMenuExit::none);
    g_dos_menu_fade = DosMenuFade::none;
    if (exit == DosMenuExit::accept) {
        accept_dos_menu(window);
    } else if (exit == DosMenuExit::cancel) {
        cancel_dos_menu(window);
    }
}

bool is_page_viewer_screen(const FrontendScreen screen) {
    switch (screen) {
    case FrontendScreen::ordering:
    case FrontendScreen::story:
    case FrontendScreen::help:
    case FrontendScreen::level_results:
    case FrontendScreen::ending:
    case FrontendScreen::finale:
        return true;
    default:
        return false;
    }
}

void render_active_page(HWND window, const bool advance_stars) {
    if (is_high_score_screen(g_frontend_screen)) {
        if (advance_stars && g_dos_menu_stars.initialized()) {
            g_dos_menu_stars.tick();
        }
        render_active_high_scores(window);
        return;
    }
    if (is_slot_screen(g_frontend_screen)) {
        if (advance_stars && g_dos_menu_stars.initialized()) {
            g_dos_menu_stars.tick();
        }
        render_active_slot_screen(window);
        return;
    }
    g_frame = g_page_base;
    if (g_page_has_stars && g_dos_menu_stars.initialized()) {
        if (advance_stars) {
            g_dos_menu_stars.tick();
        }
        g_dos_menu_stars.draw(g_frame);
    }
    InvalidateRect(window, nullptr, FALSE);
}

void begin_page_exit(HWND window, const PageExit exit) {
    if (g_page_fade != PageFade::none || exit == PageExit::none) {
        return;
    }
    g_page_exit = exit;
    g_page_fade = PageFade::fade_out;
    g_page_fade_level = 20;
    g_page_fade_source = g_frame;
    schedule_retrace_timer(window);
}

void update_current_text_page(HWND window, const FrontendScreen screen) {
    if (screen == FrontendScreen::ordering) {
        update_ordering_screen(window);
    } else if (screen == FrontendScreen::story) {
        update_story_screen(window);
    } else if (screen == FrontendScreen::ending) {
        update_ending_screen(window);
    }
}

void finish_page_exit(HWND window, const PageExit exit) {
    const auto screen = g_frontend_screen;
    if (screen == FrontendScreen::quit_confirmation) {
        if (exit == PageExit::advance) {
            if (g_quit_target == QuitTarget::application) {
                DestroyWindow(window);
            } else {
                update_title_screen(window);
            }
        } else {
            switch (g_quit_return) {
            case QuitReturn::title:
                update_title_screen(window);
                break;
            case QuitReturn::pause_menu:
                update_pause_screen(window);
                break;
            case QuitReturn::level:
                resume_level(window);
                break;
            }
        }
        return;
    }
    if (screen == FrontendScreen::volume_control) {
        write_dos_settings();
        if (g_control_return_to_level) {
            g_control_return_to_level = false;
            resume_level(window);
        } else {
            update_options_screen(window);
        }
        return;
    }
    if (screen == FrontendScreen::key_controls) {
        if (g_capture_key) {
            g_capture_key = false;
            update_key_controls_screen(window);
        } else if (exit == PageExit::advance) {
            g_capture_key = true;
            update_key_controls_screen(window);
        } else {
            update_options_screen(window);
        }
        return;
    }
    if (screen == FrontendScreen::joystick_calibration) {
        if (!g_joystick_calibration_succeeded) {
            (void)set_joystick_enabled(false);
        }
        if (g_control_return_to_level) {
            g_control_return_to_level = false;
            resume_level(window);
        } else {
            update_options_screen(window);
        }
        return;
    }
    if (screen == FrontendScreen::level_results) {
        finish_level_results(window);
        return;
    }
    if (screen == FrontendScreen::high_scores) {
        if (exit == PageExit::advance && g_high_score_episode < 3) {
            ++g_high_score_episode;
            update_high_scores_screen(window);
        } else {
            update_title_screen(window);
        }
        return;
    }
    if (screen == FrontendScreen::high_score_name) {
        if (exit == PageExit::advance) {
            g_save_file->set_high_score_name(
                static_cast<std::size_t>(g_high_score_episode),
                static_cast<std::size_t>(g_high_score_edit_rank),
                g_high_score_name);
            g_save_file->write(g_save_path);
        }
        update_title_screen(window);
        return;
    }
    if (screen == FrontendScreen::restore_menu) {
        if (exit == PageExit::advance) {
            restore_selected_slot(window);
        } else {
            return_from_slot_screen(window);
        }
        return;
    }
    if (screen == FrontendScreen::save_menu) {
        return_from_slot_screen(window);
        return;
    }
    if (screen == FrontendScreen::save_name) {
        if (exit == PageExit::advance) {
            commit_save(window);
        } else {
            g_save_name = g_save_original_name;
            g_frontend_screen = FrontendScreen::save_menu;
            render_active_slot_screen(window);
        }
        return;
    }
    if (exit == PageExit::previous) {
        --g_page_index;
        update_current_text_page(window, screen);
        return;
    }
    if (exit == PageExit::next) {
        ++g_page_index;
        update_current_text_page(window, screen);
        return;
    }
    if (exit == PageExit::first) {
        g_page_index = 0;
        update_current_text_page(window, screen);
        return;
    }
    if (exit == PageExit::last) {
        if (screen == FrontendScreen::ordering) {
            g_page_index = 8;
        } else if (screen == FrontendScreen::story) {
            g_page_index = 9;
        } else if (screen == FrontendScreen::ending) {
            g_page_index = ending_page_counts[
                static_cast<std::size_t>(g_ending_episode)] - 1;
        }
        update_current_text_page(window, screen);
        return;
    }
    if (exit == PageExit::advance && screen == FrontendScreen::help) {
        if (++g_page_index < help_page_count) {
            update_reference_screen(window);
        } else if (g_reference_return_to_pause) {
            update_pause_screen(window);
        } else if (g_reference_return_to_level) {
            g_reference_return_to_level = false;
            resume_level(window);
        } else {
            update_title_screen(window);
        }
        return;
    }
    if (screen == FrontendScreen::ending) {
        // 0548 continues the episode-complete path after 3F7D returns FF.
        // Episode four always proceeds to KISS.PCX; the other three proceed
        // directly to high-score handling.
        if (g_ending_episode == 3) {
            update_finale_screen(window);
        } else {
            finish_episode(window);
        }
        return;
    }
    if (screen == FrontendScreen::finale) {
        finish_episode(window);
        return;
    }
    if (screen == FrontendScreen::help && g_reference_return_to_pause) {
        update_pause_screen(window);
    } else if (screen == FrontendScreen::help &&
               g_reference_return_to_level) {
        g_reference_return_to_level = false;
        resume_level(window);
    } else {
        update_title_screen(window);
    }
}

void update_page_fade(HWND window) {
    if (g_page_fade == PageFade::fade_in) {
        ++g_page_fade_level;
        if (g_page_fade_level >= 20) {
            g_page_fade_level = 20;
            g_page_fade = PageFade::none;
            render_active_page(window, true);
        } else {
            g_frame = hocus::render_vga_fade(
                g_page_fade_source, {g_page_fade_level, 20});
            InvalidateRect(window, nullptr, FALSE);
        }
        return;
    }
    if (g_page_fade != PageFade::fade_out) {
        return;
    }

    --g_page_fade_level;
    if (g_page_fade_level > 0) {
        g_frame = hocus::render_vga_fade(
            g_page_fade_source, {g_page_fade_level, 20});
        InvalidateRect(window, nullptr, FALSE);
        return;
    }

    g_page_fade_level = 0;
    g_frame = hocus::render_vga_fade(g_page_fade_source, {0, 20});
    InvalidateRect(window, nullptr, FALSE);
    const auto exit = std::exchange(g_page_exit, PageExit::none);
    g_page_fade = PageFade::none;
    finish_page_exit(window, exit);
}

void process_game_cheat_scan(const bool released, const LPARAM lparam) {
    if (!g_show_level || !g_game) {
        return;
    }

    // The original INT 09h handler does not compare text.  It resets its
    // 16-bit accumulator on the make code for F, Q, or B, then adds every make
    // and break scan code.  These four totals uniquely encode FEELGOOD, BLAKE,
    // QUARK, and BANANA on the DOS set-1 keyboard.
    const auto make = static_cast<std::uint8_t>((lparam >> 16) & 0xFF);
    if (!released && (make == 0x21 || make == 0x10 || make == 0x30)) {
        g_cheat_scan_sum = 0;
    }
    const auto scan = static_cast<std::uint16_t>(
        released ? (make | 0x80U) : make);
    g_cheat_scan_sum = static_cast<std::uint16_t>(g_cheat_scan_sum + scan);

    if (g_cheat_scan_sum == 0x05BA) {
        // 1392:0486 is the only success path that explicitly clears the sum.
        g_cheat_scan_sum = 0;
        g_game->apply_cheat(hocus::CheatCode::full_health);
    } else if (g_cheat_scan_sum == 0x03D6) {
        g_game->apply_cheat(hocus::CheatCode::both_keys);
    } else if (g_cheat_scan_sum == 0x0378) {
        g_game->apply_cheat(hocus::CheatCode::rapid_fire);
    } else if (g_cheat_scan_sum == 0x04D8) {
        g_game->apply_cheat(hocus::CheatCode::laser_shots);
    }
}

bool is_cheat_menu_shortcut(const WPARAM wparam,
                            const LPARAM lparam) noexcept {
    return wparam == VK_F1 && (lparam & (1L << 30)) == 0 &&
           (GetKeyState(VK_CONTROL) & 0x8000) != 0 &&
           (GetKeyState(VK_MENU) & 0x8000) != 0;
}

bool is_fullscreen_shortcut(const WPARAM wparam,
                            const LPARAM lparam) noexcept {
    return wparam == VK_RETURN && (lparam & (1L << 29)) != 0;
}

void toggle_fullscreen(HWND window) {
    if (!g_fullscreen) {
        g_windowed_style = static_cast<DWORD>(
            GetWindowLongPtrW(window, GWL_STYLE));
        g_windowed_placement.length = sizeof(WINDOWPLACEMENT);
        if (!GetWindowPlacement(window, &g_windowed_placement)) {
            throw std::runtime_error("Could not save the window placement");
        }
        MONITORINFO monitor{};
        monitor.cbSize = sizeof(MONITORINFO);
        if (!GetMonitorInfoW(
                MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST),
                &monitor)) {
            throw std::runtime_error("Could not determine the fullscreen monitor");
        }
        SetWindowLongPtrW(
            window, GWL_STYLE,
            static_cast<LONG_PTR>(g_windowed_style & ~WS_OVERLAPPEDWINDOW));
        if (!SetWindowPos(
                window, HWND_TOP, monitor.rcMonitor.left, monitor.rcMonitor.top,
                monitor.rcMonitor.right - monitor.rcMonitor.left,
                monitor.rcMonitor.bottom - monitor.rcMonitor.top,
                SWP_NOOWNERZORDER | SWP_FRAMECHANGED)) {
            throw std::runtime_error("Could not enter fullscreen mode");
        }
        g_fullscreen = true;
    } else {
        SetWindowLongPtrW(window, GWL_STYLE,
                          static_cast<LONG_PTR>(g_windowed_style));
        g_windowed_placement.length = sizeof(WINDOWPLACEMENT);
        if (!SetWindowPlacement(window, &g_windowed_placement) ||
            !SetWindowPos(window, nullptr, 0, 0, 0, 0,
                          SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                              SWP_NOOWNERZORDER | SWP_FRAMECHANGED)) {
            throw std::runtime_error("Could not restore windowed mode");
        }
        g_fullscreen = false;
    }
    InvalidateRect(window, nullptr, FALSE);
}

LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam,
                             LPARAM lparam) {
    switch (message) {
    case WM_ERASEBKGND:
        return 1;
    case WM_KEYUP:
        process_game_cheat_scan(true, lparam);
        break;
    case WM_SYSKEYDOWN:
        if (is_fullscreen_shortcut(wparam, lparam)) {
            if ((lparam & (1L << 30)) == 0) {
                try {
                    toggle_fullscreen(window);
                } catch (const std::exception& error) {
                    MessageBoxA(window, error.what(),
                                "Hocus Native - fullscreen error",
                                MB_OK | MB_ICONERROR);
                }
            }
            return 0;
        }
        if (is_cheat_menu_shortcut(wparam, lparam)) {
            try {
                toggle_cheat_menu(window);
            } catch (const std::exception& error) {
                MessageBoxA(window, error.what(),
                            "Hocus Native - cheat menu error",
                            MB_OK | MB_ICONERROR);
            }
            return 0;
        }
        break;
    case WM_SYSCHAR:
        // Suppress the system-menu mnemonic/beep while retaining explicit
        // Alt+Enter, Alt+F4, and Ctrl+Alt+F1 handling.
        return 0;
    case WM_SYSCOMMAND:
        if ((wparam & 0xFFF0U) == SC_KEYMENU) {
            // Blocks bare Alt/F10 and Alt+Space from opening Windows' Move,
            // Size, Minimize, Maximize, Close system menu.
            return 0;
        }
        break;
    case WM_KEYDOWN:
        try {
            if (is_cheat_menu_shortcut(wparam, lparam)) {
                toggle_cheat_menu(window);
                return 0;
            }
            process_game_cheat_scan(false, lparam);
            const bool first_press = (lparam & (1L << 30)) == 0;
            if (!first_press &&
                (g_show_level ||
                 (!is_dos_menu_screen(g_frontend_screen) &&
                  !is_slot_screen(g_frontend_screen) &&
                  !is_high_score_screen(g_frontend_screen) &&
                  !is_control_screen(g_frontend_screen)))) {
                break;
            }
            if (g_startup_active) {
                advance_startup(window);
                return 0;
            }
            if (g_auto_credits) {
                // 06B8:42CB uses any key to end the current timed page;
                // the second credits page is still shown before returning.
                begin_auto_credit_page_exit();
                return 0;
            }
            if (g_demo_playing) {
                update_title_screen(window);
                return 0;
            }
            if (!g_show_level &&
                g_frontend_screen == FrontendScreen::cheat_menu) {
                if (wparam == VK_ESCAPE) {
                    close_cheat_menu(window);
                } else if (wparam == VK_UP || wparam == VK_DOWN) {
                    constexpr int item_count =
                        static_cast<int>(hocus::cheat_code_count) + 1;
                    g_cheat_selection =
                        (g_cheat_selection +
                         (wparam == VK_UP ? -1 : 1) + item_count) %
                        item_count;
                    render_cheat_menu(window);
                } else if (wparam == VK_RETURN || wparam == VK_SPACE) {
                    toggle_selected_cheat(window);
                } else if (wparam >= '1' &&
                           wparam < '1' + hocus::cheat_code_count + 1) {
                    g_cheat_selection = static_cast<int>(wparam - '1');
                    toggle_selected_cheat(window);
                } else if (wparam >= VK_NUMPAD1 &&
                           wparam < VK_NUMPAD1 + hocus::cheat_code_count + 1) {
                    g_cheat_selection =
                        static_cast<int>(wparam - VK_NUMPAD1);
                    toggle_selected_cheat(window);
                }
                return 0;
            }
            if (!g_show_level &&
                g_frontend_screen == FrontendScreen::cheat_level_select) {
                if (wparam == VK_ESCAPE) {
                    return_to_cheat_menu(window);
                } else if (wparam == VK_UP || wparam == VK_DOWN) {
                    constexpr int item_count = 3;
                    g_cheat_level_selection =
                        (g_cheat_level_selection +
                         (wparam == VK_UP ? -1 : 1) + item_count) %
                        item_count;
                    render_cheat_level_select(window);
                } else if (wparam == VK_LEFT || wparam == VK_RIGHT) {
                    if (g_cheat_level_selection < 2) {
                        change_cheat_level_value(
                            window, wparam == VK_LEFT ? -1 : 1);
                    }
                } else if (wparam == VK_RETURN || wparam == VK_SPACE) {
                    if (g_cheat_level_selection == 2) {
                        warp_to_cheat_level(window);
                    } else {
                        change_cheat_level_value(window, 1);
                    }
                }
                return 0;
            }
            if (!g_show_level &&
                (g_frontend_screen == FrontendScreen::game_paused ||
                 g_frontend_screen == FrontendScreen::game_notice)) {
                resume_level(window);
                return 0;
            }
            if (g_show_level && g_game->modal_overlay_active()) {
                // 0BA5:542E and 38B6 accept any keyboard key, not merely
                // Hocus's configurable Up/action binding.
                g_game->dismiss_modal_overlay();
                apply_game_timer(window);
                update_level(window);
                return 0;
            }
            if (!g_show_level && is_control_screen(g_frontend_screen)) {
                if (g_page_fade != PageFade::none) {
                    return 0;
                }
                if (g_frontend_screen == FrontendScreen::volume_control) {
                    if (wparam == VK_ESCAPE || wparam == VK_RETURN) {
                        begin_page_exit(window, PageExit::cancel);
                    } else if (wparam == VK_UP || wparam == VK_DOWN) {
                        g_volume_selection = wparam == VK_UP ? 0 : 1;
                        render_active_control_screen(window, false);
                    } else if (wparam == VK_LEFT || wparam == VK_RIGHT) {
                        int& volume = g_volume_selection == 0
                            ? g_sound_volume : g_music_volume;
                        volume = std::clamp(
                            volume + (wparam == VK_LEFT ? -1 : 1), 0, 15);
                        if (g_volume_selection == 0) {
                            rebuild_sound_playback_waves();
                        } else {
                            apply_music_volume();
                        }
                        render_active_control_screen(window, false);
                    }
                    return 0;
                }

                if (g_frontend_screen ==
                    FrontendScreen::joystick_calibration) {
                    if (wparam == VK_ESCAPE) {
                        begin_page_exit(window, PageExit::cancel);
                    }
                    return 0;
                }

                if (wparam == VK_ESCAPE) {
                    begin_page_exit(window, PageExit::cancel);
                } else if (g_capture_key && wparam >= 'A' && wparam <= 'R') {
                    const auto binding = static_cast<std::uint8_t>(wparam - 'A');
                    const auto action = static_cast<std::size_t>(g_key_selection);
                    g_dos_key_bindings[action] = binding;
                    g_key_bindings[action] = dos_key_virtual_keys[binding];
                    begin_page_exit(window, PageExit::advance);
                } else if (!g_capture_key &&
                           wparam >= 'A' && wparam <= 'H') {
                    g_key_selection = static_cast<int>(wparam - 'A');
                    begin_page_exit(window, PageExit::advance);
                }
                return 0;
            }
            if (!g_show_level && is_slot_screen(g_frontend_screen)) {
                if (g_page_fade != PageFade::none) {
                    return 0;
                }
                if (g_frontend_screen == FrontendScreen::save_name) {
                    if (wparam == VK_ESCAPE) {
                        g_save_name = g_save_original_name;
                        g_frontend_screen = FrontendScreen::save_menu;
                        render_active_slot_screen(window);
                    } else if (wparam == VK_BACK && !g_save_name.empty()) {
                        g_save_name.pop_back();
                        render_active_slot_screen(window);
                    } else if (wparam == VK_RETURN) {
                        begin_page_exit(window, PageExit::advance);
                    }
                    return 0;
                }

                if (wparam == VK_ESCAPE) {
                    begin_page_exit(window, PageExit::cancel);
                    return 0;
                }
                if (wparam == VK_UP || wparam == VK_DOWN) {
                    g_slot_selection =
                        (g_slot_selection + (wparam == VK_UP ? -1 : 1) + 9) % 9;
                    if (g_frontend_screen == FrontendScreen::restore_menu) {
                        g_restore_slot_selection = g_slot_selection;
                    } else {
                        g_save_slot_selection = g_slot_selection;
                    }
                    render_active_slot_screen(window);
                    return 0;
                }
                if (wparam >= '1' && wparam <= '9') {
                    g_slot_selection = static_cast<int>(wparam - '1');
                    if (g_frontend_screen == FrontendScreen::restore_menu) {
                        g_restore_slot_selection = g_slot_selection;
                    } else {
                        g_save_slot_selection = g_slot_selection;
                    }
                    render_active_slot_screen(window);
                    return 0;
                }
                if (wparam == VK_RETURN) {
                    const auto slot = g_save_file->slot(
                        static_cast<std::size_t>(g_slot_selection));
                    if (g_frontend_screen == FrontendScreen::restore_menu) {
                        if (slot.occupied) {
                            begin_page_exit(window, PageExit::advance);
                        }
                    } else {
                        g_save_original_name = slot.name;
                        // 25CC replaces an empty slot's visible "<empty>"
                        // marker with DS:20E3's empty string before editing.
                        g_save_name = slot.occupied ? slot.name : "";
                        update_save_name_screen(window);
                    }
                    return 0;
                }
                return 0;
            }
            if (!g_show_level &&
                is_high_score_screen(g_frontend_screen)) {
                if (g_page_fade != PageFade::none) {
                    return 0;
                }
                if (g_frontend_screen == FrontendScreen::high_scores) {
                    begin_page_exit(window, wparam == VK_ESCAPE
                        ? PageExit::cancel : PageExit::advance);
                } else if (wparam == VK_BACK &&
                           !g_high_score_name.empty()) {
                    g_high_score_name.pop_back();
                    render_active_high_scores(window);
                } else if (wparam == VK_RETURN) {
                    g_save_file->set_high_score_name(
                        static_cast<std::size_t>(g_high_score_episode),
                        static_cast<std::size_t>(g_high_score_edit_rank),
                        g_high_score_name);
                    g_save_file->write(g_save_path);
                    // 1C9B returns directly after the write; the main menu is
                    // freshly composed under its own fade-in.
                    update_title_screen(window);
                }
                // 1C9B does not give Escape a cancel role while editing.
                return 0;
            }
            if (!g_show_level &&
                g_frontend_screen == FrontendScreen::quit_confirmation) {
                if (g_page_fade != PageFade::none) {
                    return 0;
                }
                // 06B8:2F24 accepts only Y, N, and Escape.  All other scan
                // codes leave the confirmation page untouched.
                if (wparam == 'Y') {
                    begin_page_exit(window, PageExit::advance);
                } else if (wparam == 'N' || wparam == VK_ESCAPE) {
                    begin_page_exit(window, PageExit::cancel);
                }
                return 0;
            }
            if (!g_show_level &&
                g_frontend_screen == FrontendScreen::level_results) {
                if (g_page_fade == PageFade::none) {
                    begin_page_exit(window, PageExit::advance);
                }
                return 0;
            }
            if (!g_show_level &&
                is_page_viewer_screen(g_frontend_screen)) {
                if (g_page_fade != PageFade::none) {
                    return 0;
                }
                if (g_frontend_screen == FrontendScreen::help) {
                    begin_page_exit(window, wparam == VK_ESCAPE
                        ? PageExit::cancel : PageExit::advance);
                    return 0;
                }
                if (g_frontend_screen == FrontendScreen::finale) {
                    begin_page_exit(window, PageExit::cancel);
                    return 0;
                }
                if (wparam == VK_ESCAPE) {
                    begin_page_exit(window, PageExit::cancel);
                    return 0;
                }

                const int page_count =
                    g_frontend_screen == FrontendScreen::ordering ? 9 :
                    g_frontend_screen == FrontendScreen::story ? 10 :
                    ending_page_counts[
                        static_cast<std::size_t>(g_ending_episode)];
                if ((wparam == VK_PRIOR || wparam == VK_LEFT ||
                     wparam == VK_UP) && g_page_index > 0) {
                    begin_page_exit(window, PageExit::previous);
                } else if ((wparam == VK_NEXT || wparam == VK_RIGHT ||
                            wparam == VK_DOWN) &&
                           g_page_index + 1 < page_count) {
                    begin_page_exit(window, PageExit::next);
                } else if (wparam == VK_HOME && g_page_index > 0) {
                    begin_page_exit(window, PageExit::first);
                } else if (wparam == VK_END &&
                           g_page_index + 1 < page_count) {
                    begin_page_exit(window, PageExit::last);
                } else {
                    // 06B8:3F7D ignores Return, Space, and all other keys on
                    // multi-page viewers, including PGUP/PGDN at a boundary.
                    render_active_page(window, true);
                }
                return 0;
            }
            if (!g_show_level && is_dos_menu_screen(g_frontend_screen)) {
                if (g_dos_menu_fade != DosMenuFade::none) {
                    return 0;
                }
                if (g_frontend_screen == FrontendScreen::main_menu) {
                    // Both the BIOS-key and joystick paths refresh the
                    // baseline at 22F6/240C, including otherwise ignored
                    // keys such as Escape in menu group zero.
                    g_title_idle_started = GetTickCount64();
                }
                if (wparam == VK_ESCAPE) {
                    if (g_frontend_screen != FrontendScreen::main_menu) {
                        begin_dos_menu_exit(window, DosMenuExit::cancel);
                    }
                    return 0;
                }
                if (navigate_dos_menu(window, wparam)) {
                    return 0;
                }
                if (wparam == VK_RETURN) {
                    begin_dos_menu_exit(window, DosMenuExit::accept);
                }
                // 06B8:2259 recognizes only Up, Down, Enter, Escape, and the
                // first letter of a selectable line.
                return 0;
            }
            if (g_show_level) {
                // Registered-v1.1 ISR 1392:04E8-05D8 maps these literal scan
                // codes to immediate in-level commands.
                if (wparam == 'P') {
                    show_game_overlay(window, FrontendScreen::game_paused,
                                      "Game Paused", 76);
                    return 0;
                }
                if (wparam == VK_F1) {
                    g_reference_return_to_pause = false;
                    g_reference_return_to_level = true;
                    g_frontend_screen = FrontendScreen::help;
                    g_page_index = 0;
                    update_reference_screen(window);
                    return 0;
                }
                if (wparam == VK_F2 || wparam == VK_F3) {
                    g_slot_menu_from_pause = false;
                    g_slot_menu_from_level = true;
                    update_slot_screen(window, wparam == VK_F2);
                    return 0;
                }
                if (wparam == VK_F10) {
                    show_quit_confirmation(window, QuitTarget::main_menu,
                                           QuitReturn::level);
                    return 0;
                }
                if (wparam == 'C' && g_joystick_enabled) {
                    if (g_joystick_backend == JoystickBackend::xinput) {
                        show_game_overlay(
                            window, FrontendScreen::game_notice,
                            "XInput controller needs no calibration", 80);
                    } else {
                        g_control_return_to_level = true;
                        update_joystick_calibration_screen(window);
                    }
                    return 0;
                }
                if (wparam == 'M') {
                    g_music_enabled = !g_music_enabled;
                    if (g_music_enabled) {
                        play_music_asset(window, g_game->music_asset_index());
                    } else {
                        close_music();
                    }
                    write_dos_settings();
                    show_game_overlay(
                        window, FrontendScreen::game_notice,
                        g_music_enabled ? "Music is now on"
                                        : "Music is now off", 80);
                    return 0;
                }
                if (wparam == 'S') {
                    g_sound_enabled = !g_sound_enabled;
                    write_dos_settings();
                    show_game_overlay(
                        window, FrontendScreen::game_notice,
                        g_sound_enabled ? "Sound effects are now on"
                                        : "Sound effects are now off", 80);
                    return 0;
                }
                if (wparam == 'V') {
                    g_control_return_to_level = true;
                    g_volume_selection = 0;
                    update_volume_screen(window);
                    return 0;
                }
            }
            if (g_show_level && wparam == VK_ESCAPE) {
                update_pause_screen(window);
                return 0;
            }

            // Every DOS front-end state has already been handled above.  In
            // particular, WARP and timed startup/credit pages ignore keyboard
            // input instead of falling into the obsolete native menu path.
            if (!g_show_level) {
                return 0;
            }


            if (wparam == VK_RETURN) {
                if (g_game->level_complete()) {
                    begin_level_results(window);
                }
                return 0;
            }
        } catch (const std::exception& error) {
            MessageBoxA(window, error.what(), "Hocus Native - render error",
                        MB_OK | MB_ICONERROR);
        }
        break;
    case WM_CHAR:
        if (!g_show_level && g_frontend_screen == FrontendScreen::save_name &&
            wparam >= 32 && wparam <= 122 &&
            g_save_name.size() < hocus::SaveFile::slot_name_capacity) {
            g_save_name.push_back(static_cast<char>(wparam));
            update_save_name_screen(window);
            return 0;
        }
        if (!g_show_level &&
            g_frontend_screen == FrontendScreen::high_score_name &&
            wparam >= 32 && wparam <= 122 &&
            g_high_score_name.size() <
                hocus::SaveFile::high_score_name_capacity) {
            g_high_score_name.push_back(static_cast<char>(wparam));
            render_active_high_scores(window);
            return 0;
        }
        break;
    case WM_TIMER:
        if (wparam == high_fps_timer_id) {
            if (!g_high_fps_mode || !g_show_level || !g_game ||
                g_game->modal_overlay_active()) {
                KillTimer(window, high_fps_timer_id);
            } else {
                try {
                    update_level(window);
                } catch (const std::exception& error) {
                    KillTimer(window, high_fps_timer_id);
                    MessageBoxA(window, error.what(),
                                "Hocus Native - high-FPS render error",
                                MB_OK | MB_ICONERROR);
                }
            }
            return 0;
        }
        if (wparam == 1 && !g_show_level && !g_demo_playing) {
            const bool uses_retrace_timer =
                g_startup_active ||
                g_dos_menu_fade != DosMenuFade::none ||
                g_page_fade != PageFade::none || g_auto_credits ||
                g_frontend_screen == FrontendScreen::warp ||
                is_high_score_screen(g_frontend_screen) ||
                is_slot_screen(g_frontend_screen) ||
                is_control_screen(g_frontend_screen) ||
                (is_page_viewer_screen(g_frontend_screen) &&
                 g_page_has_stars) ||
                is_dos_menu_screen(g_frontend_screen) ||
                frontend_needs_joystick_poll();
            if (uses_retrace_timer) {
                schedule_retrace_timer(window);
            } else {
                KillTimer(window, 1);
            }
        }
        if (wparam == 1) {
            dispatch_frontend_joystick(window);
        }
        if (wparam == 1 && g_startup_active) {
            update_startup(window);
        } else if (wparam == 1 &&
                   g_dos_menu_fade != DosMenuFade::none) {
            try {
                update_dos_menu_fade(window);
            } catch (const std::exception& error) {
                MessageBoxA(window, error.what(),
                            "Hocus Native - menu transition error",
                            MB_OK | MB_ICONERROR);
                g_dos_menu_fade = DosMenuFade::none;
                update_title_screen(window);
            }
        } else if (wparam == 1 && g_page_fade != PageFade::none) {
            try {
                update_page_fade(window);
            } catch (const std::exception& error) {
                MessageBoxA(window, error.what(),
                            "Hocus Native - page transition error",
                            MB_OK | MB_ICONERROR);
                g_page_fade = PageFade::none;
                update_title_screen(window);
            }
        } else if (wparam == 1 && g_auto_credits) {
            try {
                update_auto_credits(window);
            } catch (const std::exception& error) {
                MessageBoxA(window, error.what(),
                            "Hocus Native - credits error",
                            MB_OK | MB_ICONERROR);
                return_from_auto_credits(window);
            }
        } else if (wparam == 1 &&
                   g_frontend_screen == FrontendScreen::warp) {
            try {
                update_warp_transition(window);
            } catch (const std::exception& error) {
                MessageBoxA(window, error.what(),
                            "Hocus Native - warp transition error",
                            MB_OK | MB_ICONERROR);
                g_page_index = 0;
                update_ending_screen(window);
            }
        } else if (wparam == 1 && g_show_level &&
                   g_game->modal_overlay_active()) {
            // 0BA5:542E/38B6 block the level loop while the shared front-end
            // poll accepts either physical joystick button. The dispatcher
            // posts that as a key; no core counter advances meanwhile.
            schedule_retrace_timer(window);
        } else if (wparam == 1 && g_demo_playing) {
            try {
                if (g_demo_frame_index >= g_demo_frames.size()) {
                    update_title_screen(window);
                    return 0;
                }
                advance_game_timer(window);
                g_game->tick(g_demo_frames[g_demo_frame_index++]);
                g_dos_menu_stars.set_random_index(g_game->random_index());
                play_pending_sounds(window);
                if (g_game->modal_overlay_active()) {
                    schedule_retrace_timer(window);
                    update_level(window);
                } else if (g_demo_frame_index >= g_demo_frames.size() ||
                    g_game->player_dead() || g_game->level_complete()) {
                    update_title_screen(window);
                } else {
                    update_level(window);
                }
            } catch (const std::exception& error) {
                MessageBoxA(window, error.what(), "Hocus Native - demo error",
                            MB_OK | MB_ICONERROR);
                update_title_screen(window);
            }
        } else if (wparam == 1 && g_show_level) {
            try {
                advance_game_timer(window);
                hocus::InputState keyboard;
                keyboard.left =
                    (GetAsyncKeyState(g_key_bindings[0]) & 0x8000) != 0;
                keyboard.right =
                    (GetAsyncKeyState(g_key_bindings[1]) & 0x8000) != 0;
                keyboard.jump =
                    (GetAsyncKeyState(g_key_bindings[2]) & 0x8000) != 0;
                keyboard.fire =
                    (GetAsyncKeyState(g_key_bindings[3]) & 0x8000) != 0;
                keyboard.action =
                    (GetAsyncKeyState(g_key_bindings[4]) & 0x8000) != 0;
                keyboard.down =
                    (GetAsyncKeyState(g_key_bindings[5]) & 0x8000) != 0;
                keyboard.scroll_up =
                    (GetAsyncKeyState(g_key_bindings[6]) & 0x8000) != 0;
                keyboard.scroll_down =
                    (GetAsyncKeyState(g_key_bindings[7]) & 0x8000) != 0;
                const bool joystick_was_enabled = g_joystick_enabled;
                hocus::InputState input;
                if (g_joystick_backend == JoystickBackend::xinput) {
                    const auto controller = read_xinput_controller_sample();
                    const auto current_buttons = controller
                        ? controller->buttons : 0;
                    const bool pause_pressed = hocus::xinput_pause_pressed(
                        current_buttons, g_previous_xinput_buttons);
                    g_previous_xinput_buttons = current_buttons;
                    if (pause_pressed) {
                        PostMessageW(window, WM_KEYDOWN, VK_ESCAPE, 0);
                        return 0;
                    }
                    input = hocus::xinput_gameplay_input(
                        keyboard,
                        controller.value_or(hocus::XInputControllerSample{}),
                        joystick_was_enabled);
                } else {
                    const auto joystick = read_frontend_joystick_sample();
                    input = hocus::dos_gameplay_input(
                        keyboard, joystick.value_or(
                            hocus::DosFrontendJoystickSample{}),
                        joystick_was_enabled, g_joystick_fire_button);
                }
                g_game->tick(input);
                g_dos_menu_stars.set_random_index(g_game->random_index());
                play_pending_sounds(window);
                if (g_game->modal_overlay_active()) {
                    schedule_retrace_timer(window);
                    update_level(window);
                } else if (g_game->player_dead()) {
                    begin_failed_level_results(window);
                } else if (g_game->level_complete()) {
                    // 0BA5:69C5 starts TITLE.MID (asset 602) directly before
                    // 06B8:4889. update_level_results_screen owns that exact
                    // transition; FANFARE.MID is startup-only.
                    begin_level_results(window);
                } else {
                    update_level(window);
                }
            } catch (const std::exception& error) {
                MessageBoxA(window, error.what(), "Hocus Native - game tick error",
                            MB_OK | MB_ICONERROR);
                update_title_screen(window);
            }
        } else if (wparam == 1 && !g_show_level &&
                   is_high_score_screen(g_frontend_screen)) {
            try {
                g_dos_menu_stars.tick();
                render_active_high_scores(window);
            } catch (const std::exception& error) {
                MessageBoxA(window, error.what(),
                            "Hocus Native - high-score animation error",
                            MB_OK | MB_ICONERROR);
                update_title_screen(window);
            }
        } else if (wparam == 1 && !g_show_level &&
                   is_slot_screen(g_frontend_screen)) {
            try {
                g_menu_cursor_ticks = (g_menu_cursor_ticks + 1) % 40;
                g_dos_menu_stars.tick();
                render_active_slot_screen(window);
            } catch (const std::exception& error) {
                MessageBoxA(window, error.what(),
                            "Hocus Native - slot animation error",
                            MB_OK | MB_ICONERROR);
                return_from_slot_screen(window);
            }
        } else if (wparam == 1 && !g_show_level &&
                   is_control_screen(g_frontend_screen)) {
            try {
                if (g_frontend_screen ==
                    FrontendScreen::joystick_calibration) {
                    JOYINFOEX state{};
                    state.dwSize = sizeof(state);
                    state.dwFlags = JOY_RETURNX | JOY_RETURNY |
                                    JOY_RETURNBUTTONS;
                    if (joyGetPosEx(JOYSTICKID1, &state) != JOYERR_NOERROR) {
                        (void)set_joystick_enabled(false);
                        begin_page_exit(window, PageExit::cancel);
                        return 0;
                    }
                    if ((state.dwButtons & (JOY_BUTTON1 | JOY_BUTTON2)) != 0) {
                        const auto word_position = [](const DWORD value) {
                            return static_cast<std::uint16_t>(
                                std::min<DWORD>(value, 0xFFFF));
                        };
                        g_joystick_x_center = word_position(state.dwXpos);
                        g_joystick_y_center = word_position(state.dwYpos);
                        g_joystick_x_low = g_joystick_x_center / 3;
                        g_joystick_x_high = static_cast<std::uint16_t>(
                            std::min<int>(0xFFFF, g_joystick_x_center +
                                g_joystick_x_center / 3));
                        g_joystick_y_low = g_joystick_y_center / 3;
                        g_joystick_y_high = static_cast<std::uint16_t>(
                            std::min<int>(0xFFFF, g_joystick_y_center +
                                g_joystick_y_center / 3));
                        if ((state.dwButtons & JOY_BUTTON1) != 0) {
                            g_joystick_fire_button = 0;
                        }
                        if ((state.dwButtons & JOY_BUTTON2) != 0) {
                            g_joystick_fire_button = 1;
                        }
                        // 1392:0A33 clears both remembered axis codes after
                        // writing the new calibration thresholds.
                        g_joystick_direction_filter.reset();
                        g_joystick_calibration_succeeded = true;
                        begin_page_exit(window, PageExit::advance);
                        return 0;
                    }
                } else if (g_frontend_screen == FrontendScreen::volume_control) {
                    g_control_blink_ticks = (g_control_blink_ticks + 1) % 21;
                    if (g_volume_preview_ticks == 0) {
                        play_volume_preview_sound(window);
                    }
                    if (++g_volume_preview_ticks > 50) {
                        g_volume_preview_ticks = 0;
                    }
                }
                render_active_control_screen(window, true);
            } catch (const std::exception& error) {
                MessageBoxA(window, error.what(),
                            "Hocus Native - control-screen error",
                            MB_OK | MB_ICONERROR);
                update_options_screen(window);
            }
        } else if (wparam == 1 && !g_show_level &&
                   is_page_viewer_screen(g_frontend_screen) &&
                   g_page_has_stars) {
            try {
                render_active_page(window, true);
            } catch (const std::exception& error) {
                MessageBoxA(window, error.what(),
                            "Hocus Native - page animation error",
                            MB_OK | MB_ICONERROR);
                update_title_screen(window);
            }
        } else if (wparam == 1 && !g_show_level &&
                   is_dos_menu_screen(g_frontend_screen)) {
            try {
                g_menu_cursor_ticks = (g_menu_cursor_ticks + 1) % 40;
                g_dos_menu_stars.tick();
                render_active_dos_menu(window);
                if (g_frontend_screen == FrontendScreen::main_menu &&
                    GetTickCount64() - g_title_idle_started >
                        title_attract_stage_milliseconds) {
                    if (g_attract_credits_shown) {
                        start_demo(window);
                    } else {
                        start_auto_credits(window);
                    }
                }
            } catch (const std::exception& error) {
                MessageBoxA(window, error.what(), "Hocus Native - demo error",
                            MB_OK | MB_ICONERROR);
                update_title_screen(window);
            }
        }
        return 0;
    case MM_MCINOTIFY:
        if (g_music_device != 0 &&
            static_cast<MCIDEVICEID>(lparam) == g_music_device &&
            wparam == MCI_NOTIFY_SUCCESSFUL) {
            if (g_music_looping) {
                mciSendStringW(L"seek hocus_music to start", nullptr, 0, window);
                mciSendStringW(L"play hocus_music notify", nullptr, 0, window);
            } else {
                close_music();
            }
        }
        return 0;
    case WM_SIZE:
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    case WM_CLOSE:
        if (confirm_quit_application(window)) {
            DestroyWindow(window);
        }
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(window, &paint);
        RECT client{};
        GetClientRect(window, &client);
        const int client_width = client.right - client.left;
        const int client_height = client.bottom - client.top;

        // Compose the black letterbox and the logical framebuffer off-screen,
        // then publish them in one BitBlt. Painting black directly to the
        // window before StretchDIBits exposed that intermediate surface at
        // every timer invalidation, causing the reported whole-game flash.
        HDC back_dc = CreateCompatibleDC(dc);
        HBITMAP back_bitmap = CreateCompatibleBitmap(
            dc, std::max(1, client_width), std::max(1, client_height));
        HGDIOBJ old_bitmap = SelectObject(back_dc, back_bitmap);
        FillRect(back_dc, &client,
                 static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));

        if (!g_frame.pixels.empty()) {
            const double scale = std::min(
                static_cast<double>(client_width) / g_frame.width,
                static_cast<double>(client_height) / g_frame.height);
            const int draw_width = static_cast<int>(g_frame.width * scale);
            const int draw_height = static_cast<int>(g_frame.height * scale);
            const int left = (client_width - draw_width) / 2;
            const int top = (client_height - draw_height) / 2;

            BITMAPINFO info{};
            info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            info.bmiHeader.biWidth = g_frame.width;
            info.bmiHeader.biHeight = -g_frame.height;
            info.bmiHeader.biPlanes = 1;
            info.bmiHeader.biBitCount = 32;
            info.bmiHeader.biCompression = BI_RGB;
            SetStretchBltMode(back_dc, COLORONCOLOR);
            StretchDIBits(back_dc, left, top, draw_width, draw_height, 0, 0,
                          g_frame.width, g_frame.height, g_frame.pixels.data(),
                          &info, DIB_RGB_COLORS, SRCCOPY);
        }
        BitBlt(dc, 0, 0, client_width, client_height,
               back_dc, 0, 0, SRCCOPY);
        SelectObject(back_dc, old_bitmap);
        DeleteObject(back_bitmap);
        DeleteDC(back_dc);
        EndPaint(window, &paint);
        return 0;
    }
    case WM_DESTROY:
        KillTimer(window, 1);
        KillTimer(window, high_fps_timer_id);
        g_sound_mixer.stop_all();
        close_music();
        if (!g_music_path.empty()) {
            std::error_code error;
            std::filesystem::remove(g_music_path, error);
        }
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR command_line,
                    int show_command) {
    SetProcessDPIAware();
    if (command_line != nullptr &&
        std::wstring_view(command_line) == L"--verify-embedded-assets") {
        try {
            hocus::validate_registered_hocus_exe(
                required_hocus_exe_path());
            hocus::DatArchive archive(load_embedded_hocus_dat(instance));
            hocus::GameLevel probe(archive, {1, 1});
            const auto frame = probe.render().image;
            return archive.entries().size() ==
                       hocus::DatArchive::registered_v1_1_asset_count &&
                   frame.width == 320 && frame.height == 200
                ? 0 : 1;
        } catch (...) {
            return 1;
        }
    }
    const TimerResolution timer_resolution;
    try {
        hocus::validate_registered_hocus_exe(required_hocus_exe_path());
        g_archive = std::make_unique<hocus::DatArchive>(
            load_embedded_hocus_dat(instance));
        g_save_path = native_save_path();
        g_save_file = std::make_unique<hocus::SaveFile>(
            hocus::SaveFile::load_or_default(g_save_path));
        g_native_settings_path = native_settings_path();
        const auto native_settings = hocus::load_native_settings(
            g_native_settings_path);
        g_high_fps_mode = native_settings.high_fps_mode;
        g_widescreen_mode = native_settings.widescreen_mode;
        apply_dos_settings();
        load_sound_effects();
        rebuild_sound_playback_waves();
        if (g_joystick_enabled) {
            (void)set_joystick_enabled(true);
        }
        g_font_mask = g_archive->read(0);
        // 06B8:039C repeats the program-start srand(time(NULL)) immediately
        // before star initialization. Borland's srand accepts only the low
        // 16 bits of the 32-bit time_t result.
        g_borland_random.seed(static_cast<std::uint16_t>(
            static_cast<std::uint64_t>(std::time(nullptr)) & 0xFFFFu));
        g_dos_menu_stars.initialize(g_archive->read(5), g_archive->read(6));
        // 0548:02F1 resets the cursor after 06B8:039C has generated the 75
        // initial stars.
        g_dos_menu_stars.set_random_index(0);
        auto palette_bytes = g_archive->read(7);
        const auto menu_palette_tail = g_archive->read(8);
        palette_bytes.insert(palette_bytes.end(), menu_palette_tail.begin(),
                             menu_palette_tail.end());
        g_game_palette = hocus::decode_vga_palette(palette_bytes);
        g_bottom = hocus::decode_planar_img(g_archive->read(9), g_game_palette);
        g_menu_logo = hocus::decode_planar_img(g_archive->read(10),
                                                g_game_palette);
        g_menu_cursor_sheet = hocus::decode_planar_img(g_archive->read(14),
                                                        g_game_palette);
        g_volume_bar = hocus::decode_planar_img(g_archive->read(15),
                                                 g_game_palette);
        if (g_bottom.width != 320 || g_bottom.height != 16 ||
            g_menu_logo.width != 320 || g_menu_logo.height != 39) {
            throw std::runtime_error("Unexpected DOS menu asset dimensions");
        }
        if (g_volume_bar.width != 32 || g_volume_bar.height != 13) {
            throw std::runtime_error("Unexpected VOL_BAR.IMG dimensions");
        }
        if (g_menu_cursor_sheet.width != 128 ||
            g_menu_cursor_sheet.height != 15) {
            throw std::runtime_error("Unexpected BULLIT.IMG dimensions");
        }
        g_piracy = hocus::decode_pcx(g_archive->read(17));
        g_apogee = hocus::decode_pcx(g_archive->read(1));
        g_title = hocus::decode_pcx(g_archive->read(2));
        if (g_piracy.width != 320 || g_piracy.height != 200 ||
            g_apogee.width != 320 || g_apogee.height != 200 ||
            g_title.width != 320 || g_title.height != 200) {
            throw std::runtime_error("A startup PCX asset is not 320x200");
        }
        const auto registered = hocus::decode_planar_img(g_archive->read(3),
                                                         g_title.palette);
        if (registered.width != 320 || registered.height != 12) {
            throw std::runtime_error("The registered-title overlay is not 320x12");
        }
        hocus::blit(registered, g_title, 0, 188);
        g_frame = g_title;
        load_selected_level();
    } catch (const std::exception& error) {
        MessageBoxA(nullptr, error.what(), "Hocus Native - startup error",
                    MB_OK | MB_ICONERROR);
        return 1;
    }

    const wchar_t class_name[] = L"HocusNativeWindow";
    WNDCLASSW window_class{};
    window_class.lpfnWndProc = window_proc;
    window_class.hInstance = instance;
    window_class.hCursor = LoadCursor(nullptr, IDC_ARROW);
    window_class.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    window_class.lpszClassName = class_name;
    if (!RegisterClassW(&window_class)) {
        return 1;
    }

    // Prefer a 2x logical window, but scale ultrawide modes down when needed
    // so the complete client area remains inside the desktop work area.
    int initial_client_height = hocus::game_frame_height * 2;
    RECT work_area{};
    if (SystemParametersInfoW(SPI_GETWORKAREA, 0, &work_area, 0)) {
        const int maximum_width = work_area.right - work_area.left - 32;
        if (MulDiv(initial_client_height, active_gameplay_frame_width(),
                   hocus::game_frame_height) > maximum_width) {
            initial_client_height = MulDiv(
                maximum_width, hocus::game_frame_height,
                active_gameplay_frame_width());
        }
    }
    RECT desired{0, 0,
                 MulDiv(initial_client_height, active_gameplay_frame_width(),
                        hocus::game_frame_height),
                 initial_client_height};
    AdjustWindowRect(&desired, WS_OVERLAPPEDWINDOW, FALSE);
    HWND window = CreateWindowExW(
        0, class_name,
        L"Hocus Native - registered v1.1 (selected E1L1; Left/Right select, Enter play)",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
        desired.right - desired.left, desired.bottom - desired.top, nullptr,
        nullptr, instance, nullptr);
    if (!window) {
        return 1;
    }
    ShowWindow(window, show_command);
    begin_startup(window);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    if (g_xinput_module != nullptr) {
        FreeLibrary(g_xinput_module);
        g_xinput_module = nullptr;
        g_xinput_get_state = nullptr;
    }
    return static_cast<int>(message.wParam);
}
