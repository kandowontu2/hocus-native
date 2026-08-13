#pragma once

#include "level.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace hocus {

// Registered v1.1 DEMO*.DMO files contain a little-endian frame count followed
// by six one-byte key states per 20 Hz gameplay frame.
std::vector<InputState> decode_demo(const std::vector<std::uint8_t>& bytes);

// The DOS attract-mode launcher selects E1 level indices 0, 2, 4, 6, or 8,
// then loads DAT resource 18 + level_index / 2.
LevelId demo_level(std::size_t demo_index);

} // namespace hocus
