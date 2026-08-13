#pragma once

#include <cstdint>
#include <vector>

namespace hocus {

struct DecodedImage {
    int width{};
    int height{};
    // 0x00RRGGBB; little-endian memory is the BGRA order expected by Win32 DIBs.
    std::vector<std::uint32_t> pixels;
    // Empty means every pixel is opaque. Sprite images use one byte per pixel.
    std::vector<std::uint8_t> opacity;
    // Retained for formats whose pixels are decoded by a separate planar stream.
    std::vector<std::uint32_t> palette;
};

DecodedImage decode_pcx(const std::vector<std::uint8_t>& bytes);
DecodedImage decode_pcx(const std::vector<std::uint8_t>& bytes,
                        const std::vector<std::uint32_t>& active_palette);
std::vector<std::uint32_t> decode_vga_palette(
    const std::vector<std::uint8_t>& bytes);
DecodedImage decode_planar_img(const std::vector<std::uint8_t>& bytes,
                               const std::vector<std::uint32_t>& palette);
void blit(const DecodedImage& source, DecodedImage& destination, int x, int y);
void blit_region(const DecodedImage& source, DecodedImage& destination,
                 int source_x, int source_y, int width, int height,
                 int destination_x, int destination_y);

} // namespace hocus
