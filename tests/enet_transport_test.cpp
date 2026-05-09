#include <cassert>
#include <cstdint>
#include <vector>

#include "breach_team/net/enet_transport.hpp"

int main() {
    breach_team::net::EnetTransport transport;
    assert(!transport.start(""));
    assert(transport.start("peer-a"));

    const std::vector<std::uint8_t> payload = {1, 2, 3, 4};
    assert(transport.send("peer-b", payload));
    assert(transport.queued_outbound() == 1);

    breach_team::net::TransportPacket inbound{};
    inbound.peer_id = "peer-z";
    inbound.payload = {9, 8};
    transport.inject_inbound(inbound);

    const auto packets = transport.poll();
    assert(packets.size() == 1);
    assert(packets.front().peer_id == "peer-z");
    assert(packets.front().payload.size() == 2);
    assert(packets.front().payload[0] == 9);
    assert(packets.front().payload[1] == 8);

    transport.clear_outbound();
    assert(transport.queued_outbound() == 0);
}
