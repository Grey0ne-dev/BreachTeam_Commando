#include "breach_team/net/bootstrap_client.hpp"

#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <regex>
#include <sstream>

namespace breach_team::net {

namespace {

bool is_safe_token(std::string_view value) {
    if (value.empty()) {
        return false;
    }

    for (const char c : value) {
        const bool safe = std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '-' || c == '_' || c == '.';
        if (!safe) {
            return false;
        }
    }
    return true;
}

bool is_safe_url(std::string_view url) {
    if (url.empty()) {
        return false;
    }

    for (const char c : url) {
        const bool safe = std::isalnum(static_cast<unsigned char>(c)) != 0 || c == ':' || c == '/' || c == '?' ||
                          c == '&' || c == '=' || c == '_' || c == '-' || c == '.' || c == '%' || c == '+' ||
                          c == '#';
        if (!safe) {
            return false;
        }
    }
    return true;
}

std::optional<BootstrapResponse> parse_line_payload(std::string_view payload) {
    BootstrapResponse response{};
    std::stringstream stream{std::string(payload)};
    std::string line;

    while (std::getline(stream, line)) {
        if (line.rfind("session_id=", 0) == 0) {
            response.session_id = line.substr(std::string("session_id=").size());
            continue;
        }

        if (line.rfind("peer=", 0) != 0) {
            continue;
        }

        const std::string data = line.substr(std::string("peer=").size());
        const std::size_t first_comma = data.find(',');
        const std::size_t second_comma = data.find(',', first_comma == std::string::npos ? first_comma : first_comma + 1);
        if (first_comma == std::string::npos || second_comma == std::string::npos) {
            continue;
        }

        const std::string peer_id = data.substr(0, first_comma);
        const std::string address = data.substr(first_comma + 1, second_comma - first_comma - 1);
        const std::string port_value = data.substr(second_comma + 1);
        if (peer_id.empty() || address.empty() || port_value.empty()) {
            continue;
        }

        char* end_ptr = nullptr;
        const unsigned long port_num = std::strtoul(port_value.c_str(), &end_ptr, 10);
        if (end_ptr == nullptr || *end_ptr != '\0' || port_num > 65535UL) {
            continue;
        }

        response.peers.push_back(BootstrapPeerEndpoint{
            .peer_id = peer_id,
            .address = address,
            .port = static_cast<std::uint16_t>(port_num),
        });
    }

    if (response.session_id.empty() || response.peers.empty()) {
        return std::nullopt;
    }
    return response;
}

std::optional<BootstrapResponse> parse_json_payload(std::string_view payload) {
    static const std::regex session_re(R"re("session_id"\s*:\s*"([^"]+)")re");
    static const std::regex object_re(R"re(\{[^{}]*\})re");
    static const std::regex peer_id_re(R"re("peer_id"\s*:\s*"([^"]+)")re");
    static const std::regex address_re(R"re("address"\s*:\s*"([^"]+)")re");
    static const std::regex port_re(R"re("port"\s*:\s*([0-9]{1,5}))re");

    std::match_results<std::string_view::const_iterator> session_match;
    if (!std::regex_search(payload.begin(), payload.end(), session_match, session_re) || session_match.size() < 2) {
        return std::nullopt;
    }

    BootstrapResponse response{};
    response.session_id = session_match[1].str();

    const std::size_t peers_key = payload.find("\"peers\"");
    if (peers_key == std::string_view::npos) {
        return std::nullopt;
    }
    const std::size_t peers_start = payload.find('[', peers_key);
    const std::size_t peers_end = payload.find(']', peers_start == std::string_view::npos ? peers_start : peers_start + 1);
    if (peers_start == std::string_view::npos || peers_end == std::string_view::npos || peers_end <= peers_start) {
        return std::nullopt;
    }

    const std::string peers_payload(payload.substr(peers_start + 1, peers_end - peers_start - 1));
    auto iter = std::sregex_iterator(peers_payload.begin(), peers_payload.end(), object_re);
    const auto end = std::sregex_iterator{};
    for (; iter != end; ++iter) {
        const std::string object = iter->str();

        std::smatch peer_id_match;
        std::smatch address_match;
        std::smatch port_match;
        if (!std::regex_search(object, peer_id_match, peer_id_re) || !std::regex_search(object, address_match, address_re) ||
            !std::regex_search(object, port_match, port_re) || peer_id_match.size() < 2 || address_match.size() < 2 ||
            port_match.size() < 2) {
            continue;
        }

        const std::string port_raw = port_match[1].str();
        char* end_ptr = nullptr;
        const unsigned long port_num = std::strtoul(port_raw.c_str(), &end_ptr, 10);
        if (end_ptr == nullptr || *end_ptr != '\0' || port_num > 65535UL) {
            continue;
        }

        response.peers.push_back(BootstrapPeerEndpoint{
            .peer_id = peer_id_match[1].str(),
            .address = address_match[1].str(),
            .port = static_cast<std::uint16_t>(port_num),
        });
    }

    if (response.session_id.empty() || response.peers.empty()) {
        return std::nullopt;
    }
    return response;
}

std::optional<BootstrapResponse>
default_fetch(std::string_view signaling_url, std::string_view session_id, std::string_view self_peer_id) {
    if (signaling_url.empty() || session_id.empty() || self_peer_id.empty()) {
        return std::nullopt;
    }

    if (signaling_url.rfind("mock://", 0) == 0) {
        BootstrapResponse response{};
        response.session_id = std::string(session_id);
        response.peers.push_back(BootstrapPeerEndpoint{
            .peer_id = std::string(self_peer_id),
            .address = "127.0.0.1",
            .port = 30000,
        });
        return response;
    }

    if (!is_safe_token(session_id) || !is_safe_token(self_peer_id)) {
        return std::nullopt;
    }
    if (!is_safe_url(signaling_url)) {
        return std::nullopt;
    }

    const std::string request_json =
        "{\"session_id\":\"" + std::string(session_id) + "\",\"peer_id\":\"" + std::string(self_peer_id) + "\"}";
    std::string command = "curl --silent --show-error --fail --max-time 3 -X POST ";
    command += "'" + std::string(signaling_url) + "' ";
    command += "-H 'Accept: application/json' ";
    command += "-H 'Content-Type: application/json' ";
    command += "--data-binary '" + request_json + "'";

    std::array<char, 256> buffer{};
    std::string payload;
    FILE* pipe = popen(command.c_str(), "r");
    if (pipe == nullptr) {
        return std::nullopt;
    }

    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        payload += buffer.data();
    }
    const int code = pclose(pipe);
    if (code != 0) {
        return std::nullopt;
    }

    if (const auto parsed_json = parse_json_payload(payload); parsed_json.has_value()) {
        return parsed_json;
    }
    return parse_line_payload(payload);
}

}  // namespace

BootstrapClient::BootstrapClient(std::string signaling_url, FetchFn fetch)
    : signaling_url_(std::move(signaling_url)), fetch_(std::move(fetch)) {
    if (!fetch_) {
        fetch_ = default_fetch;
    }
}

std::optional<BootstrapResponse> BootstrapClient::establish_contact(
    std::string_view session_id,
    std::string_view self_peer_id
) {
    if (contacted_) {
        return std::nullopt;
    }

    auto response = fetch_(signaling_url_, session_id, self_peer_id);
    if (!response.has_value()) {
        return std::nullopt;
    }

    contacted_ = true;
    return response;
}

bool BootstrapClient::contacted() const {
    return contacted_;
}

}  // namespace breach_team::net
