#pragma once

#include "pcx.h"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hocus {

struct TextPageLine {
    int x{};
    int colour{};
    std::string text;
};

struct TextPage {
    int nonempty_spacing{};
    int empty_spacing{};
    std::vector<TextPageLine> lines;
};

// The registered executable has two page compositors. 06B8:3230 reserves
// y=0..39 for HOCUS.IMG and centers every line in y=40..183. The illustrated
// variants (3401/364D/3899/3AE5/3D31) use y=0..183 and honor a nonzero x in
// the page metadata.
enum class TextPageLayout {
    plain,
    illustrated,
};

struct TextPageRenderOptions {
    TextPageLayout layout{TextPageLayout::plain};
    const DecodedImage* picture{};
    int picture_x{};
    int picture_y{};
    int current_page{};
    int page_count{1};
};

// 0B97:0001 is a fixed startup presentation. Fades advance once per VGA
// retrace; holds use the interruptible millisecond wait helper at 1392:0108.
enum class StartupStage {
    initial_fade_out,
    piracy_fade_in,
    piracy_hold,
    piracy_fade_out,
    apogee_fade_in,
    apogee_hold,
    apogee_fade_out,
    title_fade_in,
    title_hold,
    title_fade_out,
    done,
};

struct StartupFade {
    int numerator{};
    int denominator{1};
};

class StartupSequence {
public:
    void tick(int elapsed_milliseconds);
    // 1392:0108 ends only the current piracy/Apogee/title hold. It never
    // bypasses the later startup screens.
    void press_any_key() noexcept;
    [[nodiscard]] StartupStage stage() const noexcept { return stage_; }
    [[nodiscard]] StartupFade fade() const noexcept;
    [[nodiscard]] bool done() const noexcept {
        return stage_ == StartupStage::done;
    }

private:
    void enter(StartupStage stage) noexcept;

    StartupStage stage_{StartupStage::initial_fade_out};
    int fade_level_{40};
    int hold_milliseconds_{};
    bool advance_pending_{};
};

DecodedImage render_vga_fade(const DecodedImage& source, StartupFade fade);

// 05D8:04B0 raises every six-bit DAC component once per retrace until it
// reaches 63. The final-level WARP.PCX transition runs exactly 70 steps.
DecodedImage render_vga_whiten(const DecodedImage& source, int steps);

TextPage decode_text_page(const std::vector<std::uint8_t>& bytes);

void draw_font_text(DecodedImage& frame,
                    const std::vector<std::uint8_t>& font_mask,
                    std::string_view text, int x, int y,
                    std::uint32_t colour);

// 06B8:087C measures this font proportionally: a space is four pixels and a
// glyph advances two pixels past its rightmost set bit. Bytes outside !..z are
// control markers and consume no horizontal space.
[[nodiscard]] int font_text_width(
    const std::vector<std::uint8_t>& font_mask, std::string_view text);

// Menu labels may place '&' immediately before their accelerator character.
// The marker is not rendered; without one, the first character remains the
// original DOS accelerator.
[[nodiscard]] char dos_menu_shortcut_key(std::string_view label) noexcept;

// 06B8:071F uses eight consecutive palette entries, one per glyph row. Style
// zero is the fixed palette-80 shadow; styles one through eight start at the
// recovered C0,C8,D0,D8,E0,E8,70,68 ramps.
void draw_dos_font_text(DecodedImage& frame,
                        const std::vector<std::uint8_t>& font_mask,
                        std::string_view text, int x, int y, int style);

struct DosMenuRender {
    DecodedImage image;
    int text_x{};
    std::vector<int> item_y;
};

struct DosSlotRender {
    DecodedImage image;
    std::array<int, 9> item_y{};
};

struct DosHighScoreEntry {
    std::string name;
    std::uint32_t score{};
};

class DosMenuStarfield {
public:
    // 06B8:039C initializes 75 radial stars from SINCOS.DAT and RANDOM.DAT.
    void initialize(const std::vector<std::uint8_t>& sincos,
                    const std::vector<std::uint8_t>& random);
    void tick();
    void draw(DecodedImage& image) const;
    [[nodiscard]] bool initialized() const noexcept { return initialized_; }
    [[nodiscard]] int next_random_below(int upper_bound);
    [[nodiscard]] int random_index() const noexcept { return random_index_; }
    void set_random_index(int index) noexcept;

private:
    struct Star {
        int angle{};
        int radius{};
        int radius_step{};
        int brightness{};
        int brightness_step{};
    };

