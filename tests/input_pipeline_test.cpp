#include <cassert>

#include "breach_team/game/game_session.hpp"
#include "breach_team/net/input_pipeline.hpp"
#include "breach_team/net/protocol.hpp"

int main() {
    using breach_team::game::GameSession;
    using breach_team::net::BUTTON_MOVE_FORWARD;
    using breach_team::net::BUTTON_MOVE_BACKWARD;
    using breach_team::net::BUTTON_TURN_LEFT;
    using breach_team::net::InputFrameBuffer;
    using breach_team::net::InputFramePacket;

    InputFrameBuffer buffer{"peer-a"};
    buffer.enqueue(InputFramePacket{
        .peer_id = "peer-a",
        .tick = 2,
        .buttons = BUTTON_MOVE_FORWARD,
        .look_delta = 0,
    });
    buffer.enqueue(InputFramePacket{
        .peer_id = "peer-a",
        .tick = 1,
        .buttons = static_cast<std::uint16_t>(BUTTON_MOVE_FORWARD | BUTTON_TURN_LEFT),
        .look_delta = 0,
    });
    buffer.enqueue(InputFramePacket{
        .peer_id = "peer-b",
        .tick = 1,
        .buttons = BUTTON_MOVE_BACKWARD,
        .look_delta = 0,
    });

    GameSession session;
    for (std::uint64_t tick = 1; tick <= 2; ++tick) {
        const auto packet = buffer.take_for_tick(tick);
        assert(packet.has_value());
        session.simulate_tick(breach_team::net::to_frame_input(*packet));
    }

    assert(session.tick_count() == 2);
    assert(session.player().position.x.to_double() > 0.0);
    assert(session.player().position.y.to_double() > 0.0);
    assert(!buffer.take_for_tick(1).has_value());

    InputFrameBuffer deterministic_fallback;
    deterministic_fallback.enqueue(InputFramePacket{
        .peer_id = "peer-z",
        .tick = 5,
        .buttons = BUTTON_MOVE_BACKWARD,
        .look_delta = 0,
    });
    deterministic_fallback.enqueue(InputFramePacket{
        .peer_id = "peer-c",
        .tick = 5,
        .buttons = BUTTON_MOVE_FORWARD,
        .look_delta = 0,
    });
    const auto fallback_packet = deterministic_fallback.take_for_tick(5);
    assert(fallback_packet.has_value());
    assert(fallback_packet->peer_id == "peer-c");
}
