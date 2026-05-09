#include "breach_team/net/input_pipeline.hpp"

#include <algorithm>

namespace breach_team::net {

InputFrameBuffer::InputFrameBuffer(std::string authoritative_peer_id)
    : authoritative_peer_id_(std::move(authoritative_peer_id)) {}

void InputFrameBuffer::set_authoritative_peer(std::string peer_id) {
    authoritative_peer_id_ = std::move(peer_id);
}

void InputFrameBuffer::enqueue(const InputFramePacket& packet) {
    pending_.erase(
        std::remove_if(
            pending_.begin(),
            pending_.end(),
            [&packet](const InputFramePacket& existing) {
                return existing.tick == packet.tick && existing.peer_id == packet.peer_id;
            }
        ),
        pending_.end()
    );

    pending_.push_back(packet);
    std::stable_sort(
        pending_.begin(),
        pending_.end(),
        [](const InputFramePacket& lhs, const InputFramePacket& rhs) {
            if (lhs.tick != rhs.tick) {
                return lhs.tick < rhs.tick;
            }
            return lhs.peer_id < rhs.peer_id;
        }
    );
}

std::optional<InputFramePacket> InputFrameBuffer::take_for_tick(std::uint64_t tick) {
    const auto start = std::find_if(
        pending_.begin(),
        pending_.end(),
        [tick](const InputFramePacket& packet) { return packet.tick == tick; }
    );
    if (start == pending_.end()) {
        return std::nullopt;
    }

    auto end = start;
    while (end != pending_.end() && end->tick == tick) {
        ++end;
    }

    auto chosen = start;
    if (!authoritative_peer_id_.empty()) {
        const auto authoritative = std::find_if(
            start,
            end,
            [this](const InputFramePacket& packet) { return packet.peer_id == authoritative_peer_id_; }
        );
        if (authoritative != end) {
            chosen = authoritative;
        }
    }

    InputFramePacket packet = *chosen;
    pending_.erase(start, end);
    return packet;
}

game::FrameInput to_frame_input(const InputFramePacket& packet) {
    game::FrameInput input{};
    input.move_forward = (packet.buttons & BUTTON_MOVE_FORWARD) != 0;
    input.move_backward = (packet.buttons & BUTTON_MOVE_BACKWARD) != 0;
    input.turn_left = (packet.buttons & BUTTON_TURN_LEFT) != 0;
    input.turn_right = (packet.buttons & BUTTON_TURN_RIGHT) != 0;
    return input;
}

}  // namespace breach_team::net
