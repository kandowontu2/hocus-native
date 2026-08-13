#pragma once

#include "pcx.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace hocus {

struct SpriteRecordInfo {
    std::string name;
    int width{};
    int height{};
    int movement_first{-1};
    int movement_last{-1};
    int airborne_frame{-1};
    int attack_first{-1};
    int attack_last{-1};
    int projectile_width{};
    int projectile_height{};
    int projectile_y_offset{};
    int projectile_first{-1};
    int projectile_last{-1};
    std::size_t frame_count{};
};

SpriteRecordInfo decode_sprite_record_info(
    const std::vector<std::uint8_t>& bytes, std::size_t sprite_index);

DecodedImage decode_sprite_frame(const std::vector<std::uint8_t>& bytes,
                                 std::size_t sprite_index,
                                 std::size_t frame,
                                 bool facing_west,
                                 const std::vector<std::uint32_t>& palette);

} // namespace hocus
