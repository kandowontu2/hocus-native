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
#include "sprite.h"
#include "timing.h"
#include "ui.h"

#include <exception>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <set>

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "usage: hocus_asset_tests HOCUS.DAT HOCUS.EXE\n";
        return 2;
    }
    try {
        const auto settings_probe = std::filesystem::temp_directory_path() /
            ("hocus-native-settings-" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()) +
             ".cfg");
        hocus::write_native_settings(
            settings_probe,
            {true, hocus::WidescreenMode::ratio_32_9});
        const auto native_settings_round_trip =
            hocus::load_native_settings(settings_probe);
        if (!native_settings_round_trip.high_fps_mode ||
            native_settings_round_trip.widescreen_mode !=
                hocus::WidescreenMode::ratio_32_9) {
            std::cerr << "native presentation settings did not round-trip\n";
            return 1;
        }
        std::filesystem::remove(settings_probe);
        const auto default_native_settings =
            hocus::load_native_settings(settings_probe);
        if (default_native_settings.high_fps_mode ||
            default_native_settings.widescreen_mode !=
                hocus::WidescreenMode::off) {
            std::cerr << "missing native settings did not use defaults\n";
            return 1;
        }

        const hocus::DatArchive archive(argv[1]);
        hocus::validate_registered_hocus_exe(argv[2]);
        hocus::DosGameTimerCadence cadence;
        constexpr std::array<std::uint32_t, 7> slow_cadence =
            {57, 57, 57, 58, 57, 57, 57};
        constexpr std::array<std::uint32_t, 7> medium_cadence =
            {50, 50, 50, 50, 50, 50, 50};
        constexpr std::array<std::uint32_t, 7> fast_cadence =
            {43, 43, 43, 42, 43, 43, 43};
        constexpr std::array<std::uint32_t, 7> retrace_cadence =
            {14, 15, 14, 14, 14, 15, 14};
        const auto verify_cadence = [&cadence](const int ticks,
                                               const auto& expected) {
            cadence.reset();
            for (const auto interval : expected) {
                if (cadence.next_interval_ms(ticks) != interval) {
                    return false;
                }
            }
            return true;
        };
        if (!verify_cadence(8, slow_cadence) ||
            !verify_cadence(7, medium_cadence) ||
            !verify_cadence(6, fast_cadence) ||
            !verify_cadence(2, retrace_cadence)) {
            std::cerr << "DOS 140 Hz game cadence mismatch\n";
            return 1;
        }
        if (hocus::dos_hud_first_decimal_digit(0) != 0 ||
            hocus::dos_hud_first_decimal_digit(9) != 9 ||
            hocus::dos_hud_first_decimal_digit(15) != 1 ||
            hocus::dos_hud_first_decimal_digit(987) != 9) {
            std::cerr << "DOS crystal HUD first-character mismatch\n";
            return 1;
        }
        hocus::BorlandRandom borland_random;
        borland_random.seed(0x1234);
        constexpr std::array<std::uint16_t, 8> borland_sequence = {
            8151, 11177, 8835, 19970, 20207, 27819, 9986, 18848,
        };
        for (const auto expected : borland_sequence) {
            if (borland_random.next() != expected) {
                std::cerr << "Borland rand() sequence mismatch\n";
                return 1;
            }
        }
        if (borland_random.state() != 0x49A05E6Cu) {
            std::cerr << "Borland rand() state mismatch\n";
            return 1;
        }
        constexpr hocus::DosFrontendJoystickSample all_joystick_inputs{
            true, true, true, true, true, true,
        };
        hocus::DosFrontendJoystickSample joystick_left;
        joystick_left.left = true;
        hocus::DosFrontendJoystickSample joystick_right;
        joystick_right.right = true;
        if (hocus::dos_frontend_joystick_code(joystick_left) != 5 ||
            hocus::dos_frontend_joystick_code(joystick_right) != 4 ||
            hocus::dos_frontend_joystick_code(all_joystick_inputs) != 6) {
            std::cerr << "DOS front-end joystick priority mismatch\n";
            return 1;
        }
        hocus::DosJoystickDirectionFilter joystick_filter;
        hocus::DosFrontendJoystickSample joystick_button;
        joystick_button.button_one = true;
        if (joystick_filter.filter(joystick_left).left ||
            !joystick_filter.filter(joystick_left).left ||
            !joystick_filter.filter(joystick_button).button_one ||
            !joystick_filter.filter(joystick_left).left ||
            joystick_filter.filter(joystick_right).right ||
            !joystick_filter.filter(joystick_right).right) {
            std::cerr << "DOS joystick direction confirmation mismatch\n";
            return 1;
        }
        hocus::DosFrontendJoystickRepeat joystick_repeat;
        hocus::DosFrontendJoystickSample joystick_up;
        joystick_up.up = true;
        if (joystick_repeat.poll(joystick_up) != 1 ||
            joystick_repeat.cooldown() != 10) {
            std::cerr << "DOS front-end joystick initial poll mismatch\n";
            return 1;
        }
        for (int suppressed = 0; suppressed < 10; ++suppressed) {
            if (joystick_repeat.poll(joystick_up) != 0) {
                std::cerr << "DOS front-end joystick debounce mismatch\n";
                return 1;
            }
        }
        if (joystick_repeat.poll(joystick_up) != 1) {
            std::cerr << "DOS front-end joystick repeat mismatch\n";
            return 1;
        }
        hocus::InputState all_keyboard{
            true, true, true, true, true, true, true, true,
        };
        hocus::DosFrontendJoystickSample gameplay_joystick;
        gameplay_joystick.right = true;
        gameplay_joystick.button_one = true;
        gameplay_joystick.button_three = true;
        const auto keyboard_gameplay = hocus::dos_gameplay_input(
            all_keyboard, gameplay_joystick, false, 0);
        const auto joystick_gameplay = hocus::dos_gameplay_input(
            all_keyboard, gameplay_joystick, true, 0);
        const auto swapped_gameplay = hocus::dos_gameplay_input(
            {}, gameplay_joystick, true, 1);
        if (!keyboard_gameplay.left || !keyboard_gameplay.scroll_down ||
            joystick_gameplay.left || !joystick_gameplay.right ||
            joystick_gameplay.jump || !joystick_gameplay.fire ||
            !joystick_gameplay.scroll_down || swapped_gameplay.fire ||
            !swapped_gameplay.jump) {
            std::cerr << "DOS joystick-exclusive gameplay policy mismatch\n";
            return 1;
        }
        hocus::StartupSequence startup;
        if (startup.stage() != hocus::StartupStage::initial_fade_out ||
            startup.fade().numerator != 40 ||
            startup.fade().denominator != 40) {
            std::cerr << "startup initial fade mismatch\n";
            return 1;
        }
        for (int frame = 0; frame < 40; ++frame) {
            startup.tick(14);
        }
        if (startup.stage() != hocus::StartupStage::piracy_fade_in ||
            startup.fade().numerator != 0) {
            std::cerr << "startup piracy entry mismatch\n";
            return 1;
        }
        for (int frame = 0; frame < 20; ++frame) {
            startup.tick(14);
        }
        startup.tick(14999);
        if (startup.stage() != hocus::StartupStage::piracy_hold) {
            std::cerr << "startup piracy hold duration mismatch\n";
            return 1;
        }
        startup.tick(1);
        for (int frame = 0; frame < 20; ++frame) {
            startup.tick(14);
        }
        if (startup.stage() != hocus::StartupStage::apogee_fade_in) {
            std::cerr << "startup Apogee entry mismatch\n";
            return 1;
        }
        for (int frame = 0; frame < 20; ++frame) {
            startup.tick(14);
        }
        startup.tick(8999);
        if (startup.stage() != hocus::StartupStage::apogee_hold) {
            std::cerr << "startup Apogee hold duration mismatch\n";
            return 1;
        }
        startup.tick(1);
        for (int frame = 0; frame < 30; ++frame) {
            startup.tick(14);
        }
        if (startup.stage() != hocus::StartupStage::title_fade_in) {
            std::cerr << "startup title entry mismatch\n";
            return 1;
        }
        for (int frame = 0; frame < 40; ++frame) {
            startup.tick(14);
        }
        startup.tick(3999);
        if (startup.stage() != hocus::StartupStage::title_hold) {
            std::cerr << "startup title hold duration mismatch\n";
            return 1;
        }
        startup.tick(1);
        for (int frame = 0; frame < 30; ++frame) {
            startup.tick(14);
        }
        if (!startup.done() || startup.fade().numerator != 0) {
            std::cerr << "startup completion mismatch\n";
            return 1;
        }
        hocus::StartupSequence advanced_startup;
        for (int frame = 0; frame < 40; ++frame) {
            advanced_startup.tick(14);
        }
        for (int frame = 0; frame < 20; ++frame) {
            advanced_startup.tick(14);
        }
        advanced_startup.press_any_key();
        if (advanced_startup.stage() !=
                hocus::StartupStage::piracy_fade_out ||
            advanced_startup.done()) {
            std::cerr << "startup any-key screen advance mismatch\n";
            return 1;
        }
        for (int frame = 0; frame < 20; ++frame) {
            advanced_startup.tick(14);
        }
        if (advanced_startup.stage() !=
            hocus::StartupStage::apogee_fade_in) {
            std::cerr << "startup any-key bypassed Apogee mismatch\n";
            return 1;
        }
        hocus::StartupSequence buffered_startup;
        buffered_startup.press_any_key();
        for (int frame = 0; frame < 40; ++frame) {
            buffered_startup.tick(14);
        }
        for (int frame = 0; frame < 20; ++frame) {
            buffered_startup.tick(14);
        }
        if (buffered_startup.stage() != hocus::StartupStage::piracy_hold) {
            std::cerr << "startup buffered key presentation mismatch\n";
            return 1;
        }
        buffered_startup.tick(14);
        if (buffered_startup.stage() !=
            hocus::StartupStage::piracy_fade_out) {
            std::cerr << "startup buffered key consumption mismatch\n";
            return 1;
        }
        const hocus::DecodedImage fade_probe{
            1, 1, {0x00FFFFFF}, {}, {0x00FFFFFF},
        };
        const auto half_fade = hocus::render_vga_fade(fade_probe, {10, 20});
        if (half_fade.pixels.front() != 0x007D7D7D ||
            half_fade.palette.front() != 0x007D7D7D) {
            std::cerr << "six-bit VGA fade raster mismatch\n";
            return 1;
        }
        const hocus::DecodedImage white_probe{
            1, 1, {0x002800FF}, {}, {0x002800FF},
        };
        const auto white_fade = hocus::render_vga_whiten(white_probe, 5);
        if (white_fade.pixels.front() != 0x003D14FF ||
            white_fade.palette.front() != 0x003D14FF) {
            std::cerr << "six-bit VGA white-fade raster mismatch\n";
            return 1;
        }

        const auto easy_results = hocus::calculate_level_results(
            {1, 1}, 0, 4, 4, 149);
        const auto hard_results = hocus::calculate_level_results(
            {4, 9}, 2, 3, 4, 180);
        if (easy_results.time_limit != 150 ||
            easy_results.treasure_bonus != 25000 ||
            easy_results.time_bonus != 25000 ||
            easy_results.total_bonus() != 50000 ||
            hard_results.time_limit != 180 ||
            hard_results.treasure_accuracy != 75 ||
            hard_results.treasure_bonus != 0 ||
            hard_results.time_bonus != 0) {
            std::cerr << "campaign result bonus mismatch\n";
            return 1;
        }
        if (archive.version() != "registered-v1.1" ||
            archive.entries().size() !=
                hocus::DatArchive::registered_v1_1_asset_count) {
            std::cerr << "archive identity mismatch\n";
            return 1;
        }
        constexpr std::array<std::size_t, 5> demo_frame_counts = {
            1563, 1912, 2155, 1903, 1401,
        };
        for (std::size_t index = 0; index < demo_frame_counts.size(); ++index) {
            const auto frames = hocus::decode_demo(archive.read(18 + index));
            if (frames.size() != demo_frame_counts[index] ||
                hocus::demo_level(index) !=
                    hocus::LevelId{1, static_cast<int>(index * 2 + 1)}) {
                std::cerr << "demo format or level mapping mismatch at asset "
                          << 18 + index << '\n';
                return 1;
            }
            hocus::GameLevel demo_game(archive, hocus::demo_level(index));
            for (const auto& input : frames) {
                demo_game.tick(input);
                if (demo_game.player_dead() || demo_game.level_complete()) {
                    break;
                }
            }
            const auto demo_render = demo_game.render();
            if (demo_render.image.width != 320 ||
                demo_render.image.height != 200) {
                std::cerr << "demo replay render mismatch at asset "
                          << 18 + index << '\n';
                return 1;
            }
        }
        const auto mapped_demo = hocus::decode_demo(
            {1, 0, 1, 0, 1, 1, 0, 1});
        if (mapped_demo.size() != 1 || !mapped_demo[0].action ||
            mapped_demo[0].left || !mapped_demo[0].right ||
            !mapped_demo[0].fire || mapped_demo[0].jump ||
            !mapped_demo[0].down) {
            std::cerr << "demo input order mismatch\n";
            return 1;
        }
        const auto original_save_path =
            std::filesystem::path(argv[1]).parent_path() / "HOCUS.SAV";
        const auto original_save = hocus::SaveFile::load(original_save_path);
        const auto blank_save = hocus::SaveFile::blank();
        const auto blank_settings = blank_save.dos_settings();
        const std::array<std::uint8_t, 8> default_key_bindings = {
            15, 16, 2, 3, 13, 14, 11, 12,
        };
        if (!blank_settings.sound_enabled || !blank_settings.music_enabled ||
            blank_settings.joystick_enabled || blank_settings.game_speed != 1 ||
            blank_settings.key_bindings != default_key_bindings ||
            blank_settings.sound_volume != 15 ||
            blank_settings.music_volume != 15) {
            std::cerr << "default DOS settings mismatch\n";
            return 1;
        }
        auto changed_settings_save = blank_save;
        auto changed_settings = blank_settings;
        changed_settings.sound_enabled = false;
        changed_settings.music_enabled = false;
        changed_settings.joystick_enabled = true;
        changed_settings.game_speed = 2;
        changed_settings.joystick_x_low = 100;
        changed_settings.joystick_x_center = 300;
        changed_settings.joystick_x_high = 400;
        changed_settings.joystick_y_low = 200;
        changed_settings.joystick_y_center = 600;
        changed_settings.joystick_y_high = 800;
        changed_settings.joystick_fire_button = 1;
        changed_settings.key_bindings[0] = 4;
        changed_settings.sound_volume = 3;
        changed_settings.music_volume = 12;
        changed_settings_save.set_dos_settings(changed_settings);
        const auto settings_round_trip = changed_settings_save.dos_settings();
        if (settings_round_trip.sound_enabled ||
            settings_round_trip.music_enabled ||
            !settings_round_trip.joystick_enabled ||
            settings_round_trip.game_speed != 2 ||
            settings_round_trip.joystick_x_low != 100 ||
            settings_round_trip.joystick_x_center != 300 ||
            settings_round_trip.joystick_x_high != 400 ||
            settings_round_trip.joystick_y_low != 200 ||
            settings_round_trip.joystick_y_center != 600 ||
            settings_round_trip.joystick_y_high != 800 ||
            settings_round_trip.joystick_fire_button != 1 ||
            settings_round_trip.key_bindings[0] != 4 ||
            settings_round_trip.sound_volume != 3 ||
            settings_round_trip.music_volume != 12) {
            std::cerr << "DOS settings round-trip mismatch\n";
            return 1;
        }
        const auto default_scores = blank_save.high_scores(0);
        if (default_scores[0].name != "Hocus Pocus" ||
            default_scores[0].score != 1000000 ||
            default_scores[4].score != 200000) {
            std::cerr << "default high-score table mismatch\n";
            return 1;
        }
        // 06B8:1BAE uses a strict signed-long comparison: an equal score does
        // not qualify, and a value with bit 31 set sorts below positive scores.
        if (blank_save.qualifying_high_score_rank(0, 200000).has_value() ||
            blank_save.qualifying_high_score_rank(0, 0x80000000U).has_value() ||
            blank_save.qualifying_high_score_rank(0, 1000000) != 1 ||
            blank_save.qualifying_high_score_rank(0, 1000001) != 0) {
            std::cerr << "DOS high-score comparison mismatch\n";
            return 1;
        }
        auto inserted_scores = blank_save;
        inserted_scores.insert_high_score(2, "Native Hocus", 900000);
        const auto episode_three_scores = inserted_scores.high_scores(2);
        if (episode_three_scores[1].name != "Native Hocus" ||
            episode_three_scores[1].score != 900000 ||
            episode_three_scores[2].score != 800000) {
            std::cerr << "high-score insertion mismatch\n";
            return 1;
        }
        auto empty_names = blank_save;
        empty_names.insert_high_score(0, "", 1100000);
        if (!empty_names.high_scores(0)[0].name.empty()) {
            std::cerr << "empty high-score name was replaced\n";
            return 1;
        }
        empty_names.set_high_score_name(0, 0, "Player");
        empty_names.set_slot(0, "", {1, 1}, 1, 0);
        if (empty_names.high_scores(0)[0].name != "Player" ||
            !empty_names.slot(0).name.empty()) {
            std::cerr << "DOS empty-name editing mismatch\n";
            return 1;
        }
        for (std::size_t index = 0; index < hocus::SaveFile::slot_count; ++index) {
            // Parsing the user's current slots must always remain valid, while
            // the in-memory fallback independently proves the empty sentinel.
            (void)original_save.slot(index);
            if (blank_save.slot(index).occupied ||
                blank_save.slot(index).name != "<empty>") {
                std::cerr << "blank save slot mismatch\n";
                return 1;
            }
        }
        auto modified_save = original_save;
        modified_save.set_slot(4, "Native Hocus", {3, 7}, 2, 0x12345678U);
        const auto temporary_save_path = std::filesystem::temp_directory_path() /
            ("hocus-native-save-test-" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()) +
             ".sav");
        modified_save.write(temporary_save_path);
        const auto round_trip_save = hocus::SaveFile::load(temporary_save_path);
        std::error_code remove_error;
        std::filesystem::remove(temporary_save_path, remove_error);
        auto temporary_backup = temporary_save_path;
        temporary_backup += ".bak";
        std::filesystem::remove(temporary_backup, remove_error);
        const auto saved_slot = round_trip_save.slot(4);
        if (!saved_slot.occupied || saved_slot.name != "Native Hocus" ||
            saved_slot.level != hocus::LevelId{3, 7} || saved_slot.skill != 2 ||
            saved_slot.score != 0x12345678U) {
            std::cerr << "DOS save slot round-trip mismatch\n";
            return 1;
        }
        // Slot writes must leave the opaque options/high-score regions intact.
        constexpr std::array<std::pair<std::size_t, std::size_t>, 5>
            writable_ranges = {{{0x1E, 0x54}, {0x54, 0x13E},
                                {0x13E, 0x147}, {0x148, 0x16C},
                                {hocus::SaveFile::file_size,
                                 hocus::SaveFile::file_size}}};
        for (std::size_t offset = 0; offset < hocus::SaveFile::file_size; ++offset) {
            const bool slot_region = std::any_of(
                writable_ranges.begin(), writable_ranges.end(),
                [offset](const auto range) {
                    return offset >= range.first && offset < range.second;
                });
            if (!slot_region && original_save.bytes()[offset] !=
                                    round_trip_save.bytes()[offset]) {
                std::cerr << "save write changed an opaque byte\n";
                return 1;
            }
        }
        constexpr std::array<std::size_t, 16> sound_assets = {
            620, 624, 626, 628, 634, 630, 636, 638,
            640, 642, 644, 612, 618, 622, 646, 648,
        };
        for (const auto asset : sound_assets) {
            const auto wave = hocus::decode_voc_to_wav(archive.read(asset));
            if (wave.size() <= 44 ||
                !std::equal(wave.begin(), wave.begin() + 4, "RIFF") ||
                !std::equal(wave.begin() + 8, wave.begin() + 12, "WAVE") ||
                !std::equal(wave.begin() + 36, wave.begin() + 40, "data")) {
                std::cerr << "VOC sound conversion mismatch at asset "
                          << asset << '\n';
                return 1;
            }
        }
        for (std::size_t asset = 599; asset <= 610; ++asset) {
            const auto midi = archive.read(asset);
            if (midi.size() < 14 ||
                !std::equal(midi.begin(), midi.begin() + 4, "MThd")) {
                std::cerr << "MIDI asset mismatch at asset " << asset << '\n';
                return 1;
            }
        }
        const auto title = hocus::decode_pcx(archive.read(2));
        if (title.width != 320 || title.height != 200 ||
            title.pixels.size() != 64000) {
            std::cerr << "title decode mismatch\n";
            return 1;
        }
        const auto registered = hocus::decode_planar_img(archive.read(3), title.palette);
        if (registered.width != 320 || registered.height != 12) {
            std::cerr << "registered title overlay mismatch\n";
            return 1;
        }
        auto palette_bytes = archive.read(7);
        const auto palette_tail = archive.read(8);
        palette_bytes.insert(palette_bytes.end(), palette_tail.begin(), palette_tail.end());
        const auto palette = hocus::decode_vga_palette(palette_bytes);
        const auto story_page = hocus::decode_text_page(archive.read(52));
        const auto bottom = hocus::decode_planar_img(archive.read(9), palette);
        const auto menu_logo = hocus::decode_planar_img(archive.read(10), palette);
        const auto menu_cursor = hocus::decode_planar_img(archive.read(14), palette);
        const auto volume_bar = hocus::decode_planar_img(archive.read(15), palette);
        const auto font = archive.read(0);
        const std::vector<std::string> main_menu_lines = {
            "Begin a new game", "Restore an old game",
            "Ordering Information~", "Instructions", "Legends and hints~",
            "Change game options", "High scores", "Quit - return to DOS",
        };
        const auto main_menu = hocus::render_dos_menu(
            menu_logo, bottom, menu_cursor, font, palette, main_menu_lines,
            false, 0, 0);
        const std::vector<std::string> accelerated_menu_lines = {
            "High scores", "HIGH &FPS MODE: OFF",
        };
        const auto accelerated_menu = hocus::render_dos_menu(
            menu_logo, bottom, menu_cursor, font, palette,
            accelerated_menu_lines, false, 1, 0);
        const std::vector<std::string> widescreen_menu_lines = {
            "HIGH &FPS MODE: OFF", "&WIDESCREEN MODE: OFF",
        };
        const auto widescreen_menu = hocus::render_dos_menu(
            menu_logo, bottom, menu_cursor, font, palette,
            widescreen_menu_lines, false, 1, 0);
        const std::array<std::string, 9> slot_names = {
            "Hocus", "", "Two", "Three", "Four",
            "Five", "Six", "Seven", "Eight",
        };
        const auto slot_screen = hocus::render_dos_slot_screen(
            menu_logo, bottom, menu_cursor, font, palette, "Select SAVE slot",
            slot_names, 0, 0, true);
        const std::array<hocus::DosHighScoreEntry, 5> score_lines = {{
            {"Hocus Pocus", 1000000}, {"Andre Foucault", 800000},
            {"Jamie Cook", 600000}, {"Karim Sultan", 400000},
            {"Chris tenDen", 200000},
        }};
        const auto high_scores = hocus::render_dos_high_scores(
            menu_logo, bottom, font, palette, 0, score_lines);
        const auto volume_screen = hocus::render_dos_volume_screen(
            volume_bar, font, palette, 15, 12, 0, true);
        const auto key_screen = hocus::render_dos_key_screen(
            font, palette, default_key_bindings);
        const auto key_choice_screen = hocus::render_dos_key_screen(
            font, palette, default_key_bindings, true, 7);
        const auto calibration_screen = hocus::render_dos_message_screen(
            font, palette,
            "Move joystick to center and press FIRE button", 5, "ESC to exit");
        const auto confirmation_screen = hocus::render_dos_confirmation_screen(
            font, palette, "Reminder: Save your game before quitting",
            "Quit game?");
        const auto result_screen = hocus::render_dos_level_results(
            font, palette, true, 1, 5, 5, 100, 1, 150, 100);
        if (volume_bar.width != 32 || volume_bar.height != 13 ||
            volume_screen.width != 320 || volume_screen.height != 200 ||
            key_screen.width != 320 || key_screen.height != 200 ||
            key_choice_screen.width != 320 || key_choice_screen.height != 200 ||
            calibration_screen.width != 320 ||
            calibration_screen.height != 200 ||
            confirmation_screen.width != 320 ||
            confirmation_screen.height != 200 ||
            result_screen.width != 320 || result_screen.height != 200) {
            std::cerr << "DOS control-screen raster mismatch\n";
            return 1;
        }
        auto star_menu = main_menu.image;
        hocus::DosMenuStarfield menu_stars;
        menu_stars.initialize(archive.read(5), archive.read(6));
        menu_stars.set_random_index(0);
        (void)menu_stars.next_random_below(16);
        if (menu_stars.random_index() != 1) {
            std::cerr << "shared RANDOM.DAT cursor mismatch\n";
            return 1;
        }
        menu_stars.tick();
        menu_stars.draw(star_menu);
        const std::vector<int> expected_menu_y = {
            76, 86, 96, 110, 120, 134, 144, 154,
        };
        const std::array<int, 9> expected_slot_y = {
            79, 89, 99, 109, 119, 129, 139, 149, 159,
        };
        bool cursor_matches = true;
        for (int y = 0; y < 15; ++y) {
            for (int x = 0; x < 16; ++x) {
                const auto source = static_cast<std::size_t>(y) *
                                        menu_cursor.width + x;
                const auto destination = static_cast<std::size_t>(y + 72) * 320 +
                                         68 + x;
                cursor_matches = cursor_matches &&
                    main_menu.image.pixels[destination] ==
                        menu_cursor.pixels[source];
            }
        }
        bool slot_cursor_matches = true;
        for (int y = 0; y < 15; ++y) {
            for (int x = 0; x < 16; ++x) {
                const auto source = static_cast<std::size_t>(y) *
                                        menu_cursor.width + x;
                const auto destination = static_cast<std::size_t>(y + 75) * 320 +
                                         4 + x;
                slot_cursor_matches = slot_cursor_matches &&
                    slot_screen.image.pixels[destination] ==
                        menu_cursor.pixels[source];
            }
        }
        const auto story_picture =
            hocus::decode_planar_img(archive.read(123), palette);
        const auto story_frame = hocus::render_text_page(
            story_page, archive.read(0), palette, bottom,
            {hocus::TextPageLayout::illustrated, &story_picture, 4, 16,
             0, 10});
        hocus::DecodedImage font_probe{8, 8,
            std::vector<std::uint32_t>(64, 0), {}, {}};
        hocus::draw_font_text(font_probe, archive.read(0), "F", 0, 0,
                              0x00FFFFFF);
        bool high_fps_f_is_silver = false;
        const int accelerated_y = accelerated_menu.item_y[1];
        const int f_x = accelerated_menu.text_x +
                        hocus::font_text_width(font, "HIGH ");
        for (int row = 0; row < 8; ++row) {
            for (int column = 0; column < 8; ++column) {
                if (accelerated_menu.image.pixels[
                        static_cast<std::size_t>(accelerated_y + row) * 320 +
                        f_x + column] == palette[0xD8 + row]) {
                    high_fps_f_is_silver = true;
                }
            }
        }
        bool widescreen_w_is_silver = false;
        const int widescreen_y = widescreen_menu.item_y[1];
        for (int row = 0; row < 8; ++row) {
            for (int column = 0; column < 8; ++column) {
                if (widescreen_menu.image.pixels[
                        static_cast<std::size_t>(widescreen_y + row) * 320 +
                        widescreen_menu.text_x + column] ==
                    palette[0xD8 + row]) {
                    widescreen_w_is_silver = true;
                }
            }
        }
        if (story_page.lines.size() != 18 ||
            story_page.nonempty_spacing != 10 ||
            story_page.empty_spacing != 10 ||
            story_page.lines.front().text != "THE STORY OF HOCUS POCUS" ||
            bottom.width != 320 || bottom.height != 16 ||
            menu_logo.width != 320 || menu_logo.height != 39 ||
            hocus::font_text_width(font, "Ordering Information~") != 129 ||
            hocus::font_text_width(font, "Use UP/DOWN/LETTER to move - ENTER to select") != 291 ||
            main_menu.text_x != 95 || main_menu.item_y != expected_menu_y ||
            hocus::dos_menu_shortcut_key("High scores") != 'H' ||
            hocus::dos_menu_shortcut_key("HIGH &FPS MODE: OFF") != 'F' ||
            hocus::dos_menu_shortcut_key("&WIDESCREEN MODE: OFF") != 'W' ||
            accelerated_menu.text_x !=
                (320 - hocus::font_text_width(font, "HIGH FPS MODE: OFF")) / 2 ||
            !high_fps_f_is_silver ||
            !widescreen_w_is_silver ||
            !cursor_matches ||
            slot_screen.item_y != expected_slot_y || !slot_cursor_matches ||
            high_scores.width != 320 || high_scores.height != 200 ||
            hocus::dos_page_prompt(0, 1) != "Press any key to continue" ||
            hocus::dos_page_prompt(0, 9) !=
                "Page 1 of 9 - Press PGDN - ESC to cancel" ||
            hocus::dos_page_prompt(4, 9) !=
                "Page 5 of 9 - Press PGUP/PGDN - ESC to cancel" ||
            hocus::dos_page_prompt(8, 9) !=
                "Page 9 of 9 - Press PGUP - ESC to cancel" ||
            !menu_stars.initialized() ||
            star_menu.pixels[72 * 320 + 265] != palette[0xF0] ||
            star_menu.pixels[131 * 320 + 100] != palette[0xF0] ||
            star_menu.pixels[115 * 320 + 6] != palette[0xF0] ||
            story_frame.width != 320 || story_frame.height != 200 ||
            story_frame.pixels.size() != 64000 ||
            font_probe.pixels[8] != 0x00FFFFFF ||
            font_probe.pixels[15] != 0) {
            std::cerr << "story page decode/render mismatch\n";
            return 1;
        }
        for (std::size_t asset = 41; asset <= 71; ++asset) {
            const auto page = hocus::decode_text_page(archive.read(asset));
            const auto rendered = hocus::render_text_page(
                page, archive.read(0), palette, bottom);
            if (rendered.width != 320 || rendered.height != 200) {
                std::cerr << "front-end text page mismatch at asset "
                          << asset << '\n';
                return 1;
            }
        }
        for (const std::size_t asset : {23U, 24U, 72U}) {
            const auto page = hocus::decode_pcx(archive.read(asset));
            if (page.width != 320 || page.height != 200) {
                std::cerr << "front-end PCX mismatch at asset " << asset << '\n';
                return 1;
            }
        }
        const auto bullet = hocus::decode_planar_img(archive.read(14), palette);
        if (bullet.width != 128 || bullet.height != 15) {
            std::cerr << "pickup-sparkle image mismatch\n";
            return 1;
        }
        const auto player = hocus::decode_sprite_frame(archive.read(130), 0, 0, false,
                                                       palette);
        const auto twinks = hocus::decode_sprite_record_info(archive.read(130), 2);
        const auto morph = hocus::decode_sprite_record_info(archive.read(130), 3);
        const auto awful_al = hocus::decode_sprite_record_info(archive.read(130), 4);
        const auto flower_flinger =
            hocus::decode_sprite_record_info(archive.read(130), 20);
        const auto boss_four =
            hocus::decode_sprite_record_info(archive.read(130), 39);
        const auto player_bolt = hocus::decode_sprite_frame(
            archive.read(130), 0, 11, false, palette);
        const auto player_laser = hocus::decode_sprite_frame(
            archive.read(130), 0, 13, false, palette);
        const auto player_laser_vertical = hocus::decode_sprite_frame(
            archive.read(130), 0, 14, false, palette);
        const auto first_bolt_pixel = std::find(
            player_bolt.opacity.begin(), player_bolt.opacity.end(), 1);
        if (player.width != 24 || player.height != 32 ||
            player.opacity.empty() || player_bolt.width != 24 ||
            player_bolt.height != 32 ||
            player_laser.width != 24 || player_laser.height != 32 ||
            player_laser_vertical.width != 24 ||
            player_laser_vertical.height != 32 ||
            first_bolt_pixel == player_bolt.opacity.end()) {
            std::cerr << "player sprite decode mismatch\n";
            return 1;
        }
        if (twinks.name != "Twinks" || twinks.width != 16 ||
            twinks.height != 13 || twinks.frame_count != 5 ||
            morph.name != "Morph" || morph.width != 24 ||
            morph.height != 32 || morph.frame_count != 5 ||
            awful_al.name != "Awful Al" || awful_al.width != 32 ||
            awful_al.height != 32 || awful_al.movement_first != 0 ||
            awful_al.movement_last != 3 || awful_al.attack_first != 4 ||
            awful_al.attack_last != 5 || awful_al.projectile_width != 4 ||
            awful_al.projectile_height != 5 ||
            awful_al.projectile_y_offset != 13 ||
            awful_al.projectile_first != 6 || awful_al.projectile_last != 6 ||
            awful_al.frame_count != 7 || flower_flinger.attack_first != 1 ||
            flower_flinger.attack_last != 6 ||
            flower_flinger.projectile_first != 7 ||
            flower_flinger.projectile_last != 8 ||
            flower_flinger.frame_count != 9 || boss_four.width != 60 ||
            boss_four.height != 64 || boss_four.projectile_first != 3 ||
            boss_four.projectile_last != 3) {
            std::cerr << "sprite animation/projectile metadata mismatch\n";
            return 1;
        }
        const auto sprite_matches = [](const hocus::DecodedImage& frame,
                                       const hocus::DecodedImage& sprite,
                                       const int destination_x,
                                       const int destination_y) {
            for (int y = 0; y < sprite.height; ++y) {
                for (int x = 0; x < sprite.width; ++x) {
                    const auto source = static_cast<std::size_t>(y) *
                                            sprite.width + x;
                    if (!sprite.opacity.empty() &&
                        sprite.opacity[source] == 0) {
                        continue;
                    }
                    const auto destination = static_cast<std::size_t>(
                        destination_y + y) * frame.width + destination_x + x;
                    if (frame.pixels[destination] != sprite.pixels[source]) {
                        return false;
                    }
                }
            }
            return true;
        };
        hocus::GameLevel game(archive);
        const auto level = game.render();
        auto e1_active_palette_bytes = archive.read(7); // GAMEPAL.PAL
        const auto e1_back_palette_bytes = archive.read(73); // BACK01.PAL
        e1_active_palette_bytes.insert(e1_active_palette_bytes.end(),
                                       e1_back_palette_bytes.begin(),
                                       e1_back_palette_bytes.end());
        const auto e1_active_palette =
            hocus::decode_vga_palette(e1_active_palette_bytes);
        hocus::GameLevel first_level_tip(archive, {1, 1}, 0, 0, true);
        const auto tip_frame = first_level_tip.render().image;
        const auto tip_asset = hocus::decode_planar_img(
            archive.read(16), e1_active_palette);
        bool tip_matches = tip_asset.width == 220 && tip_asset.height == 68;
        for (int y = 0; y < tip_asset.height && tip_matches; ++y) {
            for (int x = 0; x < tip_asset.width; ++x) {
                const auto source = static_cast<std::size_t>(y) *
                                        tip_asset.width + x;
                const auto destination = static_cast<std::size_t>(46 + y) *
                                             tip_frame.width + 48 + x;
                if (tip_frame.pixels[destination] !=
                    tip_asset.pixels[source]) {
                    tip_matches = false;
                    break;
                }
            }
        }
        first_level_tip.tick({});
        if (!first_level_tip.modal_overlay_active() ||
            !first_level_tip.active_message().empty() ||
            first_level_tip.elapsed_ticks() != 0 || !tip_matches) {
            std::cerr << "first-level crystal tip mismatch\n";
            return 1;
        }
        first_level_tip.dismiss_modal_overlay();
        if (first_level_tip.modal_overlay_active()) {
            std::cerr << "first-level crystal tip dismissal mismatch\n";
            return 1;
        }
        if (level.image.width != 320 || level.image.height != 200 ||
            level.player_pixel_x != 48 || level.player_pixel_y != 912 ||
            level.camera_pixel_x != 0 || level.camera_pixel_y != 800 ||
            level.image.palette != e1_active_palette ||
            level.image.palette[0x6F] != 0x00450000 ||
            level.image.palette[0x80] != e1_active_palette[0x80]) {
            std::cerr << "E1L1 render mismatch\n";
            return 1;
        }
        hocus::GameLevel widescreen_game(
            archive, {1, 1}, 0, 0, false,
            hocus::widescreen_frame_width);
        widescreen_game.place_player(106, 36 * 16);
        const auto widescreen_scene = widescreen_game.render();
        hocus::GameLevel standard_game(archive);
        standard_game.place_player(106, 36 * 16);
        const auto standard_scene = standard_game.render();
        bool centred_hud_matches = true;
        int widened_world_pixels = 0;
        constexpr int widescreen_ui_offset =
            (hocus::widescreen_frame_width - hocus::original_frame_width) / 2;
        const auto widescreen_backdrop = hocus::decode_pcx(
            archive.read(static_cast<std::size_t>(
                widescreen_game.backdrop_asset_index())),
            e1_active_palette);
        for (int y = 0; y < hocus::gameplay_viewport_height; ++y) {
            for (int x = 0; x < hocus::widescreen_frame_width; ++x) {
                if (x >= widescreen_ui_offset &&
                    x < hocus::widescreen_frame_width -
                            widescreen_ui_offset) {
                    continue;
                }
                const int backdrop_x =
                    (x - widescreen_ui_offset +
                     hocus::original_frame_width) %
                    hocus::original_frame_width;
                if (widescreen_scene.image.pixels[
                        static_cast<std::size_t>(y) *
                            hocus::widescreen_frame_width + x] !=
                    widescreen_backdrop.pixels[
                        static_cast<std::size_t>(y) *
                            hocus::original_frame_width + backdrop_x]) {
                    ++widened_world_pixels;
                }
            }
        }
        for (int y = hocus::gameplay_viewport_height;
             y < hocus::game_frame_height && centred_hud_matches; ++y) {
            for (int x = 0; x < hocus::original_frame_width; ++x) {
                const auto standard_pixel =
                    standard_scene.image.pixels[
                        static_cast<std::size_t>(y) *
                            hocus::original_frame_width + x];
                const auto widescreen_pixel =
                    widescreen_scene.image.pixels[
                        static_cast<std::size_t>(y) *
                            hocus::widescreen_frame_width +
                        x + widescreen_ui_offset];
                if (standard_pixel != widescreen_pixel) {
                    centred_hud_matches = false;
                    break;
                }
            }
        }
        if (widescreen_game.viewport_width() !=
                hocus::widescreen_frame_width ||
            widescreen_scene.image.width != hocus::widescreen_frame_width ||
            widescreen_scene.image.height != hocus::game_frame_height ||
            widescreen_scene.camera_pixel_x != 672 ||
            widescreen_scene.camera_pixel_y != 496 ||
            !sprite_matches(widescreen_scene.image, player, 176, 80) ||
            widened_world_pixels == 0 || !centred_hud_matches ||
            widescreen_menu.image.width != 320) {
            std::cerr << "widescreen gameplay render mismatch\n";
            return 1;
        }
        for (const int expanded_width : {
                 hocus::ultrawide_frame_width,
                 hocus::super_ultrawide_frame_width}) {
            hocus::GameLevel expanded_game(
                archive, {1, 1}, 0, 0, false, expanded_width);
            expanded_game.place_player(106, 36 * 16);
            const auto expanded_scene = expanded_game.render();
            const int horizontal_focus_half_tiles =
                (expanded_width + 8) / 16;
            const int expected_camera_x =
                (106 - horizontal_focus_half_tiles) * 8;
            const int expected_player_x = 106 * 8 - expected_camera_x;
            if (expanded_game.viewport_width() != expanded_width ||
                expanded_scene.image.width != expanded_width ||
                expanded_scene.image.height != hocus::game_frame_height ||
                expanded_scene.camera_pixel_x != expected_camera_x ||
                !sprite_matches(expanded_scene.image, player,
                                expected_player_x, 80)) {
                std::cerr << "expanded aspect gameplay render mismatch\n";
                return 1;
            }
        }
        // 0BA5:29C5 retains camera state and advances at most one eight-pixel
        // X unit and one 16-pixel row toward the previous tick's player
        // position. Rendering must not clamp directly to the new position.
        hocus::GameLevel camera_tracking(archive);
        camera_tracking.place_player(106, 36 * 16);
        const auto camera_initial = camera_tracking.render();
        camera_tracking.tick({false, true});
        const auto camera_after_move = camera_tracking.render();
        camera_tracking.tick({false, true});
        const auto camera_after_follow = camera_tracking.render();
        const auto camera_interpolation_start = camera_tracking.render(0.0);
        const auto camera_interpolation_mid = camera_tracking.render(0.5);
        if (camera_initial.camera_pixel_x != 688 ||
            camera_initial.camera_pixel_y != 496 ||
            camera_after_move.camera_pixel_x != 688 ||
            camera_after_move.camera_pixel_y != 496 ||
            !sprite_matches(camera_after_move.image, player, 160, 80) ||
            camera_after_follow.camera_pixel_x != 696 ||
            camera_after_follow.camera_pixel_y != 512 ||
            camera_interpolation_start.camera_pixel_x != 688 ||
            camera_interpolation_start.camera_pixel_y != 496 ||
            camera_interpolation_mid.camera_pixel_x != 692 ||
            camera_interpolation_mid.camera_pixel_y != 504) {
            std::cerr << "incremental camera tracking mismatch\n";
            return 1;
        }
        // Configurable binding six follows DS:70A4 and increments CF36;
        // binding seven follows DS:70A3 and decrements it. Both are held
        // controls and retain their offset instead of snapping to five.
        hocus::GameLevel camera_scroll(archive);
        hocus::InputState scroll_up;
        scroll_up.scroll_up = true;
        hocus::InputState scroll_down;
        scroll_down.scroll_down = true;
        for (int step = 0; step < 5; ++step) {
            camera_scroll.tick(scroll_up);
        }
        if (camera_scroll.camera_focus_rows() != 8) {
            std::cerr << "dedicated camera scroll-up mismatch\n";
            return 1;
        }
        for (int step = 0; step < 10; ++step) {
            camera_scroll.tick(scroll_down);
        }
        if (camera_scroll.camera_focus_rows() != 0) {
            std::cerr << "dedicated camera scroll-down mismatch\n";
            return 1;
        }
        camera_scroll.tick({});
        if (camera_scroll.camera_focus_rows() != 5) {
            std::cerr << "camera focus reset mismatch\n";
            return 1;
        }
        hocus::GameLevel restored_score(archive, {1, 1}, 12345);
        if (restored_score.progress().score != 12345) {
            std::cerr << "restored campaign score mismatch\n";
            return 1;
        }
        if (game.progress().health != 100 || game.progress().firepower != 1 ||
            game.progress().crystals != 0 || game.progress().score != 0 ||
            game.progress().total_crystals != 5 ||
            game.progress().total_treasures != 86 ||
            game.event_at(18, 57) != 5 ||
            game.background_tile_at(18, 57) != 40) {
            std::cerr << "E1L1 initial event state mismatch\n";
            return 1;
        }

        hocus::GameLevel crystal_flash(archive, {1, 1});
        crystal_flash.place_player(18 * 2, 57 * 16);
        crystal_flash.tick({});
        const auto crystal_flash_frame = crystal_flash.render().image;
        // 0x6F is GAMEPAL.PAL's dark-red 69,0,0 entry. BACK01.PCX leaves
        // that unused embedded-palette slot black, which previously made the
        // entire crystal flash look like a rendering outage.
        constexpr std::uint32_t crystal_flash_colour = 0x00450000;
        const auto flash_pixels = std::count(
            crystal_flash_frame.pixels.begin(),
            crystal_flash_frame.pixels.begin() + 320 * 160,
            crystal_flash_colour);
        // 0BA5:4DC9 substitutes the solid flash for the two tile-layer
        // copies, but the player and transient effects are painted afterward.
        if (crystal_flash.crystal_flash_ticks() != 8 ||
            crystal_flash_colour == 0 ||
            crystal_flash_frame.pixels[0] != crystal_flash_colour ||
            flash_pixels <= 300 * 150 || flash_pixels >= 320 * 160) {
            std::cerr << "crystal viewport-flash mismatch\n";
            return 1;
        }
        crystal_flash.tick({});
        if (crystal_flash.crystal_flash_ticks() != 7) {
            std::cerr << "crystal viewport-flash timing mismatch\n";
            return 1;
        }

        constexpr int visual_variants[9] = {0, 0, 1, 1, 2, 2, 3, 3, 3};
        constexpr int music_variants[4][9] = {
            {3, 3, 4, 4, 0, 0, 2, 2, 2},
            {9, 9, 7, 7, 8, 8, 1, 1, 1},
            {4, 4, 0, 0, 7, 7, 6, 6, 6},
            {2, 2, 8, 8, 9, 9, 5, 5, 5},
        };
        std::set<int> observed_events;
        std::set<int> observed_enemy_behaviours;
        for (int episode = 1; episode <= 4; ++episode) {
            for (int number = 1; number <= 9; ++number) {
                const int index = (episode - 1) * 9 + number - 1;
                const auto start = archive.read(131 + index);
                const int start_column = start[2] | (start[3] << 8);
                const int start_row = start[4] | (start[5] << 8);
                const auto raw_events = archive.read(563 + index);
                int crystals = 0;
                int treasures = 0;
                for (std::size_t offset = 0; offset < raw_events.size(); offset += 2) {
                    const int event = raw_events[offset] | (raw_events[offset + 1] << 8);
                    observed_events.insert(event);
                    crystals += event == 5;
                    treasures += event >= 0 && event <= 3;
                }
                const auto raw_enemies = archive.read(383 + index);
                for (std::size_t offset = 0; offset < raw_enemies.size();
                     offset += 24) {
                    const auto sprite = static_cast<std::int16_t>(
                        raw_enemies[offset] | (raw_enemies[offset + 1] << 8));
                    if (sprite < 0) {
                        continue;
                    }
                    observed_enemy_behaviours.insert(
                        static_cast<std::int16_t>(
                            raw_enemies[offset + 22] |
                            (raw_enemies[offset + 23] << 8)));
                }

                hocus::GameLevel selected(archive, {episode, number});
                const auto selected_scene = selected.render();
                const int expected_variant = (episode - 1) * 4 +
                                             visual_variants[number - 1];
                if (selected.level_id() != hocus::LevelId{episode, number} ||
                    selected.level_name() != "E" + std::to_string(episode) +
                                             "L" + std::to_string(number) ||
                    selected.backdrop_asset_index() != 89 + expected_variant ||
                    selected.tileset_asset_index() != 105 + expected_variant ||
                    selected.music_asset_index() !=
                        600 + music_variants[episode - 1][number - 1] ||
                    selected_scene.image.width != 320 ||
                    selected_scene.image.height != 200 ||
                    selected_scene.player_pixel_x != start_column * 16 ||
                    selected_scene.player_pixel_y != start_row * 16 ||
                    selected.progress().total_crystals != crystals ||
                    selected.progress().total_treasures != treasures) {
                    std::cerr << "registered level selection mismatch at E"
                              << episode << 'L' << number << '\n';
                    return 1;
                }
            }
        }
        const std::set<int> expected_events = {
            0, 1, 2, 3, 4, 5, 6, 7, 9, 10, 12, 13, 14, 15, 16,
            23, 24, 25, 26, 27, 28, 29,
            33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45,
            46, 47, 48, 49, 56, 57, 58,
            81, 82, 83, 84, 85, 86, 87, 88,
            106, 107, 108, 109, 110,
            116, 117, 118, 119, 120, 121, 122, 123, 124, 125, 126,
            127, 128, 129, 130, 131, 132, 133, 134, 135, 136, 137,
            138, 139, 140, 30000,
        };
        const std::set<int> expected_enemy_behaviours = {
            0, 1, 2, 3, 4, 5, 6, 8, 99,
        };
        if (observed_events != expected_events ||
            observed_enemy_behaviours != expected_enemy_behaviours) {
            std::cerr << "registered event/enemy selector inventory mismatch\n";
            return 1;
        }

        hocus::GameLevel teleporter(archive, {1, 5});
        // E1L5 event 28: .003 trigger cell 11518 (238,47) to cell 13014
        // (54,54). The second endpoint retains event 28 but cannot retrigger.
        teleporter.place_player(238 * 2, 47 * 16);
        teleporter.tick({false, true, true, false, true});
        const auto teleporter_sounds = teleporter.take_sound_events();
        if (!teleporter.teleporting() || teleporter.teleporter_ticks() != 2 ||
            teleporter.event_at(238, 47) != 30000 ||
            teleporter.event_at(54, 54) != 28 ||
            teleporter.player().x_half_tiles != 238 * 2 ||
            teleporter.player().y_pixels != 47 * 16 ||
            teleporter.active_projectile_count() != 0 ||
            std::count(teleporter_sounds.begin(), teleporter_sounds.end(), 5) != 1) {
            std::cerr << "teleporter activation/input-lock mismatch\n";
            return 1;
        }
        for (int tick = 0; tick < 500 && teleporter.teleporting(); ++tick) {
            teleporter.tick({false, true, true, false, true});
        }
        if (teleporter.teleporting() ||
            teleporter.player().x_half_tiles != 54 * 2 ||
            teleporter.player().y_pixels != 54 * 16 ||
            teleporter.event_at(54, 54) != 28 ||
            teleporter.active_projectile_count() != 0) {
            std::cerr << "teleporter travel/completion mismatch\n";
            return 1;
        }

        hocus::GameLevel super_shot(archive, {1, 4});
        super_shot.place_player(112 * 2, 53 * 16); // Event 7: add shot.
        super_shot.tick({});
        if (super_shot.progress().firepower != 2) {
            std::cerr << "firepower pickup fixture mismatch\n";
            return 1;
        }
        // Types 4, 5, and 11 require the primary (center-column) dispatcher
        // sample. Merely touching this super-shot with Hocus's right column
        // leaves it in place.
        super_shot.place_player(201, 25 * 16);
        super_shot.tick({});
        if (super_shot.progress().super_shot_ticks != 0 ||
            super_shot.event_at(102, 25) != 10) {
            std::cerr << "side-sample super-shot mismatch\n";
            return 1;
        }
        super_shot.place_player(102 * 2, 25 * 16); // Event 10.
        super_shot.tick({});
        if (super_shot.progress().firepower != 10 ||
            super_shot.progress().super_shot_ticks != 599) {
            std::cerr << "super-shot activation mismatch\n";
            return 1;
        }
        // A second pickup refreshes the counter without replacing the saved
        // pre-power-up firepower value.
        super_shot.place_player(145 * 2, 50 * 16);
        super_shot.tick({});
        for (int tick = 0; tick < 597; ++tick) {
            super_shot.tick({});
        }
        if (super_shot.progress().firepower != 10 ||
            super_shot.progress().super_shot_ticks != 2) {
            std::cerr << "super-shot countdown mismatch\n";
            return 1;
        }
        super_shot.tick({});
        if (super_shot.progress().firepower != 2 ||
            super_shot.progress().super_shot_ticks != 0) {
            std::cerr << "super-shot firepower restoration mismatch\n";
            return 1;
        }

        // 0BA5:3E91 upgrades the saved firepower as well as the visible,
        // ten-shot-capped value while the timed super-shot is active.
        hocus::GameLevel super_shot_upgrade(archive, {1, 4});
        super_shot_upgrade.place_player(102 * 2, 25 * 16);
        super_shot_upgrade.tick({});
        super_shot_upgrade.place_player(112 * 2, 53 * 16);
        super_shot_upgrade.tick({});
        for (int tick = 0; tick < 598; ++tick) {
            super_shot_upgrade.tick({});
        }
        if (super_shot_upgrade.progress().super_shot_ticks != 0 ||
            super_shot_upgrade.progress().firepower != 2) {
            std::cerr << "super-shot saved-firepower upgrade mismatch\n";
            return 1;
        }

        // E2L4 event 9 arms CF0E/CF10. CF0E stays at 25 while the jump is
        // available, then emits exactly 25 release updates after consumption.
        hocus::GameLevel super_jump_pickup(archive, {2, 4});
        super_jump_pickup.place_player(66 * 2, 25 * 16);
        super_jump_pickup.tick({});
        for (int tick = 0; tick < 5; ++tick) {
            super_jump_pickup.tick({});
        }
        if (!super_jump_pickup.progress().super_jump_available ||
            super_jump_pickup.super_jump_effect_ticks() != 25) {
            std::cerr << "armed super-jump effect mismatch\n";
            return 1;
        }
        super_jump_pickup.tick({false, false, true});
        if (super_jump_pickup.progress().super_jump_available) {
            std::cerr << "super-jump consumption mismatch\n";
            return 1;
        }
        for (int tick = 0; tick < 25; ++tick) {
            super_jump_pickup.tick({});
        }
        if (super_jump_pickup.super_jump_effect_ticks() != 0) {
            std::cerr << "super-jump release-effect timing mismatch\n";
            return 1;
        }

        // Item definition type 11 at 0BA5:3DD6 adds three laser shots,
        // restarts the five-tick indicator blink, consumes the event, and
        // emits sound nine.
        hocus::GameLevel laser_pickup(archive, {2, 5});
        laser_pickup.place_player(137 * 2, 6 * 16);
        laser_pickup.tick({});
        const auto laser_pickup_sounds = laser_pickup.take_sound_events();
        const auto laser_pickup_scene = laser_pickup.render();
        const int indicator_x = laser_pickup_scene.player_pixel_x -
                                laser_pickup_scene.camera_pixel_x + 4;
        const int indicator_y = laser_pickup_scene.player_pixel_y -
                                laser_pickup_scene.camera_pixel_y - 14 - 2 * 14;
        if (laser_pickup.progress().laser_shots != 3 ||
            laser_pickup.event_at(137, 6) != 30000 ||
            laser_pickup.laser_indicator_ticks() != 4 ||
            !laser_pickup.laser_indicator_visible() ||
            std::count(laser_pickup_sounds.begin(),
                       laser_pickup_sounds.end(), 9) != 1 ||
            !sprite_matches(laser_pickup_scene.image, player_laser_vertical,
                            indicator_x, indicator_y)) {
            std::cerr << "laser pickup/indicator offset mismatch\n";
            return 1;
        }

        hocus::GameLevel cheats(archive, {1, 1});
        cheats.apply_cheat(hocus::CheatCode::both_keys);
        cheats.apply_cheat(hocus::CheatCode::rapid_fire);
        cheats.apply_cheat(hocus::CheatCode::laser_shots);
        cheats.apply_cheat(hocus::CheatCode::laser_shots);
        cheats.apply_cheat(hocus::CheatCode::full_health);
        if (!cheats.progress().silver_key || !cheats.progress().gold_key ||
            cheats.progress().firepower != 5 ||
            cheats.progress().super_shot_ticks != 600 ||
            cheats.progress().laser_shots != 3 ||
            cheats.progress().health != 100 ||
            !cheats.laser_indicator_visible() ||
            cheats.laser_indicator_ticks() != 5) {
            std::cerr << "keyboard ISR cheat-state mismatch\n";
            return 1;
        }
        for (int tick = 0; tick < 5; ++tick) {
            cheats.tick({});
        }
        if (cheats.laser_indicator_visible() ||
            cheats.laser_indicator_ticks() != 5 ||
            cheats.progress().super_shot_ticks != 595) {
            std::cerr << "stored-laser blink timing mismatch\n";
            return 1;
        }

        // The native Ctrl+Alt+F1 menu turns the four recovered one-shot
        // cheats into persistent toggles and adds a fifth mid-air jump toggle
        // without changing their typed-code behavior above.
        hocus::GameLevel persistent_cheats(archive, {1, 1});
        for (const auto cheat : {
                 hocus::CheatCode::full_health,
                 hocus::CheatCode::both_keys,
                 hocus::CheatCode::rapid_fire,
                 hocus::CheatCode::laser_shots,
                 hocus::CheatCode::midair_jump,
             }) {
            persistent_cheats.set_cheat_enabled(cheat, true);
            if (!persistent_cheats.cheat_enabled(cheat)) {
                std::cerr << "persistent cheat enable-state mismatch\n";
                return 1;
            }
        }
        persistent_cheats.tick({false, false, false, false, true});
        if (persistent_cheats.progress().health != 100 ||
            !persistent_cheats.progress().silver_key ||
            !persistent_cheats.progress().gold_key ||
            persistent_cheats.progress().firepower != 5 ||
            persistent_cheats.progress().super_shot_ticks != 600 ||
            persistent_cheats.progress().laser_shots != 3 ||
            persistent_cheats.active_projectile_count() != 1 ||
            !persistent_cheats.projectiles().front().laser) {
            std::cerr << "persistent cheat enforcement mismatch\n";
            return 1;
        }
        persistent_cheats.place_player(148, 58 * 16);
        persistent_cheats.tick({});
        if (persistent_cheats.progress().health != 100 ||
            persistent_cheats.progress().damage_cooldown != 10 ||
            persistent_cheats.progress().super_shot_ticks != 600 ||
            persistent_cheats.progress().laser_shots != 3) {
            std::cerr << "persistent cheat post-collision mismatch\n";
            return 1;
        }
        persistent_cheats.set_cheat_enabled(
            hocus::CheatCode::rapid_fire, false);
        if (persistent_cheats.cheat_enabled(hocus::CheatCode::rapid_fire) ||
            persistent_cheats.progress().super_shot_ticks != 0 ||
            persistent_cheats.progress().firepower != 1) {
            std::cerr << "persistent rapid-fire disable mismatch\n";
            return 1;
        }

        hocus::GameLevel elevator_up(archive, {3, 1});
        // E3L1 uses elevator tiles 78/79. This pair at (28,31) has clearance
        // in both directions; sample its right tile to exercise pair recovery.
        elevator_up.place_player(29 * 2, 29 * 16);
        elevator_up.tick({false, false, false, true, false});
        if (elevator_up.player().y_pixels != 28 * 16 ||
            elevator_up.main_tile_at(28, 30) != 78 ||
            elevator_up.main_tile_at(29, 30) != 79 ||
            elevator_up.main_tile_at(28, 31) != 0xFF ||
            elevator_up.main_tile_at(29, 31) != 0xFF) {
            std::cerr << "upward elevator movement mismatch\n";
            return 1;
        }

        hocus::GameLevel elevator_down(archive, {3, 1});
        elevator_down.place_player(28 * 2, 29 * 16);
        elevator_down.tick({false, false, false, false, false, true});
        if (elevator_down.player().y_pixels != 30 * 16 ||
            elevator_down.main_tile_at(28, 32) != 78 ||
            elevator_down.main_tile_at(29, 32) != 79 ||
            elevator_down.main_tile_at(28, 31) != 0xFF ||
            elevator_down.main_tile_at(29, 31) != 0xFF) {
            std::cerr << "downward elevator movement mismatch\n";
            return 1;
        }
        game.tick({false, true, false});
        if (game.player().x_half_tiles != 7 || game.player().y_pixels != 912) {
            std::cerr << "right movement mismatch\n";
            return 1;
        }
        game.tick({true, false, false}); // turn west without moving
        game.tick({true, false, false});
        if (game.player().x_half_tiles != 6 || !game.player().facing_west) {
            std::cerr << "turn/left movement mismatch\n";
            return 1;
        }
        game.tick({false, false, true});
        int minimum_y = game.player().y_pixels;
        for (int tick = 1; tick < 19; ++tick) {
            game.tick({});
            minimum_y = std::min(minimum_y, game.player().y_pixels);
        }
        if (minimum_y != 864 || game.player().y_pixels != 912 ||
            game.player().jumping || game.player().falling) {
            std::cerr << "normal jump trajectory mismatch\n";
            return 1;
        }

        hocus::GameLevel midair_jump(archive);
        midair_jump.set_cheat_enabled(hocus::CheatCode::midair_jump, true);
        midair_jump.tick({false, false, true});
        midair_jump.tick({});
        const int before_second_jump = midair_jump.player().y_pixels;
        midair_jump.tick({false, false, true});
        if (!midair_jump.player().jumping || midair_jump.player().falling ||
            midair_jump.player().jump_index != 1 ||
            midair_jump.player().y_pixels != before_second_jump - 16) {
            std::cerr << "mid-air jump activation mismatch\n";
            return 1;
        }
        // Holding Jump must continue the new arc rather than resetting its
        // first -16 step every update; another jump requires a release edge.
        midair_jump.tick({false, false, true});
        if (midair_jump.player().jump_index != 2 ||
            midair_jump.player().y_pixels != before_second_jump - 24) {
            std::cerr << "mid-air jump edge handling mismatch\n";
            return 1;
        }
        // Restarting an airborne arc can bypass the registered landing sample
        // and leave Hocus's lower half inside the floor. The native recovery
        // is cheat-gated and chooses the nearest clear pixel above it.
        hocus::GameLevel floor_eject(archive);
        floor_eject.place_player(6, 58 * 16);
        floor_eject.set_cheat_enabled(
            hocus::CheatCode::midair_jump, true);
        floor_eject.tick({});
        if (floor_eject.player().y_pixels != 57 * 16 ||
            floor_eject.player().jumping || floor_eject.player().falling) {
            std::cerr << "mid-air jump floor eject mismatch\n";
            return 1;
        }
        hocus::GameLevel registered_floor_overlap(archive);
        registered_floor_overlap.place_player(6, 58 * 16);
        registered_floor_overlap.tick({});
        if (registered_floor_overlap.player().y_pixels != 58 * 16) {
            std::cerr << "disabled floor eject altered registered path\n";
            return 1;
        }
        // The native mid-air extension must also lift the DOS jump-lifetime
        // camera freeze. Otherwise chained jumps can leave Hocus outside the
        // viewport and make a later landing place him inside unseen floors.
        hocus::GameLevel midair_camera(archive);
        midair_camera.place_player(106, 36 * 16);
        midair_camera.set_cheat_enabled(
            hocus::CheatCode::midair_jump, true);
        const int camera_before_jump = midair_camera.render().camera_pixel_y;
        midair_camera.tick({false, false, true});
        midair_camera.tick({});
        const int camera_during_ascent =
            midair_camera.render().camera_pixel_y;
        bool camera_followed_descent_while_jumping = false;
        int preceding_camera_y = camera_during_ascent;
        for (int tick = 0; tick < 17; ++tick) {
            midair_camera.tick({});
            const int camera_y = midair_camera.render().camera_pixel_y;
            if (midair_camera.player().jumping &&
                camera_y > preceding_camera_y) {
                camera_followed_descent_while_jumping = true;
            }
            preceding_camera_y = camera_y;
        }
        if (camera_during_ascent >= camera_before_jump ||
            !camera_followed_descent_while_jumping) {
            std::cerr << "mid-air jump camera tracking mismatch\n";
            return 1;
        }
        hocus::GameLevel traversal(archive);
        for (int tick = 0; tick < 100; ++tick) {
            traversal.tick({false, true, false});
        }
        // This path climbs the opening stairs, descends to the floor, and stops
        // against the first impassable wall using the recovered step-up flags.
        if (traversal.player().x_half_tiles != 84 ||
            traversal.player().y_pixels != 912) {
            std::cerr << "E1L1 collision traversal mismatch\n";
            return 1;
        }

        hocus::GameLevel shooting(archive);
        shooting.tick({false, false, false, false, true});
        if (shooting.active_projectile_count() != 1 ||
            !shooting.projectiles().front().active ||
            shooting.projectiles().front().vertical ||
            shooting.projectiles().front().facing_west) {
            std::cerr << "projectile spawn mismatch\n";
            return 1;
        }
        hocus::GameLevel laser_render(archive);
        laser_render.apply_cheat(hocus::CheatCode::laser_shots);
        laser_render.tick({false, false, false, false, true});
        const auto laser_scene = laser_render.render();
        const auto& active_laser = laser_render.projectiles().front();
        if (!active_laser.active || !active_laser.laser ||
            laser_render.progress().laser_shots != 2 ||
            !sprite_matches(laser_scene.image, player_laser,
                            active_laser.x_pixels - laser_scene.camera_pixel_x,
                            active_laser.y_pixels - laser_scene.camera_pixel_y)) {
            std::cerr << "laser firing/ammo/raster mismatch\n";
            return 1;
        }
        const auto bolt_scene = shooting.render();
        const auto& active_bolt = shooting.projectiles().front();
        const auto bolt_source = static_cast<std::size_t>(
            std::distance(player_bolt.opacity.begin(), first_bolt_pixel));
        const int bolt_source_x = static_cast<int>(bolt_source % player_bolt.width);
        const int bolt_source_y = static_cast<int>(bolt_source / player_bolt.width);
        const int bolt_x = active_bolt.x_pixels - bolt_scene.camera_pixel_x +
                           bolt_source_x;
        const int bolt_y = active_bolt.y_pixels - bolt_scene.camera_pixel_y +
                           bolt_source_y;
        if (bolt_x < 0 || bolt_x >= bolt_scene.image.width ||
            bolt_y < 0 || bolt_y >= bolt_scene.image.height ||
            bolt_scene.image.pixels[static_cast<std::size_t>(bolt_y) *
                                    bolt_scene.image.width + bolt_x] !=
                player_bolt.pixels[bolt_source]) {
            std::cerr << "lightning-bolt render mismatch\n";
            return 1;
        }
        shooting.tick({false, false, false, false, true});
        if (shooting.active_projectile_count() != 1 ||
            std::none_of(shooting.spell_trails().begin(),
                         shooting.spell_trails().end(),
                         [](const hocus::SpellTrailState& trail) {
                             return trail.active;
                         })) {
            std::cerr << "normal fire edge/cap mismatch\n";
            return 1;
        }

        hocus::GameLevel vertical_shot(archive);
        vertical_shot.tick({false, false, false, true, true});
        if (vertical_shot.active_projectile_count() != 1 ||
            !vertical_shot.projectiles().front().vertical) {
            std::cerr << "vertical projectile mismatch\n";
            return 1;
        }

        hocus::GameLevel breakable(archive);
        breakable.place_player(278, 52 * 16);
        if (breakable.main_tile_at(142, 52) != 0x51) {
            std::cerr << "breakable tile fixture mismatch\n";
            return 1;
        }
        breakable.tick({false, false, false, false, true});
        for (int tick = 0; tick < 4; ++tick) {
            breakable.tick({});
        }
        const auto& break_twinkle = breakable.twinkles().front();
        if (breakable.main_tile_at(142, 52) != 0xFF ||
            breakable.active_projectile_count() != 0 ||
            break_twinkle.ticks != 6 || break_twinkle.x_pixels != 208 ||
            break_twinkle.y_pixels != 80 ||
            breakable.explosion_bursts().front().ticks != 0) {
            std::cerr << "projectile breakable-wall collision mismatch: tile="
                      << static_cast<int>(breakable.main_tile_at(142, 52))
                      << " shots=" << breakable.active_projectile_count()
                      << " twinkle=(" << break_twinkle.x_pixels << ','
                      << break_twinkle.y_pixels << ',' << break_twinkle.ticks
                      << ") explosion="
                      << breakable.explosion_bursts().front().ticks
                      << " camera=(" << breakable.render().camera_pixel_x
                      << ',' << breakable.render().camera_pixel_y << ")\n";
            return 1;
        }
        // The main layer was already presented before 5806 broke the tile.
        // Its old pixels survive this frame and disappear on the next one.
        const auto e1_tiles = hocus::decode_pcx(
            archive.read(105), e1_active_palette);
        hocus::GameLevel breakable_snapshot(archive);
        breakable_snapshot.place_player(278, 52 * 16);
        breakable_snapshot.tick({false, false, false, false, true});
        for (int tick = 0;
             tick < 20 && breakable_snapshot.main_tile_at(142, 52) != 0xFF;
             ++tick) {
            breakable_snapshot.tick({});
        }
        const auto break_frame = breakable_snapshot.render();
        const int break_destination_x = 142 * 16 - break_frame.camera_pixel_x;
        const int break_destination_y = 52 * 16 - break_frame.camera_pixel_y;
        const auto tile_row_matches = [&](const hocus::DecodedImage& frame) {
            for (int x = 0; x < 16; ++x) {
                const auto source = static_cast<std::size_t>(64 + 14) *
                                        e1_tiles.width + 16 + x;
                const auto destination = static_cast<std::size_t>(
                    break_destination_y + 14) * frame.width +
                    break_destination_x + x;
                if (frame.pixels[destination] != e1_tiles.pixels[source]) {
                    return false;
                }
            }
            return true;
        };
        if (!tile_row_matches(break_frame.image)) {
            std::cerr << "late breakable-tile draw snapshot mismatch\n";
            return 1;
        }
        breakable_snapshot.tick({});
        if (tile_row_matches(breakable_snapshot.render().image)) {
            std::cerr << "breakable-tile next-frame removal mismatch\n";
            return 1;
        }

        hocus::GameLevel solid_wall(archive);
        // Row 51 is empty through column 140 and solid from column 141, so
        // the east branch's current-row pair hits an ordinary wall at once.
        solid_wall.place_player(278, 51 * 16);
        solid_wall.tick({false, false, false, false, true});
        const auto& solid_burst = solid_wall.explosion_bursts().front();
        if (solid_wall.active_projectile_count() != 0 ||
            solid_burst.ticks != 16 ||
            solid_burst.particles.front().x_pixels != 192 ||
            solid_burst.particles.front().y_pixels != 98 ||
            solid_wall.twinkles().front().ticks != 0) {
            std::cerr << "projectile solid-wall collision mismatch: shots="
                      << solid_wall.active_projectile_count() << " burst=("
                      << solid_burst.particles.front().x_pixels << ','
                      << solid_burst.particles.front().y_pixels << ','
                      << solid_burst.ticks << ") twinkle="
                      << solid_wall.twinkles().front().ticks << '\n';
            return 1;
        }

        hocus::GameLevel west_wall(archive);
        // At row 55 columns 119-120 are clear with a floor below, while
        // column 117 is solid. One left update selects the west projectile
        // branch; its second shot update reaches that wall pair.
        west_wall.place_player(238, 55 * 16);
        west_wall.tick({true});
        west_wall.tick({false, false, false, false, true});
        west_wall.tick({});
        const auto& west_burst = west_wall.explosion_bursts().front();
        if (west_wall.active_projectile_count() != 0 ||
            west_burst.ticks != 16 ||
            west_burst.particles.front().x_pixels != 137 ||
            west_burst.particles.front().y_pixels != 90 ||
            west_wall.twinkles().front().ticks != 0) {
            std::cerr << "west projectile wall collision mismatch: shots="
                      << west_wall.active_projectile_count() << " burst=("
                      << west_burst.particles.front().x_pixels << ','
                      << west_burst.particles.front().y_pixels << ','
                      << west_burst.ticks << ") facing="
                      << west_wall.player().facing_west << '\n';
            return 1;
        }

        hocus::GameLevel ceiling_shot(archive);
        // The clear player cell at 53,51 has a solid ceiling at 53,50.
        ceiling_shot.place_player(106, 51 * 16);
        ceiling_shot.tick({false, false, false, true, true});
        const auto& ceiling_burst = ceiling_shot.explosion_bursts().front();
        if (ceiling_shot.active_projectile_count() != 0 ||
            ceiling_burst.ticks != 16 ||
            ceiling_burst.particles.front().x_pixels != 172 ||
            ceiling_burst.particles.front().y_pixels != 82 ||
            ceiling_shot.twinkles().front().ticks != 0) {
            std::cerr << "vertical projectile wall collision mismatch: shots="
                      << ceiling_shot.active_projectile_count() << " burst=("
                      << ceiling_burst.particles.front().x_pixels << ','
                      << ceiling_burst.particles.front().y_pixels << ','
                      << ceiling_burst.ticks << ") vertical="
                      << ceiling_shot.projectiles().front().vertical << '\n';
            return 1;
        }

        hocus::GameLevel switches(archive);
        switches.place_player(286, 52 * 16); // Switch event 33 at 143,52.
        if (switches.background_tile_at(143, 52) != 64 ||
            switches.main_tile_at(145, 52) == 0xFF) {
            std::cerr << "switch fixture mismatch\n";
            return 1;
        }
        switches.tick({false, false, false, true});
        const auto switch_sounds = switches.take_sound_events();
        if (switches.background_tile_at(143, 52) != 65 ||
            switches.event_at(143, 52) != 33 ||
            switches.main_tile_at(145, 52) == 0xFF ||
            std::count(switch_sounds.begin(), switch_sounds.end(), 7) != 1) {
            std::cerr << "switch activation mismatch\n";
            return 1;
        }
        hocus::GameLevel repeat_switch(archive);
        repeat_switch.place_player(286, 52 * 16);
        repeat_switch.tick({false, false, false, true});
        (void)repeat_switch.take_sound_events();
        repeat_switch.tick({});
        repeat_switch.tick({false, false, false, true});
        const auto repeat_switch_sounds = repeat_switch.take_sound_events();
        if (repeat_switch.background_tile_at(143, 52) != 64 ||
            std::count(repeat_switch_sounds.begin(),
                       repeat_switch_sounds.end(), 6) != 1) {
            std::cerr << "completed-switch reuse mismatch\n";
            return 1;
        }
        // 0BA5:2C81 runs every other game update. Its shared 25-step timer
        // forces any cells not already selected by the one-in-three dissolve
        // to their final state on the 50th update.
        for (int tick = 0; tick < 49; ++tick) {
            switches.tick({});
        }
        if (switches.main_tile_at(145, 52) != 0xFF ||
            switches.main_tile_at(156, 53) == 0xFF) {
            std::cerr << "switch door mutation mismatch\n";
            return 1;
        }
        // The DOS updater touches only the 21 visible columns. A pending
        // off-screen cell remains armed and takes its one-in-three path once
        // the camera later reaches it.
        switches.place_player(156 * 2, 52 * 16);
        for (int tick = 0;
             tick < 200 && switches.main_tile_at(156, 53) != 0xFF; ++tick) {
            switches.tick({});
        }
        if (switches.main_tile_at(156, 53) != 0xFF) {
            std::cerr << "off-screen switch mutation mismatch\n";
            return 1;
        }

        // 0BA5:2C81 also advances `.001` mode-one background animations in
        // the visible 21 x 10 tile window on every other update.
        hocus::GameLevel animated_tile(archive);
        animated_tile.place_player(47 * 2, 40 * 16);
        if (animated_tile.background_tile_at(47, 35) != 40) {
            std::cerr << "animated-tile fixture mismatch\n";
            return 1;
        }
        animated_tile.tick({});
        if (animated_tile.background_tile_at(47, 35) != 40) {
            std::cerr << "animated-tile phase mismatch\n";
            return 1;
        }
        animated_tile.tick({});
        if (animated_tile.background_tile_at(47, 35) != 41) {
            std::cerr << "animated-tile advance mismatch\n";
            return 1;
        }

        hocus::GameLevel locked_gate(archive);
        locked_gate.place_player(98, 57 * 16); // Silver keyhole event 81.
        locked_gate.tick({});
        if (locked_gate.event_at(49, 57) != 81 ||
            locked_gate.main_tile_at(53, 57) == 0xFF) {
            std::cerr << "locked keyhole mismatch\n";
            return 1;
        }
        locked_gate.place_player(34, 46 * 16); // Collect silver key at 17,46.
        locked_gate.tick({});
        if (!locked_gate.progress().silver_key) {
            std::cerr << "silver key pickup mismatch\n";
            return 1;
        }
        // 41AA does not apply the center-column test: the keyhole in column
        // 49 unlocks while sampled in Hocus's right-hand column.
        locked_gate.place_player(96, 57 * 16);
        locked_gate.tick({});
        const auto gate_sounds = locked_gate.take_sound_events();
        if (locked_gate.progress().silver_key ||
            locked_gate.event_at(49, 57) != 30000 ||
            locked_gate.background_tile_at(49, 57) != 7 ||
            locked_gate.main_tile_at(53, 57) == 0xFF ||
            std::count(gate_sounds.begin(), gate_sounds.end(), 7) != 1) {
            std::cerr << "keyhole unlock mismatch\n";
            return 1;
        }
        for (int tick = 0; tick < 19; ++tick) {
            locked_gate.tick({});
        }
        if (locked_gate.main_tile_at(53, 57) != 0xFF ||
            locked_gate.main_tile_at(53, 58) != 0xFF) {
            std::cerr << "keyhole door mutation mismatch\n";
            return 1;
        }

        // Events 56..80 are a separate, previously omitted `.005` family.
        // E1L4 event 57 restores insert-data tiles 68..71 on row 53.
        hocus::GameLevel insertion_gate(archive, {1, 4});
        if (insertion_gate.event_at(60, 52) != 57 ||
            insertion_gate.main_tile_at(68, 53) != 0xFF) {
            std::cerr << "insertion-gate fixture mismatch\n";
            return 1;
        }
        insertion_gate.place_player(120, 52 * 16);
        insertion_gate.tick({});
        if (insertion_gate.event_at(60, 52) != 30000 ||
            insertion_gate.main_tile_at(68, 53) != 0xFF) {
            std::cerr << "insertion-gate activation mismatch\n";
            return 1;
        }
        for (int tick = 0; tick < 19; ++tick) {
            insertion_gate.tick({});
        }
        if (insertion_gate.main_tile_at(68, 53) == 0xFF ||
            insertion_gate.main_tile_at(71, 53) != 0xFF) {
            std::cerr << "insertion-gate restore mismatch\n";
            return 1;
        }
        insertion_gate.place_player(71 * 2, 52 * 16);
        for (int tick = 0;
             tick < 200 && insertion_gate.main_tile_at(71, 53) == 0xFF;
             ++tick) {
            insertion_gate.tick({});
        }
        if (insertion_gate.main_tile_at(71, 53) == 0xFF) {
            std::cerr << "off-screen insertion-gate mismatch\n";
            return 1;
        }

        hocus::GameLevel enemy_trigger(archive);
        enemy_trigger.place_player(60, 56 * 16); // Trigger 116 at 30,56.
        enemy_trigger.tick({});
        if (enemy_trigger.active_enemy_count() != 3) {
            std::cerr << "enemy trigger count mismatch\n";
            return 1;
        }
        const auto spawn_sounds = enemy_trigger.take_sound_events();
        if (std::count(spawn_sounds.begin(), spawn_sounds.end(), 14) != 1) {
            std::cerr << "enemy spawn-sound throttle mismatch\n";
            return 1;
        }
        // 0BA5:06F5 advances the sound deadline by five 140 Hz timer ticks,
        // while each game update consumes at least six. It therefore permits
        // another activation sound on the next update, while still merging
        // all enemies created during one update into a single cue.
        hocus::GameLevel spawn_sound_clock(archive);
        spawn_sound_clock.place_player(60, 56 * 16);
        spawn_sound_clock.tick({});
        (void)spawn_sound_clock.take_sound_events();
        spawn_sound_clock.place_player(470, 0);
        spawn_sound_clock.tick({});
        if (spawn_sound_clock.active_enemy_count() != 0) {
            std::cerr << "enemy spawn-sound release fixture mismatch\n";
            return 1;
        }
        spawn_sound_clock.place_player(60, 56 * 16);
        spawn_sound_clock.tick({});
        const auto repeated_spawn_sounds =
            spawn_sound_clock.take_sound_events();
        if (std::count(repeated_spawn_sounds.begin(),
                       repeated_spawn_sounds.end(), 14) != 1) {
            std::cerr << "enemy spawn-sound 140 Hz deadline mismatch\n";
            return 1;
        }
        const auto& spawned = enemy_trigger.enemies();
        if (!spawned[0].active || spawned[0].type != 0 ||
            spawned[0].spawn_cell != 57 * 240 + 21 ||
            spawned[0].x_pixels != 21 * 16 ||
            spawned[0].y_pixels != 57 * 16 ||
            spawned[0].health != 1 || spawned[0].behaviour != 0 ||
            spawned[0].x_velocity_pixels != 4 ||
            spawned[0].spawn_ticks != 19 ||
            enemy_trigger.event_at(21, 57) != 106) {
            std::cerr << "enemy spawn state mismatch\n";
            return 1;
        }
        // The enemy spawn word at DS:BF84 routes 0BA5:126C directly to the
        // randomized Twinks/countdown tail. MORPH is Hocus's teleporter
        // artwork and must never be drawn over a spawning enemy.
        const auto spawning_scene = enemy_trigger.render();
        const auto morph_east = hocus::decode_sprite_frame(
            archive.read(130), 3, 0, false, e1_active_palette);
        const auto morph_west = hocus::decode_sprite_frame(
            archive.read(130), 3, 0, true, e1_active_palette);
        const int spawn_screen_x = spawned[0].x_pixels -
                                   spawning_scene.camera_pixel_x;
        const int spawn_screen_y = spawned[0].y_pixels -
                                   spawning_scene.camera_pixel_y;
        if (sprite_matches(spawning_scene.image, morph_east,
                           spawn_screen_x, spawn_screen_y) ||
            sprite_matches(spawning_scene.image, morph_west,
                           spawn_screen_x, spawn_screen_y)) {
            std::cerr << "enemy spawn incorrectly uses Hocus morph raster\n";
            return 1;
        }
        // Remaining on any cell carrying trigger 116 must not duplicate the
        // three live spawn markers while their morph timers run.
        for (int tick = 0; tick < 19; ++tick) {
            enemy_trigger.tick({});
        }
        if (enemy_trigger.active_enemy_count() != 3 ||
            spawned[0].spawn_ticks != 0 ||
            spawned[0].animation_delay_ticks != 1 ||
            spawned[0].animation_delay_reset_ticks != 1 ||
            spawned[0].attack_pose_ticks != 0) {
            std::cerr << "enemy morph/deduplication mismatch\n";
            return 1;
        }

        // 0BA5:05DB adds twice the zero-based skill to every non-negative
        // enemy extra-hit count. Sentinels remain untouched.
        hocus::GameLevel moderate_enemy(archive, {1, 1}, 0, 1);
        hocus::GameLevel hard_enemy(archive, {1, 1}, 0, 2);
        moderate_enemy.place_player(60, 56 * 16);
        hard_enemy.place_player(60, 56 * 16);
        moderate_enemy.tick({});
        hard_enemy.tick({});
        if (moderate_enemy.enemies()[0].health != 3 ||
            hard_enemy.enemies()[0].health != 5) {
            std::cerr << "enemy difficulty-health mismatch\n";
            return 1;
        }

        // 0BA5:00D3 runs before 0BA5:126C. An enemy that moves into Hocus
        // cannot damage him until the following update's collision pass.
        hocus::GameLevel body_phase(archive);
        body_phase.place_player(60, 56 * 16);
        body_phase.tick({});
        for (int tick = 0; tick < 19; ++tick) {
            body_phase.tick({});
        }
        const int body_enemy_index = 2;
        const int body_start_x =
            body_phase.enemies()[body_enemy_index].x_pixels;
        body_phase.place_player((body_start_x + awful_al.width - 8) / 8,
                                body_phase.enemies()[body_enemy_index].y_pixels);
        body_phase.tick({});
        if (body_phase.progress().health != 100 ||
            body_phase.enemies()[body_enemy_index].x_pixels != body_start_x ||
            body_phase.enemies()[body_enemy_index].animation_delay_ticks != 0) {
            std::cerr << "enemy initial animation-gate mismatch: "
                      << "health=" << body_phase.progress().health
                      << " start_x=" << body_start_x
                      << " end_x="
                      << body_phase.enemies()[body_enemy_index].x_pixels
                      << " width=" << awful_al.width << '\n';
            return 1;
        }
        body_phase.tick({});
        if (body_phase.progress().health != 100 ||
            body_phase.enemies()[body_enemy_index].x_pixels != body_start_x + 4 ||
            body_phase.enemies()[body_enemy_index].animation_delay_ticks != 1) {
            std::cerr << "enemy body pre-movement collision-order mismatch\n";
            return 1;
        }
        body_phase.tick({});
        if (body_phase.progress().health != 96 ||
            body_phase.progress().damage_cooldown != 20) {
            std::cerr << "enemy body deferred-contact mismatch\n";
            return 1;
        }

        enemy_trigger.place_player(42, 57 * 16);
        enemy_trigger.tick({});
        if (enemy_trigger.progress().health != 96 ||
            enemy_trigger.progress().damage_cooldown != 20 ||
            spawned[0].x_pixels != 21 * 16) {
            std::cerr << "enemy patrol/contact mismatch\n";
            return 1;
        }

        hocus::GameLevel enemy_shot(archive);
        enemy_shot.place_player(60, 56 * 16);
        enemy_shot.tick({});
        for (int tick = 0; tick < 19; ++tick) {
            enemy_shot.tick({});
        }
        // The DOS collision helper tests the spell's X point rather than its
        // 16-pixel artwork box. Position the shot so that point crosses the
        // moving enemy on the firing update.
        enemy_shot.place_player(40, 57 * 16);
        enemy_shot.tick({false, false, false, false, true});
        if (enemy_shot.enemies()[0].health != 1 ||
            enemy_shot.active_projectile_count() != 1) {
            std::cerr << "Hocus-shot pre-movement collision-order mismatch\n";
            return 1;
        }
        enemy_shot.tick({});
        if (enemy_shot.enemies()[0].health != 0 ||
            !enemy_shot.enemies()[0].active ||
            enemy_shot.active_projectile_count() != 0 ||
            enemy_shot.event_at(21, 57) != 106 ||
            enemy_shot.enemies()[0].hit_cooldown != 9 ||
            enemy_shot.enemies()[0].hit_flash_masked) {
            std::cerr << "enemy projectile-damage mismatch\n";
            return 1;
        }
        enemy_shot.tick({});
        if (enemy_shot.enemies()[0].hit_cooldown != 8 ||
            !enemy_shot.enemies()[0].hit_flash_masked) {
            std::cerr << "enemy hit-flash counter/parity mismatch\n";
            return 1;
        }

        // 0BA5:03AF sends a laser to the death path regardless of the
        // enemy's remaining hit count, and 0365 leaves that projectile alive
        // so it can pierce later enemy slots.
        hocus::GameLevel laser_enemy(archive);
        laser_enemy.place_player(60, 56 * 16);
        laser_enemy.tick({});
        for (int tick = 0; tick < 19; ++tick) {
            laser_enemy.tick({});
        }
        laser_enemy.place_player(40, 57 * 16);
        laser_enemy.apply_cheat(hocus::CheatCode::laser_shots);
        laser_enemy.tick({false, false, false, false, true});
        laser_enemy.tick({});
        if (laser_enemy.enemies()[0].active ||
            laser_enemy.active_enemy_count() != 2 ||
            laser_enemy.event_at(21, 57) != 30000 ||
            laser_enemy.active_projectile_count() != 1 ||
            !laser_enemy.projectiles().front().laser) {
            std::cerr << "laser instant-kill/piercing mismatch\n";
            return 1;
        }

        hocus::GameLevel enemy_fire(archive);
        enemy_fire.place_player(60, 56 * 16);
        enemy_fire.tick({});
        for (int tick = 0; tick < 19; ++tick) {
            enemy_fire.tick({});
        }
        bool observed_enemy_projectile = false;
        bool observed_embedded_projectile_art = false;
        for (int tick = 0; tick < 30 && enemy_fire.progress().health == 100;
             ++tick) {
            enemy_fire.tick({});
            observed_enemy_projectile = observed_enemy_projectile ||
                enemy_fire.active_enemy_projectile_count() != 0;
            for (const auto& projectile : enemy_fire.enemy_projectiles()) {
                if (!projectile.active) {
                    continue;
                }
                const auto& expected = projectile.owner_type == 0
                    ? awful_al
                    : hocus::decode_sprite_record_info(archive.read(130), 6);
                if (projectile.width != expected.projectile_width * 4 ||
                    projectile.height != expected.projectile_height + 3 ||
                    projectile.sprite_frame < expected.projectile_first ||
                    projectile.sprite_frame > expected.projectile_last) {
                    std::cerr << "embedded enemy projectile state mismatch\n";
                    return 1;
                }
                observed_embedded_projectile_art = true;
            }
        }
        if (!observed_enemy_projectile || !observed_embedded_projectile_art ||
            enemy_fire.progress().health != 96 ||
            enemy_fire.progress().damage_cooldown == 0) {
            std::cerr << "enemy projectile spawn/damage mismatch\n";
            return 1;
        }

        // E3L3 trigger 116 contains a type-three definition whose health is
        // the -2 kill-all sentinel used by 0BA5:0267 -> 0BA5:01B6.
        hocus::GameLevel sentinel_enemy(archive, {3, 3});
        sentinel_enemy.place_player(231 * 2, 42 * 16);
        sentinel_enemy.tick({});
        for (int tick = 0; tick < 19; ++tick) {
            sentinel_enemy.tick({});
        }
        const auto sentinel = std::find_if(
            sentinel_enemy.enemies().begin(), sentinel_enemy.enemies().end(),
            [](const hocus::EnemyState& enemy) {
                return enemy.active && enemy.type == 3 && enemy.health == -2;
            });
        if (sentinel == sentinel_enemy.enemies().end() ||
            sentinel_enemy.active_enemy_count() < 2) {
            std::cerr << "special sentinel enemy spawn mismatch\n";
            return 1;
        }
        const int next_sentinel_x =
            sentinel->x_pixels + sentinel->x_velocity_pixels;
        const int next_sentinel_y =
            sentinel->y_pixels + sentinel->y_velocity_pixels;
        // Horizontal fire reaches player_x+24 on this tick. Round toward the
        // interior of the enemy because player X is quantized to eight pixels.
        const int firing_x_half_tiles =
            (next_sentinel_x - 24 + 7) / 8;
        sentinel_enemy.place_player(firing_x_half_tiles,
                                    next_sentinel_y - 10);
        sentinel_enemy.tick({false, false, false, false, true});
        if (sentinel_enemy.active_enemy_count() < 2) {
            std::cerr << "sentinel collision was not deferred to next tick\n";
            return 1;
        }
        sentinel_enemy.tick({});
        if (sentinel_enemy.active_enemy_count() != 0 ||
            std::any_of(sentinel_enemy.enemies().begin(),
                        sentinel_enemy.enemies().end(),
                        [](const hocus::EnemyState& enemy) {
                            return enemy.active;
                        })) {
            std::cerr << "special sentinel kill-all mismatch\n";
            return 1;
        }

        struct EnemyModeFixture {
            hocus::LevelId level;
            int trigger_column;
            int trigger_row;
            int type;
            int behaviour;
            int health;
        };
        constexpr EnemyModeFixture enemy_modes[] = {
            {{1, 1}, 33, 43, 1, 1, 2},
            {{1, 1}, 53, 48, 2, 2, 1},
            {{1, 3}, 90, 49, 1, 3, 1},
            {{1, 9}, 117, 42, 3, 4, 34},
            {{2, 7}, 212, 55, 0, 5, 1},
            {{3, 5}, 53, 51, 0, 6, 1},
            {{3, 9}, 20, 54, 0, 8, 200},
        };
        hocus::GameLevel later_enemy_art(archive, {2, 7});
        hocus::GameLevel final_boss_art(archive, {4, 9});
        if (enemy_trigger.enemy_sprite_record_for_type(0) != 4 ||
            later_enemy_art.enemy_sprite_record_for_type(0) != 20 ||
            final_boss_art.enemy_sprite_record_for_type(0) != 39) {
            std::cerr << "per-level enemy source-sprite mapping mismatch\n";
            return 1;
        }
        for (const auto& fixture : enemy_modes) {
            hocus::GameLevel selected_enemy(archive, fixture.level);
            selected_enemy.place_player(fixture.trigger_column * 2,
                                        fixture.trigger_row * 16);
            selected_enemy.tick({});
            const auto found = std::find_if(
                selected_enemy.enemies().begin(),
                selected_enemy.enemies().end(),
                [&fixture](const hocus::EnemyState& enemy) {
                    return enemy.active && enemy.type == fixture.type;
                });
            if (found == selected_enemy.enemies().end() ||
                found->behaviour != fixture.behaviour ||
                found->health != fixture.health ||
                found->spawn_ticks != 19) {
                std::cerr << "enemy definition/mode mismatch at E"
                          << fixture.level.episode << 'L'
                          << fixture.level.number << '\n';
                return 1;
            }
            if (fixture.behaviour == 4) {
                const auto sounds = selected_enemy.take_sound_events();
                if (found->facing_west || found->sprite_frame != 0 ||
                    found->x_velocity_pixels != 0 ||
                    found->y_velocity_pixels != 0 ||
                    std::find(sounds.begin(), sounds.end(), 11) ==
                        sounds.end()) {
                    std::cerr << "selector-four creation state mismatch\n";
                    return 1;
                }
            } else if (fixture.behaviour == 5 && found->sprite_frame != 0) {
                std::cerr << "selector-five creation frame mismatch\n";
                return 1;
            }
        }

        hocus::GameLevel episode_three_boss(archive, {3, 9});
        episode_three_boss.place_player(40, 54 * 16);
        episode_three_boss.tick({}); // Trigger 116 at 20,54; boss at 30,54.
        for (int tick = 0; tick < 19; ++tick) {
            episode_three_boss.tick({});
        }
        const auto boss = std::find_if(
            episode_three_boss.enemies().begin(),
            episode_three_boss.enemies().end(),
            [](const hocus::EnemyState& enemy) {
                return enemy.active && enemy.behaviour == 8;
            });
        if (boss == episode_three_boss.enemies().end() ||
            boss->anchor_x_pixels != 30 * 16 ||
            boss->behaviour_frame != 0 || boss->spawn_ticks != 0) {
            std::cerr << "episode-three boss initial state mismatch\n";
            return 1;
        }
        bool boss_moved = false;
        bool boss_changed_frame = false;
        bool boss_fired = false;
        for (int tick = 0; tick < 500; ++tick) {
            const int pre_update_boss_x = boss->x_pixels;
            episode_three_boss.tick({});
            boss_moved = boss_moved || boss->x_pixels != boss->anchor_x_pixels;
            boss_changed_frame = boss_changed_frame ||
                boss->behaviour_frame != 0;
            boss_fired = boss_fired ||
                episode_three_boss.active_enemy_projectile_count() != 0;
            if (boss->x_pixels != pre_update_boss_x &&
                (!boss->draw_state_valid ||
                 boss->draw_x_pixels != pre_update_boss_x)) {
                std::cerr << "episode-three boss pre-action render mismatch\n";
                return 1;
            }
            for (const auto& projectile :
                 episode_three_boss.enemy_projectiles()) {
                if (projectile.active && projectile.delay_ticks != 0) {
                    std::cerr << "episode-three boss projectile delay mismatch\n";
                    return 1;
                }
            }
            if (!boss->active ||
                std::abs(boss->x_pixels - boss->anchor_x_pixels) > 20 ||
                boss->behaviour_frame < 0 || boss->behaviour_frame > 3 ||
                boss->facing_west != (boss->behaviour_frame >= 2)) {
                std::cerr << "episode-three boss controller bounds mismatch\n";
                return 1;
            }
        }
        if (!boss_moved || !boss_changed_frame || !boss_fired) {
            std::cerr << "episode-three boss movement/fire mismatch\n";
            return 1;
        }

        // 0BA5:012B makes contact with a current extra-hit count above 20
        // immediately fatal on the selectable skills.
        hocus::GameLevel boss_contact(archive, {3, 9});
        boss_contact.place_player(40, 54 * 16);
        boss_contact.tick({});
        for (int tick = 0; tick < 19; ++tick) {
            boss_contact.tick({});
        }
        const auto contact_boss = std::find_if(
            boss_contact.enemies().begin(), boss_contact.enemies().end(),
            [](const hocus::EnemyState& enemy) {
                return enemy.active && enemy.behaviour == 8;
            });
        if (contact_boss == boss_contact.enemies().end()) {
            std::cerr << "boss-contact setup mismatch\n";
            return 1;
        }
        boss_contact.place_player(contact_boss->x_pixels / 8,
                                  contact_boss->y_pixels);
        boss_contact.tick({});
        if (boss_contact.progress().health != 0 ||
            boss_contact.death_ticks() != 0) {
            std::cerr << "boss-contact fatality mismatch\n";
            return 1;
        }
        boss_contact.tick({});
        if (boss_contact.death_ticks() != 89) {
            std::cerr << "deferred death-sequence initialization mismatch\n";
            return 1;
        }

        hocus::GameLevel episode_four_boss(archive, {4, 9});
        if (episode_four_boss.active_enemy_count() != 1 ||
            !episode_four_boss.enemies()[0].active ||
            episode_four_boss.enemies()[0].behaviour != 99 ||
            episode_four_boss.enemies()[0].boss_stage != 0 ||
            episode_four_boss.enemies()[0].health != 800 ||
            episode_four_boss.enemies()[0].x_pixels != 128 * 16 ||
            episode_four_boss.enemies()[0].y_pixels != 54 * 16 ||
            episode_four_boss.enemies()[0].spawn_ticks != 20) {
            std::cerr << "episode-four boss initial state mismatch\n";
            return 1;
        }
        episode_four_boss.tick({});
        const auto boss_health_scene = episode_four_boss.render();
        if (!episode_four_boss.enemies()[0].health_bar_visible ||
            boss_health_scene.image.pixels[152 * 320 + 4] != palette[0x6F] ||
            boss_health_scene.image.pixels[154 * 320 + 315] != palette[0x68]) {
            std::cerr << "boss health-bar raster mismatch\n";
            return 1;
        }
        std::array<bool, 4> observed_boss_stages{};
        constexpr int boss_super_shots[][2] = {
            {108, 39}, {154, 50}, {107, 51}, {139, 51},
            {66, 57}, {109, 57}, {111, 57},
        };
        constexpr int boss_health_pickups[][2] = {
            {151, 55}, {153, 55}, {68, 57}, {70, 57}, {72, 57},
            {74, 57}, {76, 57}, {78, 57}, {80, 57}, {137, 57},
            {139, 57}, {145, 57}, {147, 57},
        };
        int next_super_shot = 0;
        int next_health_pickup = 0;
        bool boss4_fired = false;
        int fight_tick = 0;
        for (; fight_tick < 5000; ++fight_tick) {
            const auto current = std::find_if(
                episode_four_boss.enemies().begin(),
                episode_four_boss.enemies().end(),
                [](const hocus::EnemyState& enemy) {
                    return enemy.active && enemy.behaviour == 99;
                });
            if (current == episode_four_boss.enemies().end()) {
                break;
            }
            if (current->boss_stage < 0 || current->boss_stage > 3) {
                std::cerr << "episode-four boss stage bounds mismatch\n";
                return 1;
            }
            observed_boss_stages[static_cast<std::size_t>(
                current->boss_stage)] = true;
            if (current->boss_stage <= 1 &&
                current->health >= 500 &&
                episode_four_boss.progress().super_shot_ticks < 450 &&
                next_super_shot < static_cast<int>(std::size(boss_super_shots))) {
                episode_four_boss.place_player(
                    boss_super_shots[next_super_shot][0] * 2,
                    boss_super_shots[next_super_shot][1] * 16);
                ++next_super_shot;
                episode_four_boss.tick({});
                continue;
            }
            if (current->boss_stage <= 1 &&
                current->health >= 500 &&
                episode_four_boss.progress().health < 100 &&
                next_health_pickup <
                    static_cast<int>(std::size(boss_health_pickups))) {
                episode_four_boss.place_player(
                    boss_health_pickups[next_health_pickup][0] * 2,
                    boss_health_pickups[next_health_pickup][1] * 16);
                ++next_health_pickup;
                episode_four_boss.tick({});
                continue;
            }
            episode_four_boss.place_player(
                std::max(0, (current->x_pixels - 24) / 8),
                std::max(0, current->y_pixels - 10));
            const bool rapid_fire =
                episode_four_boss.progress().super_shot_ticks > 0;
            episode_four_boss.tick(
                {false, false, false, false,
                 rapid_fire || fight_tick % 2 == 0});
            boss4_fired = boss4_fired ||
                episode_four_boss.active_enemy_projectile_count() != 0;
            for (const auto& projectile :
                 episode_four_boss.enemy_projectiles()) {
                if (projectile.active && projectile.delay_ticks != 0) {
                    std::cerr << "episode-four boss projectile delay mismatch\n";
                    return 1;
                }
            }
        }
        if (fight_tick == 5000 || !boss4_fired ||
            !std::all_of(observed_boss_stages.begin(),
                         observed_boss_stages.end(),
                         [](bool observed) { return observed; }) ||
            episode_four_boss.active_enemy_count() != 0) {
            std::cerr << "episode-four boss staged fight mismatch: tick="
                      << fight_tick << " fired=" << boss4_fired
                      << " stages=" << observed_boss_stages[0]
                      << observed_boss_stages[1]
                      << observed_boss_stages[2]
                      << observed_boss_stages[3]
                      << " active="
                      << episode_four_boss.active_enemy_count()
                      << " player_health="
                      << episode_four_boss.progress().health
                      << " death=" << episode_four_boss.death_ticks();
            const auto remaining = std::find_if(
                episode_four_boss.enemies().begin(),
                episode_four_boss.enemies().end(),
                [](const hocus::EnemyState& enemy) {
                    return enemy.active && enemy.behaviour == 99;
                });
            if (remaining != episode_four_boss.enemies().end()) {
                std::cerr << " stage=" << remaining->boss_stage
                          << " health=" << remaining->health
                          << " morph=" << remaining->spawn_ticks;
            }
            std::cerr << '\n';
            return 1;
        }

        hocus::GameLevel completion(archive);
        constexpr int crystal_cells[][2] = {
            {47, 35}, {95, 42}, {62, 56}, {140, 56}, {18, 57},
        };
        for (const auto& cell : crystal_cells) {
            completion.place_player(cell[0] * 2, cell[1] * 16);
            completion.tick({});
        }
        if (completion.progress().crystals != 5 ||
            completion.level_complete() ||
            completion.level_complete_ticks() != 89 ||
            completion.progress().invisibility_ticks < 29000) {
            std::cerr << "level completion trigger mismatch\n";
            return 1;
        }

        // Completion calls the same eight-slot enemy purge. Collect four
        // distant crystals first, then leave the nearby one until enemies are
        // live so off-camera release cannot mask the behavior.
        hocus::GameLevel completion_with_enemies(archive);
        constexpr int remote_crystals[][2] = {
            {47, 35}, {95, 42}, {62, 56}, {140, 56},
        };
        for (const auto& cell : remote_crystals) {
            completion_with_enemies.place_player(cell[0] * 2,
                                                  cell[1] * 16);
            completion_with_enemies.tick({});
        }
        completion_with_enemies.place_player(60, 56 * 16);
        completion_with_enemies.tick({});
        for (int tick = 0; tick < 19; ++tick) {
            completion_with_enemies.tick({});
        }
        if (completion_with_enemies.active_enemy_count() != 3) {
            std::cerr << "completion enemy setup mismatch\n";
            return 1;
        }
        completion_with_enemies.place_player(18 * 2, 57 * 16);
        completion_with_enemies.tick({});
        if (completion_with_enemies.level_complete_ticks() != 89 ||
            completion_with_enemies.active_enemy_count() != 0 ||
            completion_with_enemies.event_at(21, 57) != 30000 ||
            completion_with_enemies.event_at(23, 57) != 30000 ||
            completion_with_enemies.event_at(25, 57) != 30000) {
            std::cerr << "completion enemy purge mismatch\n";
            return 1;
        }
        const int completion_x = completion.player().x_half_tiles;
        for (int tick = 0; tick < 88; ++tick) {
            completion.tick({false, true, false});
        }
        const auto completion_sounds = completion.take_sound_events();
        if (!completion.level_complete() ||
            completion.level_complete_ticks() != 1 ||
            completion.player().x_half_tiles != completion_x ||
            std::find(completion_sounds.begin(), completion_sounds.end(), 9) ==
                completion_sounds.end()) {
            std::cerr << "level completion countdown/lock mismatch\n";
            return 1;
        }

        hocus::GameLevel events(archive);
        hocus::GameLevel moderate_damage(archive, {}, 0, 1);
        hocus::GameLevel hard_damage(archive, {}, 0, 2);
        moderate_damage.place_player(148, 58 * 16);
        hard_damage.place_player(148, 58 * 16);
        moderate_damage.tick({});
        hard_damage.tick({});
        if (moderate_damage.progress().health != 88 ||
            hard_damage.progress().health != 84 ||
            moderate_damage.damage_amount() != 12 ||
            hard_damage.damage_amount() != 16 ||
            moderate_damage.elapsed_ticks() != 1) {
            std::cerr << "skill damage/timer mismatch\n";
            return 1;
        }
        events.place_player(106, 36 * 16); // Ruby at map cell 53,36.
        events.tick({});
        const auto& ruby_sparkle = events.pickup_sparkles().front();
        if (events.progress().score != 100 || events.progress().treasures != 1 ||
            events.event_at(53, 36) != 30000 ||
            events.background_tile_at(53, 36) != 7 ||
            ruby_sparkle.frame != 0 || ruby_sparkle.mirrored ||
            ruby_sparkle.ticks != 16 || ruby_sparkle.x_pixels != 159 ||
            ruby_sparkle.y_pixels != 62) {
            std::cerr << "treasure event mismatch\n";
            return 1;
        }
        const auto ruby_frame = events.render().image;
        const auto e1_bullet = hocus::decode_planar_img(
            archive.read(14), e1_active_palette);
        bool sparkle_first_frame_matches = false;
        for (int y = 0; y < 15 && !sparkle_first_frame_matches; ++y) {
            for (int x = 0; x < 16; ++x) {
                const auto source = static_cast<std::size_t>(y) *
                                        e1_bullet.width + x;
                if (e1_bullet.pixels[source] == e1_active_palette.front()) {
                    continue;
                }
                sparkle_first_frame_matches =
                    ruby_frame.pixels[static_cast<std::size_t>(64 + y) * 320 +
                                      159 + x] == e1_bullet.pixels[source];
                if (sparkle_first_frame_matches) {
                    break;
                }
            }
        }
        if (!sparkle_first_frame_matches) {
            std::cerr << "pickup-sparkle pre-movement raster mismatch: draw=("
                      << ruby_sparkle.draw_x_pixels << ','
                      << ruby_sparkle.draw_y_pixels << ") visible="
                      << ruby_sparkle.draw_visible << '\n';
            return 1;
        }
        const auto treasure_sounds = events.take_sound_events();
        if (treasure_sounds.size() != 1 ||
            (treasure_sounds.front() != 2 && treasure_sounds.front() != 3)) {
            std::cerr << "treasure sound event mismatch\n";
            return 1;
        }

        // 0BA5:3AC0 emits logical sound nine in addition to the ordinary
        // randomized treasure cue when the last treasure is collected.
        hocus::GameLevel final_treasure(archive);
        bool reached_final_treasure = false;
        for (int row = 0; row < 60 && !reached_final_treasure; ++row) {
            for (int column = 0; column < 240 && !reached_final_treasure;
                 ++column) {
                if (final_treasure.event_at(column, row) > 3) {
                    continue;
                }
                final_treasure.dismiss_message();
                final_treasure.apply_cheat(hocus::CheatCode::full_health);
                final_treasure.place_player(column * 2, row * 16);
                final_treasure.tick({});
                const auto sounds = final_treasure.take_sound_events();
                if (final_treasure.progress().treasures ==
                    final_treasure.progress().total_treasures) {
                    reached_final_treasure =
                        std::find(sounds.begin(), sounds.end(), 9) !=
                            sounds.end() &&
                        std::any_of(sounds.begin(), sounds.end(),
                                    [](const int sound) {
                                        return sound == 2 || sound == 3;
                                    }) &&
                        final_treasure.level_number_flash_ticks() == 1;
                }
            }
        }
        if (!reached_final_treasure) {
            std::cerr << "final-treasure sound/flash mismatch\n";
            return 1;
        }
        events.tick({});
        if (events.pickup_sparkles().front().ticks != 15 ||
            events.pickup_sparkles().front().y_pixels != 44) {
            std::cerr << "pickup sparkle timing mismatch: ticks="
                      << events.pickup_sparkles().front().ticks << " y="
                      << events.pickup_sparkles().front().y_pixels
                      << " camera_y=" << events.render().camera_pixel_y
                      << " player_y=" << events.player().y_pixels << '\n';
            return 1;
        }
        (void)events.take_sound_events();

        events.place_player(36, 57 * 16); // Crystal at map cell 18,57.
        events.tick({});
        if (events.progress().crystals != 1 ||
            events.event_at(18, 57) != 30000 ||
            events.background_tile_at(18, 57) != 7) {
            std::cerr << "crystal event mismatch\n";
            return 1;
        }
        const auto crystal_sounds = events.take_sound_events();
        if (std::find(crystal_sounds.begin(), crystal_sounds.end(), 4) ==
            crystal_sounds.end()) {
            std::cerr << "crystal sound event mismatch\n";
            return 1;
        }

        events.place_player(148, 58 * 16); // Persistent lava at 74,58.
        events.tick({});
        if (events.progress().health != 96 ||
            events.progress().damage_cooldown != 10 ||
            events.event_at(74, 58) != 14) {
            std::cerr << "hazard event mismatch\n";
            return 1;
        }
        for (int tick = 0; tick < 9; ++tick) {
            events.place_player(148, 58 * 16);
            events.tick({});
        }
        if (events.progress().health != 96 ||
            events.progress().damage_cooldown != 1) {
            std::cerr << "hazard cooldown mismatch\n";
            return 1;
        }
        events.place_player(148, 58 * 16);
        events.tick({});
        if (events.progress().health != 92 ||
            events.progress().damage_cooldown != 10) {
            std::cerr << "hazard repeat mismatch\n";
            return 1;
        }

        hocus::GameLevel death(archive);
        for (int tick = 0; tick < 300 && death.progress().health > 0; ++tick) {
            death.place_player(148, 58 * 16);
            death.tick({});
        }
        if (death.progress().health != 0 || death.death_ticks() != 89 ||
            death.player_dead() ||
            std::count_if(death.explosion_bursts().begin(),
                          death.explosion_bursts().end(),
                          [](const hocus::ExplosionBurstState& burst) {
                              return burst.ticks > 0;
                          }) != 2 ||
            std::count_if(death.twinkles().begin(), death.twinkles().end(),
                          [](const hocus::TwinkleState& twinkle) {
                              return twinkle.ticks > 0;
                          }) != 2) {
            std::cerr << "death countdown trigger mismatch\n";
            return 1;
        }
        const int death_x = death.player().x_half_tiles;
        for (int tick = 0; tick < 88; ++tick) {
            death.tick({false, true, false, false, true});
        }
        if (!death.player_dead() || death.death_ticks() != 1 ||
            death.player().x_half_tiles != death_x) {
            std::cerr << "death countdown/lock mismatch\n";
            return 1;
        }

        hocus::GameLevel full_health(archive);
        full_health.place_player(108, 39 * 16);
        full_health.tick({});
        if (full_health.progress().health != 100 ||
            full_health.event_at(54, 39) != 4) {
            std::cerr << "full-health pickup mismatch\n";
            return 1;
        }

        hocus::GameLevel side_hazard(archive);
        side_hazard.place_player(145, 58 * 16); // Lava at 74,58 is offset +1.
        side_hazard.tick({});
        if (side_hazard.progress().health != 100 ||
            side_hazard.progress().damage_cooldown != 0) {
            std::cerr << "side-sample hazard mismatch\n";
            return 1;
        }

        // The original event dispatcher covers all six map cells occupied by
        // Hocus. These fixtures guard the right and lower samples that make a
        // visibly overlapped pickup collectible.
        hocus::GameLevel right_footprint(archive);
        right_footprint.place_player(148, 58 * 16); // Take four lava damage.
        right_footprint.tick({});
        right_footprint.place_player(106, 39 * 16); // Heal is one cell right.
        right_footprint.tick({});
        if (right_footprint.progress().health != 100 ||
            right_footprint.event_at(54, 39) != 30000) {
            std::cerr << "right-side pickup footprint mismatch\n";
            return 1;
        }

        hocus::GameLevel lower_footprint(archive);
        lower_footprint.place_player(148, 58 * 16); // Take four lava damage.
        lower_footprint.tick({});
        lower_footprint.place_player(140, 38 * 16); // Heal is one row below.
        lower_footprint.tick({});
        if (lower_footprint.progress().health != 100 ||
            lower_footprint.event_at(70, 39) != 30000) {
            std::cerr << "lower pickup footprint mismatch\n";
            return 1;
        }

        events.place_player(108, 39 * 16); // Damaged Hocus collects the heal.
        events.tick({});
        const auto heal_sounds = events.take_sound_events();
        if (events.progress().health != 100 ||
            events.event_at(54, 39) != 30000 ||
            events.background_tile_at(54, 39) != 7 ||
            std::count(heal_sounds.begin(), heal_sounds.end(), 1) != 1) {
            std::cerr << "health pickup mismatch\n";
            return 1;
        }

        events.place_player(32, 53 * 16); // Wizard at 16,53.
        events.tick({false, false, false, true});
        const int message_paused_tick = events.elapsed_ticks();
        if (events.active_message().size() != 3 ||
            events.active_message().front() != "So... another brave apprentice!" ||
            events.event_at(16, 53) != 15 ||
            events.background_tile_at(16, 53) != 99) {
            std::cerr << "wizard message mismatch\n";
            return 1;
        }
        events.tick({}); // Release Fire.
        events.tick({false, false, false, true});
        if (!events.active_message().empty() ||
            events.elapsed_ticks() != message_paused_tick) {
            std::cerr << "wizard message dismissal mismatch\n";
            return 1;
        }

        hocus::GameLevel treasure_hud(archive);
        std::vector<std::pair<int, int>> treasure_cells;
        for (int row = 0; row < 60; ++row) {
            for (int column = 0; column < 240; ++column) {
                if (treasure_hud.event_at(column, row) <= 3) {
                    treasure_cells.emplace_back(column, row);
                }
            }
        }
        for (const auto [column, row] : treasure_cells) {
            if (treasure_hud.progress().treasures ==
                treasure_hud.progress().total_treasures) {
                break;
            }
            treasure_hud.place_player(column * 2, row * 16);
            treasure_hud.tick({});
        }
        const auto hud_stuff = hocus::decode_planar_img(
            archive.read(11), palette);
        const auto hud_region_matches = [&hud_stuff](
                const hocus::DecodedImage& image, const int source_x) {
            for (int y = 0; y < 8; ++y) {
                for (int x = 0; x < 8; ++x) {
                    if (image.pixels[static_cast<std::size_t>(182 + y) * 320 +
                                     296 + x] !=
                        hud_stuff.pixels[static_cast<std::size_t>(y) *
                                             hud_stuff.width + source_x + x]) {
                        return false;
                    }
                }
            }
            return true;
        };
        if (treasure_hud.progress().treasures !=
                treasure_hud.progress().total_treasures ||
            treasure_hud.level_number_flash_ticks() != 1 ||
            !hud_region_matches(treasure_hud.render().image, 80)) {
            std::cerr << "final-treasure HUD flash start mismatch: got "
                      << treasure_hud.progress().treasures << '/'
                      << treasure_hud.progress().total_treasures
                      << " phase=" << treasure_hud.level_number_flash_ticks()
                      << " raster="
                      << hud_region_matches(treasure_hud.render().image, 80)
                      << '\n';
            return 1;
        }
        for (int tick = 0; tick < 9; ++tick) {
            treasure_hud.tick({});
        }
        if (treasure_hud.level_number_flash_ticks() != 10 ||
            !hud_region_matches(treasure_hud.render().image, 8)) {
            std::cerr << "final-treasure HUD visible phase mismatch\n";
            return 1;
        }
        for (int tick = 0; tick < 11; ++tick) {
            treasure_hud.tick({});
        }
        if (treasure_hud.level_number_flash_ticks() != 1 ||
            !hud_region_matches(treasure_hud.render().image, 80)) {
            std::cerr << "final-treasure HUD wrap mismatch\n";
            return 1;
        }

        hocus::GameLevel player_sound(archive);
        player_sound.tick({false, false, false, false, true});
        const auto firing_sounds = player_sound.take_sound_events();
        if (std::find(firing_sounds.begin(), firing_sounds.end(), 0) ==
            firing_sounds.end()) {
            std::cerr << "player firing sound event mismatch\n";
            return 1;
        }

        std::cout << "validated archive, audio, rendering, motion, projectiles, switches, keys, enemies, pickups, hazards, completion, and messages\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
