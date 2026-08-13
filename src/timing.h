#pragma once

#include <cstdint>

namespace hocus {

inline constexpr int dos_timer_hz = 140;

// Borland C++ 1991's srand()/rand() pair at 0000:1715/1726. The registered
// executable uses this stream only to choose an attract demo; RANDOM.DAT is a
// separate deterministic stream used by stars and gameplay.
class BorlandRandom {
public:
    constexpr void seed(const std::uint16_t value) noexcept { state_ = value; }

    [[nodiscard]] constexpr std::uint16_t next() noexcept {
        state_ = state_ * 0x015A4E35u + 1u;
        return static_cast<std::uint16_t>((state_ >> 16u) & 0x7FFFu);
    }

    [[nodiscard]] constexpr std::uint32_t state() const noexcept {
        return state_;
    }

private:
    std::uint32_t state_{};
};

// Converts an integral number of registered-v1.1 timer ticks into Win32's
// integral milliseconds without accumulating rounding drift.
class DosGameTimerCadence {
public:
    [[nodiscard]] std::uint32_t next_interval_ms(const int timer_ticks) {
        const int numerator = phase_numerator_ + timer_ticks * 1000;
        const auto interval = static_cast<std::uint32_t>(
            (numerator + dos_timer_hz / 2) / dos_timer_hz);
        phase_numerator_ =
            numerator - static_cast<int>(interval) * dos_timer_hz;
        return interval;
    }

    void reset() noexcept { phase_numerator_ = 0; }

private:
    int phase_numerator_{};
};

} // namespace hocus
