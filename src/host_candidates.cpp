#include "breach_team/net/host_candidates.hpp"

#include <algorithm>
#include <cmath>

namespace breach_team::net {

void HostCandidates::upsert(PeerMetrics metrics) {
    peers_.insert_or_assign(metrics.peer_id, std::move(metrics));
}

void HostCandidates::remove(std::string_view peer_id) {
    peers_.erase(std::string(peer_id));
}

std::optional<ScoredPeer> HostCandidates::best_host(std::size_t peers_online) const {
    const auto top = top_migration_targets(peers_online);
    if (top.empty()) {
        return std::nullopt;
    }
    return top.front();
}

std::vector<ScoredPeer> HostCandidates::top_migration_targets(std::size_t peers_online) const {
    std::vector<ScoredPeer> ordered;
    ordered.reserve(peers_.size());

    for (const auto& [peer_id, metrics] : peers_) {
        ordered.push_back({peer_id, score(metrics)});
    }

    std::sort(
        ordered.begin(),
        ordered.end(),
        [](const ScoredPeer& lhs, const ScoredPeer& rhs) { return lhs.score > rhs.score; }
    );

    const std::size_t limit = std::min(compute_top_k(peers_online), ordered.size());
    ordered.resize(limit);
    return ordered;
}

std::size_t HostCandidates::compute_top_k(std::size_t peers_online) {
    if (peers_online <= 1) {
        return 1;
    }
    return static_cast<std::size_t>(std::ceil(std::log2(static_cast<double>(peers_online))));
}

double HostCandidates::score(const PeerMetrics& metrics) {
    const double uptime_component = std::log1p(std::max(0.0, metrics.uptime_seconds)) * 2.0;
    const double latency_penalty = std::max(0.0, metrics.rtt_ms) * 0.03;
    const double loss_penalty = std::clamp(metrics.packet_loss_ratio, 0.0, 1.0) * 30.0;
    const double cpu_component = std::clamp(metrics.cpu_idle_ratio, 0.0, 1.0) * 8.0;
    return uptime_component + cpu_component - latency_penalty - loss_penalty;
}

}  // namespace breach_team::net
