#include "breach_team/net/protocol.hpp"

#include <array>
#include <cstddef>
#include <cstring>

namespace breach_team::net {

namespace {

class ByteWriter {
public:
    void write_u8(std::uint8_t value) {
        bytes_.push_back(value);
    }

    void write_u16(std::uint16_t value) {
        write_integral(value);
    }

    void write_u32(std::uint32_t value) {
        write_integral(value);
    }

    void write_u64(std::uint64_t value) {
        write_integral(value);
    }

    void write_i16(std::int16_t value) {
        write_integral(static_cast<std::uint16_t>(value));
    }

    void write_i32(std::int32_t value) {
        write_integral(static_cast<std::uint32_t>(value));
    }

    void write_string(const std::string& value) {
        if (value.size() > 255) {
            write_u8(0);
            return;
        }

        write_u8(static_cast<std::uint8_t>(value.size()));
        bytes_.insert(bytes_.end(), value.begin(), value.end());
    }

    std::vector<std::uint8_t> take() {
        return std::move(bytes_);
    }

private:
    template <typename T>
    void write_integral(T value) {
        for (std::size_t idx = 0; idx < sizeof(T); ++idx) {
            bytes_.push_back(static_cast<std::uint8_t>((value >> (idx * 8)) & static_cast<T>(0xFF)));
        }
    }

    std::vector<std::uint8_t> bytes_{};
};

class ByteReader {
public:
    explicit ByteReader(std::span<const std::uint8_t> bytes) : bytes_(bytes) {}

    std::optional<std::uint8_t> read_u8() {
        if (!has(1)) {
            return std::nullopt;
        }
        return bytes_[cursor_++];
    }

    std::optional<std::uint16_t> read_u16() {
        return read_integral<std::uint16_t>();
    }

    std::optional<std::uint32_t> read_u32() {
        return read_integral<std::uint32_t>();
    }

    std::optional<std::uint64_t> read_u64() {
        return read_integral<std::uint64_t>();
    }

    std::optional<std::int16_t> read_i16() {
        const auto value = read_u16();
        if (!value.has_value()) {
            return std::nullopt;
        }
        return static_cast<std::int16_t>(*value);
    }

    std::optional<std::int32_t> read_i32() {
        const auto value = read_u32();
        if (!value.has_value()) {
            return std::nullopt;
        }
        return static_cast<std::int32_t>(*value);
    }

    std::optional<std::string> read_string() {
        const auto size = read_u8();
        if (!size.has_value()) {
            return std::nullopt;
        }

        if (!has(*size)) {
            return std::nullopt;
        }

        std::string value;
        value.resize(*size);
        std::memcpy(value.data(), bytes_.data() + cursor_, *size);
        cursor_ += *size;
        return value;
    }

    bool consumed_all() const {
        return cursor_ == bytes_.size();
    }

private:
    bool has(std::size_t count) const {
        return cursor_ + count <= bytes_.size();
    }

    template <typename T>
    std::optional<T> read_integral() {
        if (!has(sizeof(T))) {
            return std::nullopt;
        }

        T value = 0;
        for (std::size_t idx = 0; idx < sizeof(T); ++idx) {
            value |= static_cast<T>(bytes_[cursor_++]) << (idx * 8);
        }
        return value;
    }

    std::span<const std::uint8_t> bytes_{};
    std::size_t cursor_ = 0;
};

template <typename TPacket>
std::vector<std::uint8_t> serialize_with_type(PacketType type, const TPacket& packet);

template <>
std::vector<std::uint8_t> serialize_with_type(PacketType type, const HelloPacket& packet) {
    ByteWriter writer;
    writer.write_u8(static_cast<std::uint8_t>(type));
    writer.write_string(packet.peer_id);
    writer.write_u64(packet.session_nonce);
    writer.write_u32(packet.protocol_version);
    return writer.take();
}

template <>
std::vector<std::uint8_t> serialize_with_type(PacketType type, const InputFramePacket& packet) {
    ByteWriter writer;
    writer.write_u8(static_cast<std::uint8_t>(type));
    writer.write_string(packet.peer_id);
    writer.write_u64(packet.tick);
    writer.write_u16(packet.buttons);
    writer.write_i16(packet.look_delta);
    return writer.take();
}

template <>
std::vector<std::uint8_t> serialize_with_type(PacketType type, const StateCheckpointPacket& packet) {
    ByteWriter writer;
    writer.write_u8(static_cast<std::uint8_t>(type));
    writer.write_string(packet.host_peer_id);
    writer.write_u32(packet.epoch);
    writer.write_u64(packet.tick);
    writer.write_i32(packet.player_x_raw);
    writer.write_i32(packet.player_y_raw);
    writer.write_i32(packet.facing_x_raw);
    writer.write_i32(packet.facing_y_raw);
    return writer.take();
}

template <>
std::vector<std::uint8_t> serialize_with_type(PacketType type, const HostMigrationAnnouncePacket& packet) {
    ByteWriter writer;
    writer.write_u8(static_cast<std::uint8_t>(type));
    writer.write_string(packet.previous_host_peer_id);
    writer.write_string(packet.new_host_peer_id);
    writer.write_u32(packet.new_epoch);
    writer.write_u64(packet.effective_tick);
    return writer.take();
}

template <>
std::vector<std::uint8_t> serialize_with_type(PacketType type, const HostMigrationAckPacket& packet) {
    ByteWriter writer;
    writer.write_u8(static_cast<std::uint8_t>(type));
    writer.write_string(packet.peer_id);
    writer.write_u32(packet.accepted_epoch);
    return writer.take();
}

}  // namespace

std::vector<std::uint8_t> serialize_packet(const Packet& packet) {
    return std::visit(
        [](const auto& value) -> std::vector<std::uint8_t> {
            using TValue = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<TValue, HelloPacket>) {
                return serialize_with_type(PacketType::hello, value);
            } else if constexpr (std::is_same_v<TValue, InputFramePacket>) {
                return serialize_with_type(PacketType::input_frame, value);
            } else if constexpr (std::is_same_v<TValue, StateCheckpointPacket>) {
                return serialize_with_type(PacketType::state_checkpoint, value);
            } else if constexpr (std::is_same_v<TValue, HostMigrationAnnouncePacket>) {
                return serialize_with_type(PacketType::host_migration_announce, value);
            } else {
                return serialize_with_type(PacketType::host_migration_ack, value);
            }
        },
        packet
    );
}

