#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "breach_team/net/host_candidates.hpp"
#include "breach_team/net/protocol.hpp"

namespace breach_team::net {

enum class HostRole {
    host,
    follower,
};

struct MigrationDecision {
    std::string previous_host_peer_id;
    std::string new_host_peer_id;
    std::uint32_t new_epoch = 0;
    std::uint64_t effective_tick = 0;
    bool promote_self = false;
};

class MigrationManager {
public:
    explicit MigrationManager(std::string self_peer_id);

    void set_current_host(std::string host_peer_id, std::uint32_t epoch);
    void register_checkpoint(std::uint32_t checkpoint_epoch, std::uint64_t checkpoint_tick);

    void update_peer_metrics(PeerMetrics metrics);
    void remove_peer(std::string_view peer_id);

    std::vector<ScoredPeer> migration_targets(std::size_t peers_online) const;
    std::optional<MigrationDecision> on_host_timeout(std::size_t peers_online);
    bool apply_announce(const HostMigrationAnnouncePacket& announce);

    const std::string& self_peer_id() const;
    const std::string& current_host_peer_id() const;
    std::uint32_t epoch() const;
    std::uint64_t last_checkpoint_tick() const;
    HostRole role() const;

private:
    std::string self_peer_id_;
    std::string current_host_peer_id_;
    std::uint32_t epoch_ = 0;
    std::uint64_t last_checkpoint_tick_ = 0;
    HostRole role_ = HostRole::follower;
    HostCandidates candidates_{};
};

}  // namespace breach_team::net
