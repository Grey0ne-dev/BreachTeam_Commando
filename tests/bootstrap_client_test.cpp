#include <cassert>

#include "breach_team/net/bootstrap_client.hpp"

int main() {
    using breach_team::net::BootstrapClient;
    using breach_team::net::BootstrapPeerEndpoint;
    using breach_team::net::BootstrapResponse;

    std::size_t calls = 0;
    BootstrapClient client{
        "https://signal.test",
        [&calls](
            std::string_view url,
            std::string_view session_id,
            std::string_view self_peer_id
        ) -> std::optional<BootstrapResponse> {
            ++calls;
            if (url.empty() || session_id.empty() || self_peer_id.empty()) {
                return std::nullopt;
            }
            BootstrapResponse response{};
            response.session_id = std::string(session_id);
            response.peers.push_back(BootstrapPeerEndpoint{
                .peer_id = std::string(self_peer_id),
                .address = "127.0.0.1",
                .port = 30000,
            });
            return response;
        },
    };

    const auto first = client.establish_contact("session-a", "peer-a");
    assert(first.has_value());
    assert(first->session_id == "session-a");
    assert(first->peers.size() == 1);
    assert(client.contacted());
    assert(calls == 1);

    const auto second = client.establish_contact("session-a", "peer-a");
    assert(!second.has_value());
    assert(calls == 1);

    BootstrapClient default_client{"mock://signal"};
    const auto default_first = default_client.establish_contact("session-b", "peer-z");
    assert(default_first.has_value());
    assert(default_first->session_id == "session-b");
    assert(default_first->peers.size() == 1);
    assert(default_first->peers.front().peer_id == "peer-z");
}
