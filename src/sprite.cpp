#include "sprite.h"

#include <algorithm>
#include <array>
#include <stdexcept>

namespace hocus {
namespace {

std::uint16_t u16(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    if (offset + 2 > bytes.size()) {
        throw std::runtime_error("Truncated sprite data");
    }
    return static_cast<std::uint16_t>(bytes[offset]) |
           (static_cast<std::uint16_t>(bytes[offset + 1]) << 8U);
}

std::uint32_t u32(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    return static_cast<std::uint32_t>(u16(bytes, offset)) |
           (static_cast<std::uint32_t>(u16(bytes, offset + 2)) << 16U);
}

} // namespace

SpriteRecordInfo decode_sprite_record_info(
    const std::vector<std::uint8_t>& bytes, const std::size_t sprite_index) {
    constexpr std::size_t info_size = 220;
    if (bytes.size() < info_size) {
        throw std::runtime_error("Truncated sprite data");
    }
    const auto first_data = u32(bytes, 0);
    const auto sprite_count = first_data / info_size;
    if (first_data % info_size != 0 || first_data > bytes.size() ||
        sprite_index >= sprite_count) {
        throw std::runtime_error("Sprite index is out of range");
    }

    const auto info = sprite_index * info_size;
    SpriteRecordInfo result;
    for (std::size_t offset = 4; offset < 26 && bytes[info + offset] != 0;
         ++offset) {
        result.name.push_back(static_cast<char>(bytes[info + offset]));
    }
    result.width = static_cast<int>(u16(bytes, info + 26)) * 4;
    result.height = u16(bytes, info + 28);
    const auto frame_reference = [&bytes, info](const std::size_t offset) {
        const auto value = u16(bytes, info + offset);
        return value == 0xFFFF ? -1 : static_cast<int>(value);
    };
    result.movement_first = frame_reference(34);
    result.movement_last = frame_reference(36);
    result.airborne_frame = frame_reference(38);
    result.attack_first = frame_reference(42);
    result.attack_last = frame_reference(44);
    result.projectile_width = u16(bytes, info + 46);
    // Keep the header value raw. 0BA5:09C7 adds three exactly once when an
    // enemy projectile slot is created.
    result.projectile_height = u16(bytes, info + 48);
    result.projectile_y_offset = u16(bytes, info + 50);
    result.projectile_first = frame_reference(52);
    result.projectile_last = frame_reference(54);

    constexpr std::array<std::size_t, 10> reference_offsets = {
        30, 32, 34, 36, 38, 40, 42, 44, 52, 54,
    };
    int last_frame = -1;
    for (const auto offset : reference_offsets) {
        last_frame = std::max(last_frame, frame_reference(offset));
    }
    result.frame_count = static_cast<std::size_t>(last_frame + 1);
    if (result.width <= 0 || result.height <= 0 || result.frame_count == 0 ||
        result.frame_count > 20) {
        throw std::runtime_error("Invalid sprite record metadata");
    }
    return result;
}

DecodedImage decode_sprite_frame(const std::vector<std::uint8_t>& bytes,
                                 std::size_t sprite_index,
                                 std::size_t frame,
                                 bool facing_west,
                                 const std::vector<std::uint32_t>& palette) {
    constexpr std::size_t info_size = 220;
    constexpr std::size_t logical_stride = 320;
    if (bytes.size() < info_size || frame >= 20 || palette.size() < 128) {
        throw std::runtime_error("Invalid sprite decoder input");
    }
    const auto first_data = u32(bytes, 0);
    const auto sprite_count = first_data / info_size;
    if (first_data % info_size != 0 || sprite_index >= sprite_count) {
        throw std::runtime_error("Sprite index is out of range");
    }

    const auto info = sprite_index * info_size;
    const auto data_offset = u32(bytes, info);
    const auto width = static_cast<std::size_t>(u16(bytes, info + 26)) * 4;
    const auto height = static_cast<std::size_t>(u16(bytes, info + 28));
    const auto pixels_offset = u16(bytes, info + 56);
    const auto pixels_size = u16(bytes, info + 58);
    const auto layout_table = info + (facing_west ? 100 : 60);
    const auto pixel_table = info + (facing_west ? 180 : 140);
    std::size_t layout_cursor = data_offset + u16(bytes, layout_table + frame * 2);
    std::size_t pixel_cursor =
        data_offset + pixels_offset + static_cast<std::size_t>(u16(bytes, pixel_table + frame * 2)) * 4;
    const auto pixel_end = data_offset + pixels_offset + pixels_size;
    if (width == 0 || height == 0 || data_offset >= bytes.size() ||
        pixel_end > bytes.size() || layout_cursor >= bytes.size() ||
        pixel_cursor > pixel_end) {
        throw std::runtime_error("Sprite record is out of bounds");
    }

    DecodedImage image;
    image.width = static_cast<int>(width);
    image.height = static_cast<int>(height);
    image.palette = palette;
    image.pixels.resize(width * height);
    image.opacity.resize(width * height);

    std::size_t pointer = 0;
    std::uint8_t transparency_mask = 0;
    std::size_t commands = 0;
    for (;;) {
        if (layout_cursor >= bytes.size() || ++commands > bytes.size()) {
            throw std::runtime_error("Unterminated sprite layout stream");
        }
        const auto command = bytes[layout_cursor++];
        if (command == 0) {
            if (layout_cursor >= bytes.size()) {
                throw std::runtime_error("Truncated sprite transparency command");
            }
            transparency_mask = bytes[layout_cursor++];
            pointer = 0;
        } else if (command == 1) {
            pointer += u16(bytes, layout_cursor);
            layout_cursor += 2;
        } else if (command == 2) {
            if (pointer * 4 + 3 >= logical_stride * height || pixel_cursor + 4 > pixel_end) {
                throw std::runtime_error("Sprite layout or pixel stream is out of bounds");
            }
            for (std::size_t pixel = 0; pixel < 4; ++pixel) {
                const auto logical = pointer * 4 + pixel;
                const auto x = logical % logical_stride;
                const auto y = logical / logical_stride;
                if ((transparency_mask & (1U << pixel)) != 0 && x < width) {
                    const auto colour = bytes[pixel_cursor + pixel];
                    if (colour >= palette.size()) {
                        throw std::runtime_error("Sprite palette index is out of range");
                    }
                    const auto target = y * width + x;
                    image.pixels[target] = palette[colour];
                    image.opacity[target] = 1;
                }
            }
            pixel_cursor += 4;
            ++pointer;
        } else if (command == 3) {
            break;
        } else {
            throw std::runtime_error("Unknown sprite layout command");
        }
    }
    return image;
}

} // namespace hocus
