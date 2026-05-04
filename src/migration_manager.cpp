#include "breach_team/net/migration_manager.hpp"

#include <algorithm>

namespace breach_team::net {

MigrationManager::MigrationManager(std::string self_peer_id) : self_peer_id_(std::move(self_peer_id)) {}

void MigrationManager::set_current_host(std::string host_peer_id, std::uint32_t epoch) {
    current_host_peer_id_ = std::move(host_peer_id);
    epoch_ = epoch;
    role_ = (current_host_peer_id_ == self_peer_id_) ? HostRole::host : HostRole::follower;
}

void MigrationManager::register_checkpoint(std::uint32_t checkpoint_epoch, std::uint64_t checkpoint_tick) {
    if (checkpoint_epoch < epoch_) {
        return;
    }
    if (checkpoint_epoch > epoch_) {
        epoch_ = checkpoint_epoch;
    }
    last_checkpoint_tick_ = std::max(last_checkpoint_tick_, checkpoint_tick);
}

void MigrationManager::update_peer_metrics(PeerMetrics metrics) {
    candidates_.upsert(std::move(metrics));
}

void MigrationManager::remove_peer(std::string_view peer_id) {
    candidates_.remove(peer_id);
}

std::vector<ScoredPeer> MigrationManager::migration_targets(std::size_t peers_online) const {
    return candidates_.top_migration_targets(peers_online);
}

std::optional<MigrationDecision> MigrationManager::on_host_timeout(std::size_t peers_online) {
    if (current_host_peer_id_.empty()) {
        return std::nullopt;
    }

    const std::string previous_host = current_host_peer_id_;
    candidates_.remove(previous_host);

    auto best = candidates_.best_host(peers_online > 0 ? peers_online - 1 : 0);
    if (!best.has_value()) {
        best = ScoredPeer{self_peer_id_, 0.0};
    }

    const std::string next_host = best->peer_id;
    const std::uint32_t next_epoch = epoch_ + 1;
    const std::uint64_t next_tick = last_checkpoint_tick_ + 1;

    current_host_peer_id_ = next_host;
    epoch_ = next_epoch;
    role_ = (next_host == self_peer_id_) ? HostRole::host : HostRole::follower;

    return MigrationDecision{
        .previous_host_peer_id = previous_host,
        .new_host_peer_id = next_host,
        .new_epoch = next_epoch,
        .effective_tick = next_tick,
        .promote_self = (next_host == self_peer_id_),
    };
}

bool MigrationManager::apply_announce(const HostMigrationAnnouncePacket& announce) {
    if (announce.new_host_peer_id.empty()) {
        return false;
    }
    if (announce.new_epoch <= epoch_) {
        return false;
    }

    current_host_peer_id_ = announce.new_host_peer_id;
    epoch_ = announce.new_epoch;
    last_checkpoint_tick_ = std::max(last_checkpoint_tick_, announce.effective_tick);
    role_ = (current_host_peer_id_ == self_peer_id_) ? HostRole::host : HostRole::follower;
    return true;
}

const std::string& MigrationManager::self_peer_id() const {
    return self_peer_id_;
}

const std::string& MigrationManager::current_host_peer_id() const {
    return current_host_peer_id_;
}

std::uint32_t MigrationManager::epoch() const {
    return epoch_;
}

std::uint64_t MigrationManager::last_checkpoint_tick() const {
    return last_checkpoint_tick_;
}

HostRole MigrationManager::role() const {
    return role_;
}

}  // namespace breach_team::net
