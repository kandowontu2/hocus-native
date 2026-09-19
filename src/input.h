#pragma once

#include "level.h"

#include <cstdint>

namespace hocus {

struct DosFrontendJoystickSample {
    bool left{};
    bool right{};
    bool up{};
    bool down{};
    bool button_one{};
    bool button_two{};
    bool button_three{};
    bool button_four{};
};

// Platform-neutral copy of the XInput gamepad fields used by the Win32 front
// end. Keeping this mapping out of main.cpp makes controller behavior
// deterministic and testable without importing an XInput DLL into hocus_core.
struct XInputControllerSample {
    std::int16_t left_thumb_x{};
    std::int16_t left_thumb_y{};
    std::uint8_t right_trigger{};
    std::uint16_t buttons{};
};

inline constexpr std::uint16_t xinput_dpad_up = 0x0001;
inline constexpr std::uint16_t xinput_dpad_down = 0x0002;
inline constexpr std::uint16_t xinput_dpad_left = 0x0004;
inline constexpr std::uint16_t xinput_dpad_right = 0x0008;
inline constexpr std::uint16_t xinput_start = 0x0010;
inline constexpr std::uint16_t xinput_back = 0x0020;
inline constexpr std::uint16_t xinput_left_shoulder = 0x0100;
inline constexpr std::uint16_t xinput_right_shoulder = 0x0200;
inline constexpr std::uint16_t xinput_a = 0x1000;
inline constexpr std::uint16_t xinput_b = 0x2000;
inline constexpr std::uint16_t xinput_x = 0x4000;
inline constexpr int xinput_left_thumb_deadzone = 7849;
inline constexpr int xinput_trigger_threshold = 30;

[[nodiscard]] constexpr DosFrontendJoystickSample xinput_directions(
    const XInputControllerSample sample) noexcept {
    DosFrontendJoystickSample result;
    result.left = (sample.buttons & xinput_dpad_left) != 0 ||
                  sample.left_thumb_x < -xinput_left_thumb_deadzone;
    result.right = (sample.buttons & xinput_dpad_right) != 0 ||
                   sample.left_thumb_x > xinput_left_thumb_deadzone;
    result.up = (sample.buttons & xinput_dpad_up) != 0 ||
                sample.left_thumb_y > xinput_left_thumb_deadzone;
    result.down = (sample.buttons & xinput_dpad_down) != 0 ||
                  sample.left_thumb_y < -xinput_left_thumb_deadzone;
    return result;
}

[[nodiscard]] constexpr DosFrontendJoystickSample
xinput_frontend_joystick_sample(const XInputControllerSample sample) noexcept {
    auto result = xinput_directions(sample);
    result.button_one = (sample.buttons & (xinput_a | xinput_start)) != 0;
    result.button_two = (sample.buttons & (xinput_b | xinput_back)) != 0;
    result.button_three = (sample.buttons & xinput_left_shoulder) != 0;
    result.button_four = (sample.buttons & xinput_right_shoulder) != 0;
    return result;
}

[[nodiscard]] constexpr InputState xinput_gameplay_input(
    const InputState keyboard, const XInputControllerSample sample,
    const bool controller_enabled) noexcept {
    if (!controller_enabled) {
        return keyboard;
    }
    const auto directions = xinput_directions(sample);
    InputState result;
    result.left = directions.left;
    result.right = directions.right;
    result.action = directions.up;
    result.down = directions.down;
    result.jump = (sample.buttons & xinput_a) != 0;
    result.fire = (sample.buttons & (xinput_x | xinput_b)) != 0 ||
                  sample.right_trigger > xinput_trigger_threshold;
    result.scroll_down = (sample.buttons & xinput_left_shoulder) != 0;
    result.scroll_up = (sample.buttons & xinput_right_shoulder) != 0;
    return result;
}

[[nodiscard]] constexpr bool xinput_pause_pressed(
    const std::uint16_t current_buttons,
    const std::uint16_t previous_buttons) noexcept {
    return (current_buttons & xinput_start) != 0 &&
           (previous_buttons & xinput_start) == 0;
}

// 1392:0299 jumps over every configurable keyboard binding whenever the DOS
// joystick-enable word is nonzero. Fixed shortcuts and cheat scanning are
// handled later by the ISR, but gameplay movement is joystick-exclusive.
[[nodiscard]] constexpr InputState dos_gameplay_input(
    const InputState keyboard, const DosFrontendJoystickSample joystick,
    const bool joystick_enabled, const int joystick_fire_button) noexcept {
    if (!joystick_enabled) {
        return keyboard;
    }

    InputState result;
    result.left = joystick.left;
    result.right = joystick.right;
    result.action = joystick.up;
    result.down = joystick.down;
    result.fire = joystick_fire_button == 0
        ? joystick.button_one : joystick.button_two;
    result.jump = joystick_fire_button == 0
        ? joystick.button_two : joystick.button_one;
    result.scroll_down = joystick.button_three;
    result.scroll_up = joystick.button_four;
    return result;
}

// 1392:08CA-093C confirms an axis direction on its second observation. The
// remembered code is not cleared at centre; moving to the opposite direction
// replaces it. Button state is passed through immediately.
class DosJoystickDirectionFilter final {
public:
    void reset() noexcept {
        last_x_code_ = 0;
        last_y_code_ = 0;
    }

    [[nodiscard]] DosFrontendJoystickSample filter(
        const DosFrontendJoystickSample raw) noexcept {
        DosFrontendJoystickSample result;
        result.button_one = raw.button_one;
        result.button_two = raw.button_two;
        result.button_three = raw.button_three;
        result.button_four = raw.button_four;
        if (raw.left) {
            result.left = last_x_code_ == 5;
            last_x_code_ = 5;
        }
        if (raw.right) {
            result.right = last_x_code_ == 4;
            last_x_code_ = 4;
        }
        if (raw.up) {
            result.up = last_y_code_ == 1;
            last_y_code_ = 1;
        }
        if (raw.down) {
            result.down = last_y_code_ == 2;
            last_y_code_ = 2;
        }
        return result;
    }

private:
    int last_x_code_{};
    int last_y_code_{};
};

// 06B8:000C assigns these codes in this exact order. DS:78F0 is the X-high
// (right) flag and DS:78F2 is X-low (left); a later active input wins, so
// button two has the highest priority and right has the lowest.
[[nodiscard]] constexpr int dos_frontend_joystick_code(
    const DosFrontendJoystickSample sample) noexcept {
    int code = 0;
    if (sample.right) {
        code = 4;
    }
    if (sample.left) {
        code = 5;
    }
    if (sample.up) {
        code = 1;
    }
    if (sample.down) {
        code = 2;
    }
    if (sample.button_one) {
        code = 3;
    }
    if (sample.button_two) {
        code = 6;
    }
    return code;
}

class DosFrontendJoystickRepeat final {
public:
    [[nodiscard]] int poll(const DosFrontendJoystickSample sample) noexcept {
        // DS:1AF8 suppresses ten subsequent calls after an accepted input.
        if (cooldown_ != 0) {
            --cooldown_;
            return 0;
        }
        const int code = dos_frontend_joystick_code(sample);
        if (code != 0) {
            cooldown_ = 10;
        }
        return code;
    }

    [[nodiscard]] int cooldown() const noexcept { return cooldown_; }

private:
    int cooldown_{};
};

} // namespace hocus
