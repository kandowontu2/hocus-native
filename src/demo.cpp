#include "demo.h"

#include <stdexcept>

namespace hocus {

namespace {

std::uint16_t u16(const std::vector<std::uint8_t>& bytes,
                  const std::size_t offset) {
    return static_cast<std::uint16_t>(bytes[offset]) |
           (static_cast<std::uint16_t>(bytes[offset + 1]) << 8U);
}

bool flag(const std::uint8_t value) {
    if (value > 1) {
        throw std::runtime_error("Demo input contains a non-boolean key state");
    }
    return value != 0;
}

} // namespace

std::vector<InputState> decode_demo(const std::vector<std::uint8_t>& bytes) {
    if (bytes.size() < 2) {
        throw std::runtime_error("Demo asset is missing its frame count");
    }

    const auto frame_count = static_cast<std::size_t>(u16(bytes, 0));
    constexpr std::size_t frame_size = 6;
    if (bytes.size() != 2 + frame_count * frame_size) {
        throw std::runtime_error("Demo asset size does not match its frame count");
    }

    std::vector<InputState> frames;
    frames.reserve(frame_count);
    for (std::size_t index = 0; index < frame_count; ++index) {
        const auto offset = 2 + index * frame_size;
        // HOCUS.EXE 0BA5:54D4 copies the record in this exact order:
        // Up/action, Left, Right, Fire, Jump, Down/reserved.
        InputState input;
        input.action = flag(bytes[offset]);
        input.left = flag(bytes[offset + 1]);
        input.right = flag(bytes[offset + 2]);
        input.fire = flag(bytes[offset + 3]);
        input.jump = flag(bytes[offset + 4]);
        input.down = flag(bytes[offset + 5]);
        frames.push_back(input);
    }
    return frames;
}

LevelId demo_level(const std::size_t demo_index) {
    if (demo_index >= 5) {
        throw std::out_of_range("Demo index must be in the range 0..4");
    }
    return {1, static_cast<int>(demo_index * 2 + 1)};
}

} // namespace hocus
