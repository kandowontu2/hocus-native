#include "registered_executable.h"

#include <array>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace hocus {
namespace {

constexpr std::size_t expected_size = 182'656;
constexpr auto expected_sha256 =
    "c02d422b7ada36b2948c084074a5b96b0c712a5e8202036ad5a53f9df6c1ed5d";

constexpr std::array<std::uint32_t, 64> round_constants = {
    0x428A2F98, 0x71374491, 0xB5C0FBCF, 0xE9B5DBA5,
    0x3956C25B, 0x59F111F1, 0x923F82A4, 0xAB1C5ED5,
    0xD807AA98, 0x12835B01, 0x243185BE, 0x550C7DC3,
    0x72BE5D74, 0x80DEB1FE, 0x9BDC06A7, 0xC19BF174,
    0xE49B69C1, 0xEFBE4786, 0x0FC19DC6, 0x240CA1CC,
    0x2DE92C6F, 0x4A7484AA, 0x5CB0A9DC, 0x76F988DA,
    0x983E5152, 0xA831C66D, 0xB00327C8, 0xBF597FC7,
    0xC6E00BF3, 0xD5A79147, 0x06CA6351, 0x14292967,
    0x27B70A85, 0x2E1B2138, 0x4D2C6DFC, 0x53380D13,
    0x650A7354, 0x766A0ABB, 0x81C2C92E, 0x92722C85,
    0xA2BFE8A1, 0xA81A664B, 0xC24B8B70, 0xC76C51A3,
    0xD192E819, 0xD6990624, 0xF40E3585, 0x106AA070,
    0x19A4C116, 0x1E376C08, 0x2748774C, 0x34B0BCB5,
    0x391C0CB3, 0x4ED8AA4A, 0x5B9CCA4F, 0x682E6FF3,
    0x748F82EE, 0x78A5636F, 0x84C87814, 0x8CC70208,
    0x90BEFFFA, 0xA4506CEB, 0xBEF9A3F7, 0xC67178F2,
};

constexpr std::uint32_t rotate_right(const std::uint32_t value,
                                     const int bits) noexcept {
    return (value >> bits) | (value << (32 - bits));
}

std::string sha256(std::vector<std::uint8_t> bytes) {
    const auto bit_length = static_cast<std::uint64_t>(bytes.size()) * 8U;
    bytes.push_back(0x80);
    while (bytes.size() % 64 != 56) {
        bytes.push_back(0);
    }
    for (int shift = 56; shift >= 0; shift -= 8) {
        bytes.push_back(static_cast<std::uint8_t>(bit_length >> shift));
    }

    std::array<std::uint32_t, 8> hash = {
        0x6A09E667, 0xBB67AE85, 0x3C6EF372, 0xA54FF53A,
        0x510E527F, 0x9B05688C, 0x1F83D9AB, 0x5BE0CD19,
    };
    for (std::size_t block = 0; block < bytes.size(); block += 64) {
        std::array<std::uint32_t, 64> words{};
        for (int index = 0; index < 16; ++index) {
            const auto offset = block + static_cast<std::size_t>(index) * 4;
            words[static_cast<std::size_t>(index)] =
                (static_cast<std::uint32_t>(bytes[offset]) << 24U) |
                (static_cast<std::uint32_t>(bytes[offset + 1]) << 16U) |
                (static_cast<std::uint32_t>(bytes[offset + 2]) << 8U) |
                static_cast<std::uint32_t>(bytes[offset + 3]);
        }
        for (int index = 16; index < 64; ++index) {
            const auto x = words[static_cast<std::size_t>(index - 15)];
            const auto y = words[static_cast<std::size_t>(index - 2)];
            const auto sigma0 = rotate_right(x, 7) ^ rotate_right(x, 18) ^
                                (x >> 3U);
            const auto sigma1 = rotate_right(y, 17) ^ rotate_right(y, 19) ^
                                (y >> 10U);
            words[static_cast<std::size_t>(index)] =
                words[static_cast<std::size_t>(index - 16)] + sigma0 +
                words[static_cast<std::size_t>(index - 7)] + sigma1;
        }

        auto a = hash[0];
        auto b = hash[1];
        auto c = hash[2];
        auto d = hash[3];
        auto e = hash[4];
        auto f = hash[5];
        auto g = hash[6];
        auto h = hash[7];
        for (int index = 0; index < 64; ++index) {
            const auto big1 = rotate_right(e, 6) ^ rotate_right(e, 11) ^
                              rotate_right(e, 25);
            const auto choose = (e & f) ^ (~e & g);
            const auto temp1 = h + big1 + choose +
                round_constants[static_cast<std::size_t>(index)] +
                words[static_cast<std::size_t>(index)];
            const auto big0 = rotate_right(a, 2) ^ rotate_right(a, 13) ^
                              rotate_right(a, 22);
            const auto majority = (a & b) ^ (a & c) ^ (b & c);
            const auto temp2 = big0 + majority;
            h = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
        }
        hash[0] += a;
        hash[1] += b;
        hash[2] += c;
        hash[3] += d;
        hash[4] += e;
        hash[5] += f;
        hash[6] += g;
        hash[7] += h;
    }

    std::ostringstream result;
    result << std::hex << std::setfill('0');
    for (const auto word : hash) {
        result << std::setw(8) << word;
    }
    return result.str();
}

} // namespace

void validate_registered_hocus_exe(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) {
        throw std::runtime_error(
            "HOCUS.EXE is required. Copy the full registered v1.1 DOS "
            "HOCUS.EXE into the same folder as hocus_native.exe.");
    }
    const auto end = stream.tellg();
    if (end < 0 || static_cast<unsigned long long>(end) >
                       std::numeric_limits<std::size_t>::max()) {
        throw std::runtime_error("Cannot determine the size of " +
                                 path.string());
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(end));
    stream.seekg(0);
    if (!bytes.empty() &&
        !stream.read(reinterpret_cast<char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()))) {
        throw std::runtime_error("Cannot read " + path.string());
    }
    if (bytes.size() != expected_size || sha256(std::move(bytes)) !=
                                             expected_sha256) {
        throw std::runtime_error(
            "Unsupported HOCUS.EXE. The full registered v1.1 DOS executable "
            "is required beside hocus_native.exe.");
    }
}

} // namespace hocus
