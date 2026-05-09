#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "breach_team/game/game_session.hpp"
#include "breach_team/net/protocol.hpp"

namespace breach_team::net {

class InputFrameBuffer {
public:
    explicit InputFrameBuffer(std::string authoritative_peer_id = {});

    void set_authoritative_peer(std::string peer_id);
    void enqueue(const InputFramePacket& packet);
    std::optional<InputFramePacket> take_for_tick(std::uint64_t tick);

private:
    std::string authoritative_peer_id_{};
    std::vector<InputFramePacket> pending_{};
};

game::FrameInput to_frame_input(const InputFramePacket& packet);

}  // namespace breach_team::net
