#include "ui.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstring>
#include <stdexcept>

namespace hocus {
namespace {

struct DosMenuLabel {
    std::string visible;
    std::size_t shortcut_offset{};
};

DosMenuLabel parse_dos_menu_label(const std::string_view label) {
    DosMenuLabel result;
    result.visible.reserve(label.size());
    bool marked = false;
    for (std::size_t index = 0; index < label.size(); ++index) {
        if (!marked && label[index] == '&' && index + 1 < label.size()) {
            marked = true;
            result.shortcut_offset = result.visible.size();
            continue;
        }
        result.visible.push_back(label[index]);
    }
    if (!marked) {
        result.shortcut_offset = 0;
    }
    return result;
}

std::uint16_t u16(const std::vector<std::uint8_t>& bytes,
                  const std::size_t offset) {
    if (offset + 2 > bytes.size()) {
        throw std::runtime_error("Truncated text page");
    }
    return static_cast<std::uint16_t>(bytes[offset]) |
           (static_cast<std::uint16_t>(bytes[offset + 1]) << 8U);
}

void validate_font(const std::vector<std::uint8_t>& font_mask) {
    if (font_mask.size() != 90 * 8) {
        throw std::runtime_error("Unexpected Hocus font dimensions");
    }
}

int glyph_width(const std::vector<std::uint8_t>& font_mask,
                const unsigned char character) {
    if (character == ' ') {
        return 4;
    }
    if (character < 33 || character > 122) {
        return 0;
    }
    const std::size_t glyph = character - 33;
    int rightmost = 0;
    for (int glyph_y = 0; glyph_y < 8; ++glyph_y) {
        const auto bits = font_mask[glyph * 8 + glyph_y];
        for (int glyph_x = 0; glyph_x < 8; ++glyph_x) {
            if ((bits & (1U << glyph_x)) != 0) {
                rightmost = std::max(rightmost, glyph_x);
            }
        }
    }
    return rightmost + 2;
}

template <typename RowColour>
void draw_font_rows(DecodedImage& frame,
                    const std::vector<std::uint8_t>& font_mask,
                    const std::string_view text, const int x, const int y,
                    RowColour row_colour) {
    validate_font(font_mask);
    int cursor = x;
    for (const unsigned char character : text) {
        if (character < 33 || character > 122) {
            cursor += glyph_width(font_mask, character);
            continue;
        }
        const std::size_t glyph = character - 33;
        for (int glyph_y = 0; glyph_y < 8; ++glyph_y) {
            const auto bits = font_mask[glyph * 8 + glyph_y];
            for (int glyph_x = 0; glyph_x < 8; ++glyph_x) {
                const int pixel_x = cursor + glyph_x;
                const int pixel_y = y + glyph_y;
                if (pixel_x >= 0 && pixel_x < frame.width &&
                    pixel_y >= 0 && pixel_y < frame.height &&
                    (bits & (1U << glyph_x)) != 0) {
                    frame.pixels[static_cast<std::size_t>(pixel_y) * frame.width +
                                 pixel_x] = row_colour(glyph_y);
                }
            }
        }
        cursor += glyph_width(font_mask, character);
    }
}

} // namespace

void StartupSequence::enter(const StartupStage stage) noexcept {
    stage_ = stage;
    hold_milliseconds_ = 0;
    switch (stage_) {
    case StartupStage::initial_fade_out:
        fade_level_ = 40;
        break;
    case StartupStage::piracy_fade_in:
    case StartupStage::apogee_fade_in:
        fade_level_ = 0;
        break;
    case StartupStage::title_fade_in:
        fade_level_ = 0;
        break;
    case StartupStage::piracy_fade_out:
    case StartupStage::apogee_fade_out:
        fade_level_ = stage_ == StartupStage::piracy_fade_out ? 20 : 30;
        break;
    case StartupStage::title_fade_out:
        fade_level_ = 30;
        break;
    case StartupStage::piracy_hold:
    case StartupStage::apogee_hold:
    case StartupStage::title_hold:
    case StartupStage::done:
        break;
    }
}

void StartupSequence::press_any_key() noexcept {
    switch (stage_) {
    case StartupStage::piracy_hold:
        advance_pending_ = false;
        enter(StartupStage::piracy_fade_out);
        break;
    case StartupStage::apogee_hold:
        advance_pending_ = false;
        enter(StartupStage::apogee_fade_out);
        break;
    case StartupStage::title_hold:
        advance_pending_ = false;
        enter(StartupStage::title_fade_out);
        break;
    case StartupStage::initial_fade_out:
    case StartupStage::piracy_fade_in:
    case StartupStage::piracy_fade_out:
    case StartupStage::apogee_fade_in:
    case StartupStage::apogee_fade_out:
    case StartupStage::title_fade_in:
        // A key already held when 1392:0108 starts ends that wait. Preserve
        // the input across a fade, then consume it after the owning screen's
        // hold stage has been presented to the front end.
        advance_pending_ = true;
        break;
    case StartupStage::title_fade_out:
    case StartupStage::done:
        break;
    }
}

StartupFade StartupSequence::fade() const noexcept {
    switch (stage_) {
    case StartupStage::initial_fade_out:
        return {fade_level_, 40};
    case StartupStage::piracy_fade_in:
    case StartupStage::piracy_fade_out:
        return {fade_level_, 20};
    case StartupStage::apogee_fade_in:
        return {fade_level_, 20};
    case StartupStage::apogee_fade_out:
    case StartupStage::title_fade_out:
        return {fade_level_, 30};
    case StartupStage::title_fade_in:
        return {fade_level_, 40};
    case StartupStage::piracy_hold:
    case StartupStage::apogee_hold:
    case StartupStage::title_hold:
        return {1, 1};
    case StartupStage::done:
        return {0, 1};
    }
    return {0, 1};
}

void StartupSequence::tick(const int elapsed_milliseconds) {
    if (elapsed_milliseconds < 0) {
        throw std::out_of_range("Startup elapsed time cannot be negative");
    }
    if (advance_pending_) {
        switch (stage_) {
        case StartupStage::piracy_hold:
            advance_pending_ = false;
            enter(StartupStage::piracy_fade_out);
            return;
        case StartupStage::apogee_hold:
            advance_pending_ = false;
            enter(StartupStage::apogee_fade_out);
            return;
        case StartupStage::title_hold:
            advance_pending_ = false;
            enter(StartupStage::title_fade_out);
            return;
        default:
            break;
        }
    }
    switch (stage_) {
    case StartupStage::initial_fade_out:
        if (--fade_level_ <= 0) {
            enter(StartupStage::piracy_fade_in);
        }
        break;
    case StartupStage::piracy_fade_in:
        if (++fade_level_ >= 20) {
            enter(StartupStage::piracy_hold);
        }
        break;
    case StartupStage::piracy_hold:
        hold_milliseconds_ += elapsed_milliseconds;
        if (hold_milliseconds_ >= 15000) {
            enter(StartupStage::piracy_fade_out);
        }
        break;
    case StartupStage::piracy_fade_out:
        if (--fade_level_ <= 0) {
            enter(StartupStage::apogee_fade_in);
        }
        break;
    case StartupStage::apogee_fade_in:
        if (++fade_level_ >= 20) {
            enter(StartupStage::apogee_hold);
        }
        break;
    case StartupStage::apogee_hold:
        hold_milliseconds_ += elapsed_milliseconds;
        if (hold_milliseconds_ >= 9000) {
            enter(StartupStage::apogee_fade_out);
        }
        break;
    case StartupStage::apogee_fade_out:
        if (--fade_level_ <= 0) {
            enter(StartupStage::title_fade_in);
        }
        break;
    case StartupStage::title_fade_in:
        if (++fade_level_ >= 40) {
            enter(StartupStage::title_hold);
        }
        break;
    case StartupStage::title_hold:
        hold_milliseconds_ += elapsed_milliseconds;
        if (hold_milliseconds_ >= 4000) {
            enter(StartupStage::title_fade_out);
        }
        break;
    case StartupStage::title_fade_out:
        if (--fade_level_ <= 0) {
            enter(StartupStage::done);
        }
        break;
    case StartupStage::done:
        break;
    }
}

DecodedImage render_vga_fade(const DecodedImage& source,
                             const StartupFade fade) {
    if (fade.denominator <= 0 || fade.numerator < 0 ||
        fade.numerator > fade.denominator) {
        throw std::out_of_range("Invalid VGA fade ratio");
    }
    const auto fade_colour = [fade](const std::uint32_t colour) {
        const auto component = [fade](const std::uint32_t value) {
            // Convert the native display component back to its exact six-bit
            // DAC level, apply 05D8:0419/046E's integer ratio, then expand it.
            const auto dac = (value * 63U + 127U) / 255U;
            const auto faded = dac * static_cast<std::uint32_t>(fade.numerator) /
                               static_cast<std::uint32_t>(fade.denominator);
            return (faded * 255U + 31U) / 63U;
        };
        return (component((colour >> 16U) & 0xFFU) << 16U) |
               (component((colour >> 8U) & 0xFFU) << 8U) |
               component(colour & 0xFFU);
    };

    DecodedImage frame = source;
    for (auto& colour : frame.palette) {
        colour = fade_colour(colour);
    }
    for (auto& pixel : frame.pixels) {
        pixel = fade_colour(pixel);
    }
    return frame;
}

DecodedImage render_vga_whiten(const DecodedImage& source, const int steps) {
    if (steps < 0) {
        throw std::out_of_range("Invalid VGA white-fade step count");
    }
    const auto whiten_colour = [steps](const std::uint32_t colour) {
        const auto component = [steps](const std::uint32_t value) {
            const auto dac = (value * 63U + 127U) / 255U;
            const auto whitened = std::min<std::uint32_t>(
                63U, dac + static_cast<std::uint32_t>(steps));
            return (whitened * 255U + 31U) / 63U;
        };
        return (component((colour >> 16U) & 0xFFU) << 16U) |
               (component((colour >> 8U) & 0xFFU) << 8U) |
               component(colour & 0xFFU);
    };

    DecodedImage frame = source;
    for (auto& colour : frame.palette) {
        colour = whiten_colour(colour);
    }
    for (auto& pixel : frame.pixels) {
        pixel = whiten_colour(pixel);
    }
    return frame;
}

TextPage decode_text_page(const std::vector<std::uint8_t>& bytes) {
    constexpr std::size_t line_capacity = 20;
    constexpr std::size_t line_metadata_size = 4;
    constexpr std::size_t line_text_size = 80;
    constexpr std::size_t text_offset = 6 + line_capacity * line_metadata_size;
    constexpr std::size_t expected_size = text_offset +
        line_capacity * line_text_size;
    if (bytes.size() != expected_size) {
        throw std::runtime_error("Unexpected text-page size");
    }

    const auto line_count = u16(bytes, 0);
    if (line_count > line_capacity) {
        throw std::runtime_error("Text page has too many lines");
    }
    TextPage page;
    page.nonempty_spacing = u16(bytes, 2);
    page.empty_spacing = u16(bytes, 4);
    page.lines.reserve(line_count);
    for (std::size_t line = 0; line < line_count; ++line) {
        const auto metadata = 6 + line * line_metadata_size;
        const auto begin = text_offset + line * line_text_size;
        auto end = begin;
        while (end < begin + line_text_size && bytes[end] != 0) {
            ++end;
        }
        page.lines.push_back({
            static_cast<int>(u16(bytes, metadata)),
            static_cast<int>(u16(bytes, metadata + 2)),
            std::string(bytes.begin() + static_cast<std::ptrdiff_t>(begin),
                        bytes.begin() + static_cast<std::ptrdiff_t>(end)),
        });
    }
    return page;
}

void draw_font_text(DecodedImage& frame,
                    const std::vector<std::uint8_t>& font_mask,
                    const std::string_view text, const int x, const int y,
                    const std::uint32_t colour) {
    draw_font_rows(frame, font_mask, text, x, y,
                   [colour](const int) { return colour; });
}

int font_text_width(const std::vector<std::uint8_t>& font_mask,
                    const std::string_view text) {
    validate_font(font_mask);
    int width = 0;
    for (const unsigned char character : text) {
        width += glyph_width(font_mask, character);
    }
    return width;
}

char dos_menu_shortcut_key(const std::string_view label) noexcept {
    const auto marker = label.find('&');
    const auto offset = marker != std::string_view::npos &&
                                marker + 1 < label.size()
        ? marker + 1 : 0;
    if (label.empty()) {
        return '\0';
    }
    return static_cast<char>(std::toupper(
        static_cast<unsigned char>(label[offset])));
}

void draw_dos_font_text(DecodedImage& frame,
                        const std::vector<std::uint8_t>& font_mask,
                        const std::string_view text, const int x, const int y,
                        const int style) {
    constexpr std::array<int, 8> style_bases = {
        0xC0, 0xC8, 0xD0, 0xD8, 0xE0, 0xE8, 0x70, 0x68,
    };
    if (style < 0 || style > static_cast<int>(style_bases.size())) {
        throw std::out_of_range("Invalid DOS font style");
    }
    const int base = style == 0 ? 0x80
                                : style_bases[static_cast<std::size_t>(style - 1)];
    const int final_index = style == 0 ? base : base + 7;
    if (final_index >= static_cast<int>(frame.palette.size())) {
        throw std::runtime_error("DOS font style exceeds the active palette");
    }
    draw_font_rows(frame, font_mask, text, x, y, [&](const int row) {
        const int palette_index = style == 0 ? base : base + row;
        return frame.palette[static_cast<std::size_t>(palette_index)];
    });
}

void DosMenuStarfield::initialize(
    const std::vector<std::uint8_t>& sincos,
    const std::vector<std::uint8_t>& random) {
    if (sincos.size() != 360 * 2 * sizeof(float) ||
        random.size() != random_.size() * 2) {
        throw std::runtime_error("Unexpected DOS menu animation tables");
    }
    for (std::size_t angle = 0; angle < sine_million_.size(); ++angle) {
        float sine{};
        float negative_cosine{};
        std::memcpy(&sine, sincos.data() + angle * 8, sizeof(float));
        std::memcpy(&negative_cosine,
                    sincos.data() + angle * 8 + sizeof(float), sizeof(float));
        // 0548:02FB-0339 loads each float, multiplies it by the 1,000,000.0
        // constant at DS:181A, and calls the truncating Borland __ftol helper.
        sine_million_[angle] =
            static_cast<std::int32_t>(sine * 1'000'000.0F);
        negative_cosine_million_[angle] =
            static_cast<std::int32_t>(negative_cosine * 1'000'000.0F);
    }
    for (std::size_t index = 0; index < random_.size(); ++index) {
        random_[index] = static_cast<std::int16_t>(
            static_cast<std::uint16_t>(random[index * 2]) |
            (static_cast<std::uint16_t>(random[index * 2 + 1]) << 8U));
    }
    random_index_ = 0;
    initialized_ = true;
    for (auto& star : stars_) {
        respawn(star);
    }
}

int DosMenuStarfield::next_random_below(const int upper_bound) {
    if (upper_bound <= 0) {
        throw std::out_of_range("Invalid DOS menu random modulus");
    }
    if (++random_index_ > 999) {
        random_index_ = 0;
    }
    return random_[static_cast<std::size_t>(random_index_)] % upper_bound;
}

void DosMenuStarfield::set_random_index(const int index) noexcept {
    random_index_ = ((index % 1000) + 1000) % 1000;
}

std::pair<int, int> DosMenuStarfield::position(const Star& star) const {
    const auto angle = static_cast<std::size_t>(star.angle);
    // 06B8:0103/039C multiply the million-scaled long by the radius and use
    // Borland's signed long division helper with the literal 1,000,000.
    const int x = static_cast<int>(
        static_cast<std::int64_t>(star.radius) * sine_million_[angle] /
        1'000'000) + 160;
    const int y = static_cast<int>(
        static_cast<std::int64_t>(star.radius) *
        negative_cosine_million_[angle] / 1'000'000) + 112;
    return {x, y};
}

void DosMenuStarfield::respawn(Star& star) {
    do {
        star.angle = next_random_below(360);
        star.radius = next_random_below(160);
    } while ([&] {
        const auto [x, y] = position(star);
        return x < 0 || x > 319 || y < 0 || y > 199;
    }());
    star.radius_step = next_random_below(5) + 1;
    star.brightness = 0;
    star.brightness_step = next_random_below(5) + 1;
}

void DosMenuStarfield::tick() {
    if (!initialized_) {
        throw std::logic_error("DOS menu starfield is not initialized");
    }
    for (auto& star : stars_) {
        star.radius += star.radius_step;
        star.brightness = std::min(127,
                                   star.brightness + star.brightness_step);
        const auto [x, y] = position(star);
        if (x < 0 || x > 319 || y < 0 || y > 199) {
            respawn(star);
        }
    }
}

void DosMenuStarfield::draw(DecodedImage& image) const {
    if (!initialized_ || image.width != 320 || image.height != 200 ||
        image.palette.size() < 256) {
        throw std::runtime_error("Invalid DOS menu starfield target");
    }
    for (const auto& star : stars_) {
        const auto [x, y] = position(star);
        const auto offset = static_cast<std::size_t>(y) * image.width + x;
        if (image.pixels[offset] == image.palette.front()) {
            const int colour = 0xF0 + (star.brightness >> 3);
            image.pixels[offset] =
                image.palette[static_cast<std::size_t>(colour)];
        }
    }
}

DosMenuRender render_dos_menu(
    const DecodedImage& logo, const DecodedImage& bottom,
    const DecodedImage& cursor_sheet,
    const std::vector<std::uint8_t>& font_mask,
    const std::vector<std::uint32_t>& palette,
    const std::span<const std::string> lines, const bool has_heading,
    const int selection, const int cursor_frame,
    const std::string_view prompt) {
    constexpr int width = 320;
    constexpr int height = 200;
    constexpr int content_top = 40;
    constexpr int content_height = 144;
    constexpr int bottom_y = 184;

    validate_font(font_mask);
    if (palette.size() < 256 || logo.width != width || logo.height != 39 ||
        bottom.width != width || bottom.height != 16 ||
        cursor_sheet.width != 128 || cursor_sheet.height != 15) {
        throw std::runtime_error("Invalid DOS menu assets");
    }
    const std::size_t first_item = has_heading ? 1 : 0;
    if (lines.size() <= first_item || selection < 0 ||
        selection >= static_cast<int>(lines.size() - first_item) ||
        cursor_frame < -1 || cursor_frame >= 8) {
        throw std::out_of_range("Invalid DOS menu state");
    }

    DosMenuRender result;
    result.image.width = width;
    result.image.height = height;
    result.image.palette = palette;
    result.image.pixels.assign(static_cast<std::size_t>(width) * height,
                               palette.front());
    blit(logo, result.image, 0, 0);
    blit(bottom, result.image, 0, bottom_y);

    std::vector<DosMenuLabel> parsed_lines;
    parsed_lines.reserve(lines.size());
    for (const auto& line : lines) {
        parsed_lines.push_back(parse_dos_menu_label(line));
    }

    int maximum_width = 0;
    for (std::size_t index = first_item; index < lines.size(); ++index) {
        maximum_width = std::max(
            maximum_width,
            font_text_width(font_mask, parsed_lines[index].visible));
    }
    result.text_x = (width - maximum_width) / 2;

    int extra_spacing = 0;
    for (const auto& line : lines) {
        if (!line.empty() && line.back() == '~') {
            extra_spacing += 4;
        }
    }
    int y = content_top +
            (content_height -
             static_cast<int>(lines.size() + (has_heading ? 1 : 0)) * 10 +
             extra_spacing) /
                2;
    if (has_heading) {
        const int heading_x =
            (width - font_text_width(font_mask,
                                     parsed_lines.front().visible)) / 2;
        draw_dos_font_text(result.image, font_mask,
                           parsed_lines.front().visible, heading_x, y, 5);
        y += 20;
    }

    result.item_y.reserve(lines.size() - first_item);
    for (std::size_t index = first_item; index < lines.size(); ++index) {
        result.item_y.push_back(y);
        const auto& parsed = parsed_lines[index];
        draw_dos_font_text(result.image, font_mask, parsed.visible,
                           result.text_x, y, 2);
        if (parsed.shortcut_offset < parsed.visible.size()) {
            const auto shortcut_x = result.text_x + font_text_width(
                font_mask, std::string_view(parsed.visible).substr(
                    0, parsed.shortcut_offset));
            draw_dos_font_text(result.image, font_mask,
                               std::string_view(parsed.visible).substr(
                                   parsed.shortcut_offset, 1),
                               shortcut_x, y, 4);
        }
        y += 10;
        if (!lines[index].empty() && lines[index].back() == '~') {
            y += 4;
        }
    }

    const int prompt_x = (width - font_text_width(font_mask, prompt)) / 2;
    draw_dos_font_text(result.image, font_mask, prompt, prompt_x + 1, 189, 0);
    draw_dos_font_text(result.image, font_mask, prompt, prompt_x, 188, 4);

    if (cursor_frame >= 0) {
        // Startup draws BULLIT.IMG at byte-X 28, Y 185 on hidden VGA page 3.
        // 06B8:0080 copies one of its eight 16x15 frames from that page. It
        // treats destination X as a Mode-X byte coordinate, so round down to
        // a four-pixel boundary exactly as the DOS routine does.
        const int cursor_x = ((result.text_x - 24) / 4) * 4;
        const int cursor_y =
            result.item_y[static_cast<std::size_t>(selection)] - 4;
        blit_region(cursor_sheet, result.image, cursor_frame * 16, 0,
                    16, 15, cursor_x, cursor_y);
    }
    return result;
}

DosSlotRender render_dos_slot_screen(
    const DecodedImage& logo, const DecodedImage& bottom,
    const DecodedImage& cursor_sheet,
    const std::vector<std::uint8_t>& font_mask,
    const std::vector<std::uint32_t>& palette,
    const std::string_view title,
    const std::span<const std::string> slot_names,
    const int selection, const int cursor_frame,
    const bool editing_name, const std::string_view prompt) {
    constexpr int width = 320;
    constexpr int height = 200;
    if (palette.size() < 256 || logo.width != width || logo.height != 39 ||
        bottom.width != width || bottom.height != 16 ||
        cursor_sheet.width != 128 || cursor_sheet.height != 15 ||
        slot_names.size() != 9 || selection < 0 || selection >= 9 ||
        cursor_frame < -1 || cursor_frame > 7) {
        throw std::runtime_error("Invalid DOS slot-screen input");
    }

    DosSlotRender result;
    result.image.width = width;
    result.image.height = height;
    result.image.palette = palette;
    result.image.pixels.assign(static_cast<std::size_t>(width) * height,
                               palette.front());
    blit(bottom, result.image, 0, 184);
    blit(logo, result.image, 0, 0);

    const int title_x = (width - font_text_width(font_mask, title)) / 2;
    draw_dos_font_text(result.image, font_mask, title, title_x, 59, 5);

    for (int index = 0; index < 9; ++index) {
        const int y = 79 + index * 10;
        result.item_y[static_cast<std::size_t>(index)] = y;
        const auto number = std::to_string(index + 1);
        draw_dos_font_text(result.image, font_mask, number, 35, y, 4);
        draw_dos_font_text(result.image, font_mask,
                           slot_names[static_cast<std::size_t>(index)],
                           45, y, 3);
    }

    if (editing_name) {
        const auto& name = slot_names[static_cast<std::size_t>(selection)];
        draw_dos_font_text(result.image, font_mask, "_",
                           45 + font_text_width(font_mask, name),
                           result.item_y[static_cast<std::size_t>(selection)],
                           2);
    }

    const int prompt_x = (width - font_text_width(font_mask, prompt)) / 2;
    draw_dos_font_text(result.image, font_mask, prompt,
                       prompt_x + 1, 189, 0);
    draw_dos_font_text(result.image, font_mask, prompt,
                       prompt_x, 188, 4);

    if (cursor_frame >= 0) {
        blit_region(cursor_sheet, result.image, cursor_frame * 16, 0,
                    16, 15, 4,
                    result.item_y[static_cast<std::size_t>(selection)] - 4);
    }
    return result;
}

DecodedImage render_dos_high_scores(
    const DecodedImage& logo, const DecodedImage& bottom,
    const std::vector<std::uint8_t>& font_mask,
    const std::vector<std::uint32_t>& palette, const int episode,
    const std::span<const DosHighScoreEntry> scores,
    const int editing_rank, const std::string_view prompt) {
    constexpr int width = 320;
    constexpr int height = 200;
    constexpr std::array<int, 5> row_y = {92, 112, 122, 132, 142};
    if (palette.size() < 256 || logo.width != width || logo.height != 39 ||
        bottom.width != width || bottom.height != 16 ||
        episode < 0 || episode >= 4 || scores.size() != 5 ||
        editing_rank < -1 || editing_rank >= 5) {
        throw std::runtime_error("Invalid DOS high-score input");
    }

    DecodedImage frame;
    frame.width = width;
    frame.height = height;
    frame.palette = palette;
    frame.pixels.assign(static_cast<std::size_t>(width) * height,
                        palette.front());
    blit(bottom, frame, 0, 184);
    blit(logo, frame, 0, 0);

    const auto title = "High Scores For Game " + std::to_string(episode + 1);
    draw_dos_font_text(frame, font_mask, title,
                       (width - font_text_width(font_mask, title)) / 2,
                       72, 5);

    for (int rank = 0; rank < 5; ++rank) {
        const int style = rank == 0 ? 4 : 3;
        const auto& entry = scores[static_cast<std::size_t>(rank)];
        draw_dos_font_text(frame, font_mask, entry.name,
                           27, row_y[static_cast<std::size_t>(rank)], style);
        const auto score = std::to_string(entry.score);
        draw_dos_font_text(frame, font_mask, score,
                           290 - font_text_width(font_mask, score),
                           row_y[static_cast<std::size_t>(rank)], style);
    }

    if (editing_rank >= 0) {
        const auto& name = scores[static_cast<std::size_t>(editing_rank)].name;
        draw_dos_font_text(frame, font_mask, "_",
                           27 + font_text_width(font_mask, name),
                           row_y[static_cast<std::size_t>(editing_rank)], 2);
    }

    const int prompt_x = (width - font_text_width(font_mask, prompt)) / 2;
    draw_dos_font_text(frame, font_mask, prompt, prompt_x + 1, 189, 0);
    draw_dos_font_text(frame, font_mask, prompt, prompt_x, 188, 4);
    return frame;
}

DecodedImage render_dos_volume_screen(
    const DecodedImage& volume_bar,
    const std::vector<std::uint8_t>& font_mask,
    const std::vector<std::uint32_t>& palette,
    const int sound_volume, const int music_volume,
    const int selected_row, const bool selected_visible) {
    constexpr int width = 320;
    constexpr int height = 200;
    if (palette.size() < 256 || volume_bar.width != 32 ||
        volume_bar.height != 13 || sound_volume < 0 || sound_volume > 15 ||
        music_volume < 0 || music_volume > 15 ||
        selected_row < 0 || selected_row > 1) {
        throw std::runtime_error("Invalid DOS volume-screen input");
    }
    validate_font(font_mask);

    DecodedImage frame;
    frame.width = width;
    frame.height = height;
    frame.palette = palette;
    frame.pixels.assign(static_cast<std::size_t>(width) * height,
                        palette.front());

    constexpr std::array<std::string_view, 2> headings = {
        "Sound FX Volume", "Music Volume",
    };
    constexpr std::array<int, 2> heading_y = {62, 122};
    constexpr std::array<int, 2> bar_y = {82, 142};
    const std::array<int, 2> selected = {sound_volume, music_volume};
    for (std::size_t row = 0; row < headings.size(); ++row) {
        const auto heading = headings[row];
        draw_dos_font_text(frame, font_mask, heading,
                           (width - font_text_width(font_mask, heading)) / 2,
                           heading_y[row], 5);
        for (int segment = 0; segment < 16; ++segment) {
            blit_region(volume_bar, frame, 0, 0, 16, 13,
                        32 + segment * 16, bar_y[row]);
        }
        if (static_cast<int>(row) != selected_row || selected_visible) {
            blit_region(volume_bar, frame, 16, 0, 16, 13,
                        32 + selected[row] * 16, bar_y[row]);
        }
    }

    constexpr std::string_view prompt =
        "UP/DOWN/LEFT/RIGHT to move - ENTER when done";
    const int prompt_x = (width - font_text_width(font_mask, prompt)) / 2;
    draw_dos_font_text(frame, font_mask, prompt, prompt_x + 1, 189, 0);
    draw_dos_font_text(frame, font_mask, prompt, prompt_x, 188, 4);
    return frame;
}

DecodedImage render_dos_key_screen(
    const std::vector<std::uint8_t>& font_mask,
    const std::vector<std::uint32_t>& palette,
    const std::span<const std::uint8_t> bindings,
    const bool choosing_key, const int action) {
    constexpr int width = 320;
    constexpr int height = 200;
    constexpr std::array<std::string_view, 8> actions = {
        "Left", "Right", "Jump", "Fire", "Up", "Down",
        "Scroll up", "Scroll down",
    };
    constexpr std::array<std::string_view, 18> keys = {
        "Left shift", "Right shift", "Ctrl", "Alt", "Caps", "Space",
        "Enter", "Insert", "Delete", "Home", "End", "Page up",
        "Page down", "Up arrow", "Down arrow", "Left arrow",
        "Right arrow", "Cursor 5",
    };
    if (palette.size() < 256 || bindings.size() != actions.size() ||
        action < 0 || action >= static_cast<int>(actions.size()) ||
        std::any_of(bindings.begin(), bindings.end(),
                    [](const std::uint8_t value) { return value >= 18; })) {
        throw std::runtime_error("Invalid DOS key-screen input");
    }
    validate_font(font_mask);

    DecodedImage frame;
    frame.width = width;
    frame.height = height;
    frame.palette = palette;
    frame.pixels.assign(static_cast<std::size_t>(width) * height,
                        palette.front());

    if (!choosing_key) {
        constexpr std::string_view heading = "Select a key to define";
        draw_dos_font_text(frame, font_mask, heading,
                           (width - font_text_width(font_mask, heading)) / 2,
                           62, 5);
        for (int index = 0; index < static_cast<int>(actions.size()); ++index) {
            const int y = 82 + index * 10;
            const std::string letter(1, static_cast<char>('A' + index));
            draw_dos_font_text(frame, font_mask, letter, 70, y, 4);
            draw_dos_font_text(frame, font_mask, "-", 80, y, 2);
            draw_dos_font_text(frame, font_mask, actions[index], 90, y, 2);
            const auto key = keys[bindings[static_cast<std::size_t>(index)]];
            draw_dos_font_text(frame, font_mask, key,
                               250 - font_text_width(font_mask, key), y, 3);
        }
    } else {
        const auto heading =
            std::string("Select new key for: ") + std::string(actions[action]);
        draw_dos_font_text(frame, font_mask, heading,
                           (width - font_text_width(font_mask, heading)) / 2,
                           72, 5);
        constexpr std::array<int, 3> letter_x = {27, 125, 200};
        constexpr std::array<int, 3> dash_x = {37, 135, 210};
        constexpr std::array<int, 3> name_x = {47, 145, 220};
        for (int column = 0; column < 3; ++column) {
            for (int row = 0; row < 6; ++row) {
                const int index = column * 6 + row;
                const int y = 92 + row * 10;
                const std::string letter(1, static_cast<char>('A' + index));
                draw_dos_font_text(frame, font_mask, letter,
                                   letter_x[column], y, 4);
                draw_dos_font_text(frame, font_mask, "-", dash_x[column], y, 2);
                draw_dos_font_text(frame, font_mask, keys[index],
                                   name_x[column], y, 2);
            }
        }
    }

    constexpr std::string_view prompt =
        "Press LETTER to select - ESC to exit";
    const int prompt_x = (width - font_text_width(font_mask, prompt)) / 2;
    draw_dos_font_text(frame, font_mask, prompt, prompt_x + 1, 189, 0);
    draw_dos_font_text(frame, font_mask, prompt, prompt_x, 188, 4);
    return frame;
}

DecodedImage render_dos_message_screen(
    const std::vector<std::uint8_t>& font_mask,
    const std::vector<std::uint32_t>& palette,
    const std::string_view message, const int style,
    const std::string_view prompt) {
    constexpr int width = 320;
    constexpr int height = 200;
    if (palette.size() < 256 || style < 0 || style > 8) {
        throw std::runtime_error("Invalid DOS message-screen input");
    }
    validate_font(font_mask);
    DecodedImage frame;
    frame.width = width;
    frame.height = height;
    frame.palette = palette;
    frame.pixels.assign(static_cast<std::size_t>(width) * height,
                        palette.front());
    draw_dos_font_text(frame, font_mask, message,
                       (width - font_text_width(font_mask, message)) / 2,
                       107, style);
    const int prompt_x = (width - font_text_width(font_mask, prompt)) / 2;
    draw_dos_font_text(frame, font_mask, prompt, prompt_x + 1, 189, 0);
    draw_dos_font_text(frame, font_mask, prompt, prompt_x, 188, 4);
    return frame;
}

DecodedImage render_dos_confirmation_screen(
    const std::vector<std::uint8_t>& font_mask,
    const std::vector<std::uint32_t>& palette,
    const std::string_view first_line,
    const std::string_view second_line) {
    constexpr int width = 320;
    constexpr int height = 200;
    constexpr std::string_view prompt =
        "Press Y for yes - N for no - ESC to exit";
    if (palette.size() < 256) {
        throw std::runtime_error("Invalid DOS confirmation-screen input");
    }
    validate_font(font_mask);
    DecodedImage frame;
    frame.width = width;
    frame.height = height;
    frame.palette = palette;
    frame.pixels.assign(static_cast<std::size_t>(width) * height,
                        palette.front());
    draw_dos_font_text(frame, font_mask, first_line,
                       (width - font_text_width(font_mask, first_line)) / 2,
                       97, 5);
    draw_dos_font_text(frame, font_mask, second_line,
                       (width - font_text_width(font_mask, second_line)) / 2,
                       117, 3);
    const int prompt_x = (width - font_text_width(font_mask, prompt)) / 2;
    draw_dos_font_text(frame, font_mask, prompt, prompt_x + 1, 189, 0);
    draw_dos_font_text(frame, font_mask, prompt, prompt_x, 188, 4);
    return frame;
}

DecodedImage render_dos_level_results(
    const std::vector<std::uint8_t>& font_mask,
    const std::vector<std::uint32_t>& palette,
    const bool complete, const int level_number,
    const int treasures_found, const int treasures_available,
    const int accuracy, const int skill, const int time_to_beat,
    const int elapsed_seconds) {
    constexpr int width = 320;
    constexpr int height = 200;
    if (palette.size() < 256 || level_number < 1 ||
        treasures_found < 0 || treasures_available < 0 ||
        accuracy < 0 || skill < 0 || skill > 2 ||
        time_to_beat < 0 || elapsed_seconds < 0) {
        throw std::runtime_error("Invalid DOS level-results input");
    }
    validate_font(font_mask);
    DecodedImage frame;
    frame.width = width;
    frame.height = height;
    frame.palette = palette;
    frame.pixels.assign(static_cast<std::size_t>(width) * height,
                        palette.front());
    const auto centered = [&](const std::string_view text,
                              const int y, const int style) {
        draw_dos_font_text(frame, font_mask, text,
                           (width - font_text_width(font_mask, text)) / 2,
                           y, style);
    };

    int y = complete ? 60 : 80;
    centered(complete ? "Castle complete - congratulations!"
                      : "Tough break - you'll get it next time!",
             y, 4);
    y += 16;
    centered("Results for Level " + std::to_string(level_number), y, 2);
    y += 24;
    centered("Treasures found: " + std::to_string(treasures_found) +
             "  Treasures available: " +
             std::to_string(treasures_available), y, 3);
    y += 16;
    centered("Accuracy: " + std::to_string(accuracy) + "%  ", y, 3);
    y += 16;
    if (complete) {
        constexpr std::array<std::string_view, 3> bonus = {
            "BONUS 25,000 points!", "BONUS 50,000 points!",
            "BONUS 75,000 points!",
        };
        centered(accuracy >= 100 ? bonus[static_cast<std::size_t>(skill)]
                                 : "Less than 100% - NO BONUS",
                 y, 2);
        y += 24;
        centered("Time to beat: " + std::to_string(time_to_beat) +
                 "  Your time: " + std::to_string(elapsed_seconds), y, 3);
        y += 16;
        centered(time_to_beat > elapsed_seconds
                     ? bonus[static_cast<std::size_t>(skill)]
                     : "Not fast enough - NO BONUS",
                 y, 2);
    } else {
        centered("Castle not complete", y, 2);
    }

    constexpr std::string_view prompt = "Press any key";
    const int prompt_x = (width - font_text_width(font_mask, prompt)) / 2;
    draw_dos_font_text(frame, font_mask, prompt, prompt_x + 1, 189, 0);
    draw_dos_font_text(frame, font_mask, prompt, prompt_x, 188, 4);
    return frame;
}

std::string dos_page_prompt(const int current_page, const int page_count) {
    if (page_count <= 0 || current_page < 0 || current_page >= page_count) {
        throw std::out_of_range("Invalid DOS page number");
    }
    if (page_count == 1) {
        return "Press any key to continue";
    }

    auto prompt = "Page " + std::to_string(current_page + 1) + " of " +
                  std::to_string(page_count);
    if (current_page == 0) {
        prompt += " - Press PGDN - ESC to cancel";
    } else if (current_page == page_count - 1) {
        prompt += " - Press PGUP - ESC to cancel";
    } else {
        prompt += " - Press PGUP/PGDN - ESC to cancel";
    }
    return prompt;
}

DecodedImage render_text_page(const TextPage& page,
                              const std::vector<std::uint8_t>& font_mask,
                              const std::vector<std::uint32_t>& palette,
                              const DecodedImage& bottom,
                              const TextPageRenderOptions& options) {
    constexpr int width = 320;
    constexpr int height = 200;
    constexpr int content_height = 184;
    if (palette.size() < 256 || bottom.width != width || bottom.height != 16) {
        throw std::runtime_error("Invalid text-page palette or bottom image");
    }
    if (options.page_count <= 0 || options.current_page < 0 ||
        options.current_page >= options.page_count) {
        throw std::out_of_range("Invalid text-page render options");
    }

    DecodedImage frame;
    frame.width = width;
    frame.height = height;
    frame.palette = palette;
    frame.pixels.assign(static_cast<std::size_t>(width) * height,
                        palette.front());
    blit(bottom, frame, 0, content_height);

    const auto prompt = dos_page_prompt(options.current_page,
                                        options.page_count);
    const int prompt_x = (width - font_text_width(font_mask, prompt)) / 2;
    draw_dos_font_text(frame, font_mask, prompt, prompt_x + 1, 189, 0);
    draw_dos_font_text(frame, font_mask, prompt, prompt_x, 188, 4);

    if (options.picture != nullptr) {
        blit(*options.picture, frame, options.picture_x, options.picture_y);
    }

    int text_height = 0;
    for (const auto& line : page.lines) {
        text_height += line.text.empty()
            ? page.empty_spacing
            : page.nonempty_spacing;
    }
    const bool illustrated = options.layout == TextPageLayout::illustrated;
    int y = illustrated ? (content_height - text_height) / 2
                        : (144 - text_height) / 2 + 40;
    for (const auto& line : page.lines) {
        if (!line.text.empty()) {
            const int x = !illustrated || line.x == 0
                ? (width - font_text_width(font_mask, line.text)) / 2
                : line.x;
            draw_dos_font_text(frame, font_mask, line.text, x, y,
                               line.colour);
        }
        y += line.text.empty() ? page.empty_spacing : page.nonempty_spacing;
    }
    return frame;
}

} // namespace hocus
