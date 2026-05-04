#include <cassert>
#include <vector>

#include "breach_team/net/host_candidates.hpp"

int main() {
    using breach_team::net::HostCandidates;
    using breach_team::net::PeerMetrics;

    assert(HostCandidates::compute_top_k(1) == 1);
    assert(HostCandidates::compute_top_k(2) == 1);
    assert(HostCandidates::compute_top_k(3) == 2);
    assert(HostCandidates::compute_top_k(8) == 3);

    HostCandidates queue;
    std::vector<PeerMetrics> peers{
        {"stable-fast", 1000.0, 10.0, 0.00, 0.9},
        {"stable-slow", 1000.0, 80.0, 0.00, 0.9},
        {"lossy", 500.0, 10.0, 0.4, 0.95},
        {"fresh", 12.0, 8.0, 0.00, 0.5},
    };

    for (auto& p : peers) {
        queue.upsert(p);
    }

    const auto top = queue.top_migration_targets(4);
    assert(top.size() == 2);
    assert(top.front().peer_id == "stable-fast");

    const auto best = queue.best_host(4);
    assert(best.has_value());
    assert(best->peer_id == "stable-fast");
}
