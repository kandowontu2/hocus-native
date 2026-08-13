#pragma once

#include "level.h"

namespace hocus {

struct LevelResults {
    int treasure_accuracy{};
    int elapsed_seconds{};
    int time_limit{};
    int treasure_bonus{};
    int time_bonus{};

    [[nodiscard]] int total_bonus() const noexcept {
        return treasure_bonus + time_bonus;
    }
};

LevelResults calculate_level_results(LevelId level, int skill,
                                     int treasures_found,
                                     int treasures_available,
                                     int elapsed_seconds);

} // namespace hocus
