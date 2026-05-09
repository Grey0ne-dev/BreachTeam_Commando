#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace breach_team::net {

constexpr std::uint32_t PROTOCOL_VERSION = 1;

enum class PacketType : std::uint8_t {
    hello = 1,
    input_frame = 2,
    state_checkpoint = 3,
    host_migration_announce = 4,
    host_migration_ack = 5,
};

enum InputButtonBit : std::uint16_t {
    BUTTON_MOVE_FORWARD = 1U << 0,
    BUTTON_MOVE_BACKWARD = 1U << 1,
    BUTTON_TURN_LEFT = 1U << 2,
    BUTTON_TURN_RIGHT = 1U << 3,
};

struct HelloPacket {
    std::string peer_id;
    std::uint64_t session_nonce = 0;
    std::uint32_t protocol_version = PROTOCOL_VERSION;
};

struct InputFramePacket {
    std::string peer_id;
    std::uint64_t tick = 0;
    std::uint16_t buttons = 0;
    std::int16_t look_delta = 0;
};

struct StateCheckpointPacket {
    std::string host_peer_id;
    std::uint32_t epoch = 0;
    std::uint64_t tick = 0;
    std::int32_t player_x_raw = 0;
    std::int32_t player_y_raw = 0;
    std::int32_t facing_x_raw = 0;
    std::int32_t facing_y_raw = 0;
};

struct HostMigrationAnnouncePacket {
    std::string previous_host_peer_id;
    std::string new_host_peer_id;
    std::uint32_t new_epoch = 0;
    std::uint64_t effective_tick = 0;
};

struct HostMigrationAckPacket {
    std::string peer_id;
    std::uint32_t accepted_epoch = 0;
};

using Packet = std::variant<
    HelloPacket,
    InputFramePacket,
    StateCheckpointPacket,
    HostMigrationAnnouncePacket,
    HostMigrationAckPacket>;

std::vector<std::uint8_t> serialize_packet(const Packet& packet);
std::optional<Packet> deserialize_packet(std::span<const std::uint8_t> bytes);

}  // namespace breach_team::net
