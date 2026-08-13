#include "pcx.h"

#include <array>
#include <algorithm>
#include <cstddef>
#include <stdexcept>

namespace hocus {
namespace {

std::uint16_t u16(const std::vector<std::uint8_t>& data, std::size_t offset) {
    if (offset + 2 > data.size()) {
        throw std::runtime_error("Truncated PCX header");
    }
    return static_cast<std::uint16_t>(data[offset]) |
           (static_cast<std::uint16_t>(data[offset + 1]) << 8U);
}

} // namespace

namespace {

DecodedImage decode_pcx_impl(
    const std::vector<std::uint8_t>& bytes,
    const std::vector<std::uint32_t>* const active_palette) {
    if (bytes.size() < 128 + 769 || bytes[0] != 0x0A || bytes[2] != 1 ||
        bytes[3] != 8) {
        throw std::runtime_error("Unsupported or truncated PCX image");
    }

    const int xmin = u16(bytes, 4);
    const int ymin = u16(bytes, 6);
    const int xmax = u16(bytes, 8);
    const int ymax = u16(bytes, 10);
    const int width = xmax - xmin + 1;
    const int height = ymax - ymin + 1;
    const int planes = bytes[65];
    const int bytes_per_line = u16(bytes, 66);
    if (width <= 0 || height <= 0 || planes != 1 || bytes_per_line < width ||
        bytes[bytes.size() - 769] != 0x0C) {
        throw std::runtime_error("Only 8-bit, one-plane paletted PCX is supported");
    }

    std::vector<std::uint8_t> indices;
    indices.reserve(static_cast<std::size_t>(bytes_per_line) * height);
    std::size_t cursor = 128;
    const auto data_end = bytes.size() - 769;
    while (indices.size() < static_cast<std::size_t>(bytes_per_line) * height) {
        if (cursor >= data_end) {
            throw std::runtime_error("Truncated PCX RLE data");
        }
        auto value = bytes[cursor++];
        std::size_t count = 1;
        if ((value & 0xC0U) == 0xC0U) {
            count = value & 0x3FU;
            if (cursor >= data_end || count == 0) {
                throw std::runtime_error("Invalid PCX RLE packet");
            }
            value = bytes[cursor++];
        }
        if (indices.size() + count >
            static_cast<std::size_t>(bytes_per_line) * height) {
            throw std::runtime_error("PCX RLE packet exceeds image dimensions");
        }
        indices.insert(indices.end(), count, value);
    }

    DecodedImage image;
    image.width = width;
    image.height = height;
    if (active_palette != nullptr) {
        if (active_palette->size() != 256) {
            throw std::runtime_error("PCX override palette must contain 256 colours");
        }
        image.palette = *active_palette;
    } else {
        const auto palette_offset = bytes.size() - 768;
        image.palette.resize(256);
        const auto scale_vga_component = [](const std::uint8_t value) {
            // 05D8:0577/05A4 shifts each PCX palette byte right twice before
            // programming the 6-bit VGA DAC. Expand that exact DAC value for
            // the native framebuffer instead of displaying the PCX byte raw.
            const auto vga = static_cast<std::uint32_t>(value >> 2U);
            return (vga * 255U + 31U) / 63U;
        };
        for (std::size_t index = 0; index < image.palette.size(); ++index) {
            const auto p = palette_offset + index * 3;
            image.palette[index] =
                (scale_vga_component(bytes[p]) << 16U) |
                (scale_vga_component(bytes[p + 1]) << 8U) |
                scale_vga_component(bytes[p + 2]);
        }
    }
    image.pixels.resize(static_cast<std::size_t>(width) * height);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const auto index = indices[static_cast<std::size_t>(y) *
                                       bytes_per_line + x];
            image.pixels[static_cast<std::size_t>(y) * width + x] =
                image.palette[index];
        }
    }
    return image;
}

} // namespace

DecodedImage decode_pcx(const std::vector<std::uint8_t>& bytes) {
    return decode_pcx_impl(bytes, nullptr);
}

DecodedImage decode_pcx(
    const std::vector<std::uint8_t>& bytes,
    const std::vector<std::uint32_t>& active_palette) {
    return decode_pcx_impl(bytes, &active_palette);
}

std::vector<std::uint32_t> decode_vga_palette(
    const std::vector<std::uint8_t>& bytes) {
    if (bytes.empty() || bytes.size() % 3 != 0) {
        throw std::runtime_error("Invalid VGA palette size");
    }
    std::vector<std::uint32_t> palette(bytes.size() / 3);
    const auto scale = [](std::uint8_t value) -> std::uint32_t {
        return (static_cast<std::uint32_t>(value) * 255U + 31U) / 63U;
    };
    for (std::size_t i = 0; i < palette.size(); ++i) {
        const auto r = scale(bytes[i * 3]);
        const auto g = scale(bytes[i * 3 + 1]);
        const auto b = scale(bytes[i * 3 + 2]);
        palette[i] = (r << 16U) | (g << 8U) | b;
    }
    return palette;
}

DecodedImage decode_planar_img(const std::vector<std::uint8_t>& bytes,
                               const std::vector<std::uint32_t>& palette) {
    if (bytes.size() < 4 || palette.empty()) {
        throw std::runtime_error("Truncated planar IMG or missing palette");
    }
    const auto width4 = u16(bytes, 0);
    const auto height = u16(bytes, 2);
    const std::size_t plane_size = static_cast<std::size_t>(width4) * height;
    if (4 + plane_size * 4 != bytes.size()) {
        throw std::runtime_error("Planar IMG dimensions do not match its size");
    }
    DecodedImage image;
    image.width = width4 * 4;
    image.height = height;
    image.palette = palette;
    image.pixels.resize(static_cast<std::size_t>(image.width) * image.height);
    for (std::size_t plane = 0; plane < 4; ++plane) {
        const auto block = 4 + plane * plane_size;
        for (std::size_t y = 0; y < height; ++y) {
            for (std::size_t x4 = 0; x4 < width4; ++x4) {
                const auto index = bytes[block + y * width4 + x4];
                if (index >= palette.size()) {
                    throw std::runtime_error("Planar IMG palette index is out of range");
                }
                image.pixels[y * image.width + x4 * 4 + plane] = palette[index];
            }
        }
    }
    return image;
}

void blit_region(const DecodedImage& source, DecodedImage& destination,
                 int source_x, int source_y, int width, int height,
                 int destination_x, int destination_y) {
    if (source_x < 0 || source_y < 0 || width < 0 || height < 0 ||
        source_x + width > source.width || source_y + height > source.height) {
        throw std::runtime_error("Source blit rectangle is out of bounds");
    }
    for (int y = 0; y < height; ++y) {
        const int dy = destination_y + y;
        if (dy < 0 || dy >= destination.height) {
            continue;
        }
        for (int x = 0; x < width; ++x) {
            const int dx = destination_x + x;
            if (dx < 0 || dx >= destination.width) {
                continue;
            }
            const auto source_index =
                static_cast<std::size_t>(source_y + y) * source.width + source_x + x;
            if (!source.opacity.empty() && source.opacity[source_index] == 0) {
                continue;
            }
            destination.pixels[static_cast<std::size_t>(dy) * destination.width + dx] =
                source.pixels[source_index];
        }
    }
}

void blit(const DecodedImage& source, DecodedImage& destination, int x, int y) {
    blit_region(source, destination, 0, 0, source.width, source.height, x, y);
}

} // namespace hocus
