#include "audio.h"

#include <cstddef>
#include <cstdint>
#include <stdexcept>

namespace hocus {
namespace {

void append_u16(std::vector<std::uint8_t>& bytes, std::uint16_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value));
    bytes.push_back(static_cast<std::uint8_t>(value >> 8U));
}

void append_u32(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value));
    bytes.push_back(static_cast<std::uint8_t>(value >> 8U));
    bytes.push_back(static_cast<std::uint8_t>(value >> 16U));
    bytes.push_back(static_cast<std::uint8_t>(value >> 24U));
}

std::uint16_t read_u16(const std::vector<std::uint8_t>& bytes,
                       std::size_t offset) {
    if (offset + 2 > bytes.size()) {
        throw std::runtime_error("Truncated VOC header");
    }
    return static_cast<std::uint16_t>(bytes[offset]) |
           static_cast<std::uint16_t>(bytes[offset + 1] << 8U);
}

} // namespace

std::vector<std::uint8_t> decode_voc_to_wav(
    const std::vector<std::uint8_t>& voc) {
    constexpr char signature[] = "Creative Voice File";
    if (voc.size() < 26) {
        throw std::runtime_error("Truncated VOC file");
    }
    for (std::size_t index = 0; index < sizeof(signature) - 1; ++index) {
        if (voc[index] != static_cast<std::uint8_t>(signature[index])) {
            throw std::runtime_error("Invalid VOC signature");
        }
    }
    if (voc[19] != 0x1A) {
        throw std::runtime_error("Invalid VOC terminator");
    }

    std::size_t cursor = read_u16(voc, 20);
    int sample_rate = 0;
    std::vector<std::uint8_t> pcm;
    while (cursor < voc.size()) {
        const int block_type = voc[cursor++];
        if (block_type == 0) {
            break;
        }
        if (cursor + 3 > voc.size()) {
            throw std::runtime_error("Truncated VOC block header");
        }
        const std::size_t size = voc[cursor] |
            (static_cast<std::size_t>(voc[cursor + 1]) << 8U) |
            (static_cast<std::size_t>(voc[cursor + 2]) << 16U);
        cursor += 3;
        if (cursor + size > voc.size()) {
            throw std::runtime_error("Truncated VOC block");
        }
        if (block_type == 1) {
            if (size < 2 || voc[cursor + 1] != 0) {
                throw std::runtime_error("Unsupported compressed VOC block");
            }
            const int rate = 1'000'000 / (256 - voc[cursor]);
            if (sample_rate != 0 && sample_rate != rate) {
                throw std::runtime_error("VOC changes sample rate");
            }
            sample_rate = rate;
            pcm.insert(pcm.end(), voc.begin() + static_cast<std::ptrdiff_t>(cursor + 2),
                       voc.begin() + static_cast<std::ptrdiff_t>(cursor + size));
        } else if (block_type == 2) {
            pcm.insert(pcm.end(), voc.begin() + static_cast<std::ptrdiff_t>(cursor),
                       voc.begin() + static_cast<std::ptrdiff_t>(cursor + size));
        } else {
            throw std::runtime_error("Unsupported VOC block type");
        }
        cursor += size;
    }
    if (sample_rate == 0 || pcm.empty()) {
        throw std::runtime_error("VOC contains no PCM data");
    }

    std::vector<std::uint8_t> wav;
    wav.reserve(44 + pcm.size() + (pcm.size() & 1U));
    wav.insert(wav.end(), {'R', 'I', 'F', 'F'});
    append_u32(wav, static_cast<std::uint32_t>(36 + pcm.size() +
                                               (pcm.size() & 1U)));
    wav.insert(wav.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
    append_u32(wav, 16);
    append_u16(wav, 1);
    append_u16(wav, 1);
    append_u32(wav, static_cast<std::uint32_t>(sample_rate));
    append_u32(wav, static_cast<std::uint32_t>(sample_rate));
    append_u16(wav, 1);
    append_u16(wav, 8);
    wav.insert(wav.end(), {'d', 'a', 't', 'a'});
    append_u32(wav, static_cast<std::uint32_t>(pcm.size()));
    wav.insert(wav.end(), pcm.begin(), pcm.end());
    if ((pcm.size() & 1U) != 0) {
        wav.push_back(0);
    }
    return wav;
}

} // namespace hocus
