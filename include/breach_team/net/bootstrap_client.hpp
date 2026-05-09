#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace breach_team::net {

struct BootstrapPeerEndpoint {
    std::string peer_id;
    std::string address;
    std::uint16_t port = 0;
};

struct BootstrapResponse {
    std::string session_id;
    std::vector<BootstrapPeerEndpoint> peers;
};

class BootstrapClient {
public:
    using FetchFn =
        std::function<std::optional<BootstrapResponse>(std::string_view, std::string_view, std::string_view)>;

    explicit BootstrapClient(std::string signaling_url, FetchFn fetch = {});

    std::optional<BootstrapResponse> establish_contact(std::string_view session_id, std::string_view self_peer_id);
    bool contacted() const;

private:
    std::string signaling_url_;
    FetchFn fetch_;
    bool contacted_ = false;
};

}  // namespace breach_team::net
