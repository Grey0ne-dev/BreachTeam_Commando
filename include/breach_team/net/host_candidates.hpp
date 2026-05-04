#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace breach_team::net {

struct PeerMetrics {
    std::string peer_id;
    double uptime_seconds = 0.0;
    double rtt_ms = 0.0;
    double packet_loss_ratio = 0.0;
    double cpu_idle_ratio = 0.0;
};

struct ScoredPeer {
    std::string peer_id;
    double score = 0.0;
};

class HostCandidates {
public:
    void upsert(PeerMetrics metrics);
    void remove(std::string_view peer_id);

    std::optional<ScoredPeer> best_host(std::size_t peers_online) const;
    std::vector<ScoredPeer> top_migration_targets(std::size_t peers_online) const;

    static std::size_t compute_top_k(std::size_t peers_online);

private:
    static double score(const PeerMetrics& metrics);

    std::unordered_map<std::string, PeerMetrics> peers_;
};

}  // namespace breach_team::net
