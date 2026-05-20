#include <cassert>
#include <chrono>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

#include "breach_team/net/enet_transport.hpp"
#include "breach_team/net/protocol.hpp"

int main() {
    breach_team::net::EnetTransport transport;
    assert(!transport.start(""));

#ifdef BREACH_TEAM_HAS_ENET
    assert(transport.start("peer-a"));

    const std::vector<std::uint8_t> payload = {1, 2, 3, 4};
    assert(!transport.send("peer-b", payload));
    assert(transport.queued_outbound() == 1);
#else
    assert(!transport.start("peer-a"));

    const std::vector<std::uint8_t> payload = {1, 2, 3, 4};
    assert(!transport.send("peer-b", payload));
    assert(transport.queued_outbound() == 0);
#endif

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

#ifdef BREACH_TEAM_HAS_ENET
    breach_team::net::EnetTransport peer_a;
    breach_team::net::EnetTransport peer_b;
    const std::string endpoint_a = "127.0.0.1:39101";
    const std::string endpoint_b = "127.0.0.1:39102";
    const std::string id_a = "peer-a@" + endpoint_a;
    const std::string id_b = "peer-b@" + endpoint_b;

    assert(peer_a.start(id_a));
    assert(peer_b.start(id_b));

    const auto hello_a = breach_team::net::serialize_packet(breach_team::net::Packet{breach_team::net::HelloPacket{
        .peer_id = id_a,
        .session_nonce = 1,
        .protocol_version = breach_team::net::PROTOCOL_VERSION,
    }});
    const auto hello_b = breach_team::net::serialize_packet(breach_team::net::Packet{breach_team::net::HelloPacket{
        .peer_id = id_b,
        .session_nonce = 1,
        .protocol_version = breach_team::net::PROTOCOL_VERSION,
    }});

    bool a_saw_b = false;
    bool b_saw_a = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < deadline && (!a_saw_b || !b_saw_a)) {
        peer_a.send(endpoint_b, hello_a);
        peer_b.send(endpoint_a, hello_b);

        for (const auto& packet : peer_a.poll()) {
            const auto decoded = breach_team::net::deserialize_packet(packet.payload);
            if (decoded.has_value() && std::holds_alternative<breach_team::net::HelloPacket>(*decoded) &&
                std::get<breach_team::net::HelloPacket>(*decoded).peer_id == id_b) {
                a_saw_b = true;
            }
        }
        for (const auto& packet : peer_b.poll()) {
            const auto decoded = breach_team::net::deserialize_packet(packet.payload);
            if (decoded.has_value() && std::holds_alternative<breach_team::net::HelloPacket>(*decoded) &&
                std::get<breach_team::net::HelloPacket>(*decoded).peer_id == id_a) {
                b_saw_a = true;
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    assert(a_saw_b);
    assert(b_saw_a);
#endif
}