std::optional<Packet> deserialize_packet(std::span<const std::uint8_t> bytes) {
    ByteReader reader(bytes);
    const auto type_raw = reader.read_u8();
    if (!type_raw.has_value()) {
        return std::nullopt;
    }

    const auto type = static_cast<PacketType>(*type_raw);
    switch (type) {
        case PacketType::hello: {
            HelloPacket packet{};
            const auto peer_id = reader.read_string();
            const auto nonce = reader.read_u64();
            const auto version = reader.read_u32();
            if (!peer_id.has_value() || !nonce.has_value() || !version.has_value() || !reader.consumed_all()) {
                return std::nullopt;
            }
            packet.peer_id = *peer_id;
            packet.session_nonce = *nonce;
            packet.protocol_version = *version;
            return packet;
        }
        case PacketType::input_frame: {
            InputFramePacket packet{};
            const auto peer_id = reader.read_string();
            const auto tick = reader.read_u64();
            const auto buttons = reader.read_u16();
            const auto look_delta = reader.read_i16();
            if (!peer_id.has_value() || !tick.has_value() || !buttons.has_value() || !look_delta.has_value() ||
                !reader.consumed_all()) {
                return std::nullopt;
            }
            packet.peer_id = *peer_id;
            packet.tick = *tick;
            packet.buttons = *buttons;
            packet.look_delta = *look_delta;
            return packet;
        }
        case PacketType::state_checkpoint: {
            StateCheckpointPacket packet{};
            const auto host_peer_id = reader.read_string();
            const auto epoch = reader.read_u32();
            const auto tick = reader.read_u64();
            const auto player_x_raw = reader.read_i32();
            const auto player_y_raw = reader.read_i32();
            const auto facing_x_raw = reader.read_i32();
            const auto facing_y_raw = reader.read_i32();
            if (!host_peer_id.has_value() || !epoch.has_value() || !tick.has_value() || !player_x_raw.has_value() ||
                !player_y_raw.has_value() || !facing_x_raw.has_value() || !facing_y_raw.has_value() ||
                !reader.consumed_all()) {
                return std::nullopt;
            }
            packet.host_peer_id = *host_peer_id;
            packet.epoch = *epoch;
            packet.tick = *tick;
            packet.player_x_raw = *player_x_raw;
            packet.player_y_raw = *player_y_raw;
            packet.facing_x_raw = *facing_x_raw;
            packet.facing_y_raw = *facing_y_raw;
            return packet;
        }
        case PacketType::host_migration_announce: {
            HostMigrationAnnouncePacket packet{};
            const auto previous_host_peer_id = reader.read_string();
            const auto new_host_peer_id = reader.read_string();
            const auto new_epoch = reader.read_u32();
            const auto effective_tick = reader.read_u64();
            if (!previous_host_peer_id.has_value() || !new_host_peer_id.has_value() || !new_epoch.has_value() ||
                !effective_tick.has_value() || !reader.consumed_all()) {
                return std::nullopt;
            }
            packet.previous_host_peer_id = *previous_host_peer_id;
            packet.new_host_peer_id = *new_host_peer_id;
            packet.new_epoch = *new_epoch;
            packet.effective_tick = *effective_tick;
            return packet;
        }
        case PacketType::host_migration_ack: {
            HostMigrationAckPacket packet{};
            const auto peer_id = reader.read_string();
            const auto accepted_epoch = reader.read_u32();
            if (!peer_id.has_value() || !accepted_epoch.has_value() || !reader.consumed_all()) {
                return std::nullopt;
            }
            packet.peer_id = *peer_id;
            packet.accepted_epoch = *accepted_epoch;
            return packet;
        }
        default:
            return std::nullopt;
    }
}

}  // namespace breach_team::net
