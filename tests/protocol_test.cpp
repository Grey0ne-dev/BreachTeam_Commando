#include <cassert>
#include <cstdint>
#include <variant>
#include <vector>

#include "breach_team/net/protocol.hpp"

int main() {
    using breach_team::net::HostMigrationAnnouncePacket;
    using breach_team::net::InputFramePacket;
    using breach_team::net::Packet;
    using breach_team::net::StateCheckpointPacket;

    {
        Packet packet = InputFramePacket{
            .peer_id = "peer-1",
            .tick = 77,
            .buttons = 0b1011,
            .look_delta = -9,
        };

        const auto encoded = breach_team::net::serialize_packet(packet);
        const auto decoded = breach_team::net::deserialize_packet(encoded);
        assert(decoded.has_value());
        assert(std::holds_alternative<InputFramePacket>(*decoded));
        const auto& roundtrip = std::get<InputFramePacket>(*decoded);
        assert(roundtrip.peer_id == "peer-1");
        assert(roundtrip.tick == 77);
        assert(roundtrip.buttons == 0b1011);
        assert(roundtrip.look_delta == -9);
    }

    {
        Packet packet = StateCheckpointPacket{
            .host_peer_id = "host-1",
            .epoch = 5,
            .tick = 4096,
            .player_x_raw = 1024,
            .player_y_raw = -256,
            .facing_x_raw = 65536,
            .facing_y_raw = 0,
        };

        const auto encoded = breach_team::net::serialize_packet(packet);
        const auto decoded = breach_team::net::deserialize_packet(encoded);
        assert(decoded.has_value());
        assert(std::holds_alternative<StateCheckpointPacket>(*decoded));
        const auto& roundtrip = std::get<StateCheckpointPacket>(*decoded);
        assert(roundtrip.host_peer_id == "host-1");
        assert(roundtrip.epoch == 5);
        assert(roundtrip.tick == 4096);
        assert(roundtrip.player_x_raw == 1024);
        assert(roundtrip.player_y_raw == -256);
    }

    {
        Packet packet = HostMigrationAnnouncePacket{
            .previous_host_peer_id = "host-1",
            .new_host_peer_id = "peer-2",
            .new_epoch = 6,
            .effective_tick = 4200,
        };

        const auto encoded = breach_team::net::serialize_packet(packet);
        const auto decoded = breach_team::net::deserialize_packet(encoded);
        assert(decoded.has_value());
        assert(std::holds_alternative<HostMigrationAnnouncePacket>(*decoded));
        const auto& roundtrip = std::get<HostMigrationAnnouncePacket>(*decoded);
        assert(roundtrip.previous_host_peer_id == "host-1");
        assert(roundtrip.new_host_peer_id == "peer-2");
        assert(roundtrip.new_epoch == 6);
        assert(roundtrip.effective_tick == 4200);
    }

    {
        std::vector<std::uint8_t> invalid = {255, 0, 1, 2};
        const auto decoded = breach_team::net::deserialize_packet(invalid);
        assert(!decoded.has_value());
    }
}
