#pragma once

#include <chrono>
#include <cstdint>

#include "breach_team/core/math.hpp"

namespace breach_team::game {

struct FrameInput {
    bool move_forward = false;
    bool move_backward = false;
    bool turn_left = false;
    bool turn_right = false;
};

struct PlayerState {
    core::Vec2Fixed position{
        core::Fixed16::from_int(0),
        core::Fixed16::from_int(0),
    };
    core::Vec2Fixed facing{
        core::Fixed16::from_int(1),
        core::Fixed16::from_int(0),
    };
};

class GameSession {
public:
    static constexpr std::chrono::milliseconds TICK_INTERVAL{16};

    void simulate_for(std::chrono::milliseconds elapsed, const FrameInput& input);
    std::uint64_t tick_count() const;
    const PlayerState& player() const;

private:
    void step(const FrameInput& input);

    std::chrono::milliseconds accumulator_{0};
    std::uint64_t tick_count_ = 0;
    PlayerState player_{};
};

}  // namespace breach_team::game
