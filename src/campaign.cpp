#include "campaign.h"

#include <array>
#include <stdexcept>

namespace hocus {

namespace {

constexpr std::array<std::array<int, 9>, 4> level_time_limits = {{
    {{150, 300, 400, 350, 350, 350, 420, 350, 400}},
    {{325, 275, 300, 350, 400, 350, 400, 375, 250}},
    {{525, 450, 520, 475, 525, 450, 550, 450, 300}},
    {{480, 375, 350, 300, 400, 350, 450, 400, 180}},
}};
constexpr std::array<int, 3> completion_bonuses = {25000, 50000, 75000};

} // namespace

LevelResults calculate_level_results(const LevelId level, const int skill,
                                     const int treasures_found,
                                     const int treasures_available,
                                     const int elapsed_seconds) {
    if (level.episode < 1 || level.episode > 4 ||
        level.number < 1 || level.number > 9 || skill < 0 || skill > 2 ||
        treasures_found < 0 || treasures_available < 0 ||
        treasures_found > treasures_available || elapsed_seconds < 0) {
        throw std::out_of_range("Invalid Hocus level-result state");
    }

    LevelResults result;
    result.treasure_accuracy = treasures_available == 0
        ? 0
        : treasures_found * 100 / treasures_available;
    result.elapsed_seconds = elapsed_seconds;
    result.time_limit = level_time_limits
        [static_cast<std::size_t>(level.episode - 1)]
        [static_cast<std::size_t>(level.number - 1)];
    const int bonus = completion_bonuses[static_cast<std::size_t>(skill)];
    result.treasure_bonus = result.treasure_accuracy == 100 ? bonus : 0;
    result.time_bonus = elapsed_seconds < result.time_limit ? bonus : 0;
    return result;
}

} // namespace hocus
