#pragma once

#include <cstddef>
#include <deque>
#include <string>

#include "breach_team/net/transport.hpp"

namespace breach_team::net {

// ENet-backed transport scaffold. Real ENet socket integration is the next step.
class EnetTransport final : public ITransport {
public:
    bool start(std::string self_peer_id) override;
    bool send(std::string_view peer_id, std::span<const std::uint8_t> payload) override;
    std::vector<TransportPacket> poll() override;

    void inject_inbound(TransportPacket packet);
    std::size_t queued_outbound() const;
    void clear_outbound();

private:
    bool started_ = false;
    std::string self_peer_id_{};
    std::deque<TransportPacket> inbound_{};
    std::deque<TransportPacket> outbound_{};
};

}  // namespace breach_team::net
