#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace breach_team::net {

struct TransportPacket {
    std::string peer_id;
    std::vector<std::uint8_t> payload;
};

class ITransport {
public:
    virtual ~ITransport() = default;

    virtual bool start(std::string self_peer_id) = 0;
    virtual bool send(std::string_view peer_id, std::span<const std::uint8_t> payload) = 0;
    virtual std::vector<TransportPacket> poll() = 0;
};

}  // namespace breach_team::net
