#include "breach_team/net/enet_transport.hpp"

namespace breach_team::net {

bool EnetTransport::start(std::string self_peer_id) {
    self_peer_id_ = std::move(self_peer_id);
    started_ = !self_peer_id_.empty();
    return started_;
}

bool EnetTransport::send(std::string_view peer_id, std::span<const std::uint8_t> payload) {
    if (!started_ || peer_id.empty()) {
        return false;
    }

    TransportPacket packet{};
    packet.peer_id = std::string(peer_id);
    packet.payload.assign(payload.begin(), payload.end());
    outbound_.push_back(std::move(packet));
    return true;
}

std::vector<TransportPacket> EnetTransport::poll() {
    std::vector<TransportPacket> packets;
    packets.reserve(inbound_.size());

    while (!inbound_.empty()) {
        packets.push_back(std::move(inbound_.front()));
        inbound_.pop_front();
    }

    return packets;
}

void EnetTransport::inject_inbound(TransportPacket packet) {
    inbound_.push_back(std::move(packet));
}

std::size_t EnetTransport::queued_outbound() const {
    return outbound_.size();
}

void EnetTransport::clear_outbound() {
    outbound_.clear();
}

}  // namespace breach_team::net
