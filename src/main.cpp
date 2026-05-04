#include <chrono>
#include <iostream>

#include "breach_team/game/game_session.hpp"
#include "breach_team/net/host_candidates.hpp"
#include "breach_team/net/migration_manager.hpp"
#include "breach_team/net/protocol.hpp"

int main() {
    breach_team::game::GameSession session;
    breach_team::game::FrameInput input{};
    input.move_forward = true;

    for (int frame = 0; frame < 60; ++frame) {
        session.simulate_for(std::chrono::milliseconds(16), input);
    }

    breach_team::net::HostCandidates candidates;
    candidates.upsert({"peer-a", 120.0, 24.0, 0.01, 0.72});
    candidates.upsert({"peer-b", 540.0, 42.0, 0.02, 0.61});
    candidates.upsert({"peer-c", 60.0, 12.0, 0.00, 0.93});

    const auto host = candidates.best_host(3);
    breach_team::net::MigrationManager migration{"peer-a"};
    migration.set_current_host("peer-b", 7);
    migration.register_checkpoint(7, session.tick_count());
    migration.update_peer_metrics({"peer-a", 240.0, 18.0, 0.00, 0.84});
    migration.update_peer_metrics({"peer-c", 120.0, 12.0, 0.01, 0.55});

    const auto decision = migration.on_host_timeout(3);
    if (decision.has_value()) {
        breach_team::net::HostMigrationAnnouncePacket announce{
            .previous_host_peer_id = decision->previous_host_peer_id,
            .new_host_peer_id = decision->new_host_peer_id,
            .new_epoch = decision->new_epoch,
            .effective_tick = decision->effective_tick,
        };
        const auto encoded = breach_team::net::serialize_packet(announce);
        const auto decoded = breach_team::net::deserialize_packet(encoded);

        if (decoded.has_value() &&
            std::holds_alternative<breach_team::net::HostMigrationAnnouncePacket>(*decoded)) {
            const auto& packet = std::get<breach_team::net::HostMigrationAnnouncePacket>(*decoded);
            std::cout << "migration previous=" << packet.previous_host_peer_id
                      << " next=" << packet.new_host_peer_id
                      << " epoch=" << packet.new_epoch
                      << " tick=" << packet.effective_tick << '\n';
        }
    }

    std::cout << "ticks=" << session.tick_count()
              << " pos=(" << session.player().position.x.to_double() << ","
              << session.player().position.y.to_double() << ")" << '\n';

    if (host.has_value()) {
        std::cout << "host-candidate=" << host->peer_id << " score=" << host->score << '\n';
    } else {
        std::cout << "host-candidate=none\n";
    }

    return 0;
}
