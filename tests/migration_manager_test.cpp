#include <cassert>

#include "breach_team/net/migration_manager.hpp"

int main() {
    using breach_team::net::HostMigrationAnnouncePacket;
    using breach_team::net::HostRole;
    using breach_team::net::MigrationManager;

    MigrationManager manager{"peer-a"};
    manager.set_current_host("peer-b", 10);
    manager.register_checkpoint(10, 500);

    manager.update_peer_metrics({"peer-a", 1800.0, 8.0, 0.0, 0.9});
    manager.update_peer_metrics({"peer-b", 2200.0, 24.0, 0.05, 0.7});
    manager.update_peer_metrics({"peer-c", 600.0, 16.0, 0.01, 0.55});

    const auto decision = manager.on_host_timeout(3);
    assert(decision.has_value());
    assert(decision->previous_host_peer_id == "peer-b");
    assert(decision->new_host_peer_id == "peer-a");
    assert(decision->new_epoch == 11);
    assert(decision->effective_tick == 501);
    assert(decision->promote_self);
    assert(manager.role() == HostRole::host);

    const HostMigrationAnnouncePacket stale{
        .previous_host_peer_id = "peer-a",
        .new_host_peer_id = "peer-c",
        .new_epoch = 11,
        .effective_tick = 600,
    };
    assert(!manager.apply_announce(stale));
    assert(manager.current_host_peer_id() == "peer-a");

    const HostMigrationAnnouncePacket newer{
        .previous_host_peer_id = "peer-a",
        .new_host_peer_id = "peer-c",
        .new_epoch = 12,
        .effective_tick = 601,
    };
    assert(manager.apply_announce(newer));
    assert(manager.current_host_peer_id() == "peer-c");
    assert(manager.epoch() == 12);
    assert(manager.last_checkpoint_tick() == 601);
    assert(manager.role() == HostRole::follower);
}
