#include "breach_team/game/game_session.hpp"

namespace breach_team::game {

namespace {

core::Mat2Fixed rotation_matrix(bool left_turn) {
    // Approximate 2D rotation step with fixed-point coefficients for deterministic stepping.
    const auto cos_v = core::Fixed16::from_double(0.9995);
    const auto sin_v = core::Fixed16::from_double(0.0314);

    if (left_turn) {
        return {cos_v, core::Fixed16::from_raw(-sin_v.raw()), sin_v, cos_v};
    }

    return {cos_v, sin_v, core::Fixed16::from_raw(-sin_v.raw()), cos_v};
}

}  // namespace

void GameSession::simulate_tick(const FrameInput& input) {
    step(input);
}

void GameSession::simulate_for(std::chrono::milliseconds elapsed, const FrameInput& input) {
    accumulator_ += elapsed;
    while (accumulator_ >= TICK_INTERVAL) {
        simulate_tick(input);
        accumulator_ -= TICK_INTERVAL;
    }
}

std::uint64_t GameSession::tick_count() const {
    return tick_count_;
}

const PlayerState& GameSession::player() const {
    return player_;
}

void GameSession::step(const FrameInput& input) {
    if (input.turn_left) {
        player_.facing = rotation_matrix(true) * player_.facing;
    } else if (input.turn_right) {
        player_.facing = rotation_matrix(false) * player_.facing;
    }

    const auto speed = core::Fixed16::from_double(0.08);
    if (input.move_forward) {
        player_.position = player_.position + (player_.facing * speed);
    } else if (input.move_backward) {
        player_.position = player_.position - (player_.facing * speed);
    }

    ++tick_count_;
}

}  // namespace breach_team::game
