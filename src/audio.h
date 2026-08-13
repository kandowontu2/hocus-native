#pragma once

#include <cstdint>
#include <vector>

namespace hocus {

// Convert the uncompressed Creative Voice blocks used by registered v1.1 to
// a mono unsigned-8-bit RIFF/WAVE image suitable for native WinMM playback.
std::vector<std::uint8_t> decode_voc_to_wav(
    const std::vector<std::uint8_t>& voc);

} // namespace hocus
