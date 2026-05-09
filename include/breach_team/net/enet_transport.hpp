#pragma once

#include <cstddef>
#include <deque>
#include <string>
#include <unordered_map>

#include "breach_team/net/transport.hpp"

namespace breach_team::net {

class EnetTransport final : public ITransport {
public:
    EnetTransport() = default;
    ~EnetTransport() override;

    EnetTransport(const EnetTransport&) = delete;
    EnetTransport& operator=(const EnetTransport&) = delete;
    EnetTransport(EnetTransport&&) = delete;
    EnetTransport& operator=(EnetTransport&&) = delete;

    bool start(std::string self_peer_id) override;
    bool send(std::string_view peer_id, std::span<const std::uint8_t> payload) override;
    std::vector<TransportPacket> poll() override;

    void inject_inbound(TransportPacket packet);
    std::size_t queued_outbound() const;
    void clear_outbound();

private:
    void shutdown();

    bool started_ = false;
    std::string self_peer_id_{};
    std::deque<TransportPacket> inbound_{};
    std::deque<TransportPacket> outbound_{};

#ifdef BREACH_TEAM_HAS_ENET
    void* host_ = nullptr;
    std::unordered_map<std::string, void*> peers_by_id_{};
    std::unordered_map<void*, std::string> ids_by_peer_{};
#endif
};

}  // namespace breach_team::net