    [[nodiscard]] std::pair<int, int> position(const Star& star) const;
    void respawn(Star& star);

    std::array<std::int32_t, 360> sine_million_{};
    std::array<std::int32_t, 360> negative_cosine_million_{};
    std::array<int, 1000> random_{};
    std::array<Star, 75> stars_{};
    int random_index_{};
    bool initialized_{};
};

// Exact observable composition performed by 06B8:19A4. `lines[0]` is a
// heading when has_heading is true; selection is always zero-based among the
// selectable lines. `cursor_sheet` is the 128x15 BULLIT.IMG raster drawn into
// hidden VGA page 3 at startup; cursor_frame is the 0..7 frame selected by
// 06B8:0080, or -1 while 06B8:19A4 is fading the newly composed menu in.
[[nodiscard]] DosMenuRender render_dos_menu(
    const DecodedImage& logo, const DecodedImage& bottom,
    const DecodedImage& cursor_sheet,
    const std::vector<std::uint8_t>& font_mask,
    const std::vector<std::uint32_t>& palette,
    std::span<const std::string> lines, bool has_heading, int selection,
    int cursor_frame,
    std::string_view prompt =
        "Use UP/DOWN/LETTER to move - ENTER to select");

// Exact shared raster for 06B8:25CC and 2B8B. The save editor keeps the
// selected row in place and adds its underscore in style two.
[[nodiscard]] DosSlotRender render_dos_slot_screen(
    const DecodedImage& logo, const DecodedImage& bottom,
    const DecodedImage& cursor_sheet,
    const std::vector<std::uint8_t>& font_mask,
    const std::vector<std::uint32_t>& palette,
    std::string_view title, std::span<const std::string> slot_names,
    int selection, int cursor_frame, bool editing_name = false,
    std::string_view prompt =
        "Use UP/DOWN/NUMBER to move - ENTER to select");

// Exact common raster of 06B8:1C9B and 2018.
[[nodiscard]] DecodedImage render_dos_high_scores(
    const DecodedImage& logo, const DecodedImage& bottom,
    const std::vector<std::uint8_t>& font_mask,
    const std::vector<std::uint32_t>& palette, int episode,
    std::span<const DosHighScoreEntry> scores, int editing_rank = -1,
    std::string_view prompt = "Press any key to continue");

// Exact rasters for 06B8:137D, 17DA, and 10AC. The DOS volume words are
// represented by their visible 0..15 segment indices; key bindings are the
// 0..17 indices used by the executable's three six-key columns.
[[nodiscard]] DecodedImage render_dos_volume_screen(
    const DecodedImage& volume_bar,
    const std::vector<std::uint8_t>& font_mask,
    const std::vector<std::uint32_t>& palette,
    int sound_volume, int music_volume, int selected_row = 0,
    bool selected_visible = true);

[[nodiscard]] DecodedImage render_dos_key_screen(
    const std::vector<std::uint8_t>& font_mask,
    const std::vector<std::uint32_t>& palette,
    std::span<const std::uint8_t> bindings,
    bool choosing_key = false, int action = 0);

[[nodiscard]] DecodedImage render_dos_message_screen(
    const std::vector<std::uint8_t>& font_mask,
    const std::vector<std::uint32_t>& palette,
    std::string_view message, int style = 5,
    std::string_view prompt = "ESC to exit");

[[nodiscard]] DecodedImage render_dos_confirmation_screen(
    const std::vector<std::uint8_t>& font_mask,
    const std::vector<std::uint32_t>& palette,
    std::string_view first_line, std::string_view second_line);

[[nodiscard]] DecodedImage render_dos_level_results(
    const std::vector<std::uint8_t>& font_mask,
    const std::vector<std::uint32_t>& palette,
    bool complete, int level_number, int treasures_found,
    int treasures_available, int accuracy, int skill,
    int time_to_beat, int elapsed_seconds);

[[nodiscard]] std::string dos_page_prompt(int current_page, int page_count);

DecodedImage render_text_page(const TextPage& page,
                              const std::vector<std::uint8_t>& font_mask,
                              const std::vector<std::uint32_t>& palette,
                              const DecodedImage& bottom,
                              const TextPageRenderOptions& options = {});

} // namespace hocus
