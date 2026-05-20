#include "breach_team/net/enet_transport.hpp"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>

#ifdef BREACH_TEAM_HAS_ENET
#include <enet/enet.h>
#endif

namespace breach_team::net {

namespace {

#ifdef BREACH_TEAM_HAS_ENET
std::string endpoint_key_from_peer(const ENetPeer* peer) {
    if (peer == nullptr) {
        return {};
    }
    std::array<char, 64> host{};
    if (enet_address_get_host_ip(&peer->address, host.data(), host.size()) != 0) {
        return {};
    }
    return std::string(host.data()) + ":" + std::to_string(peer->address.port);
}

bool parse_peer_endpoint(std::string_view peer_id, std::string& host, std::uint16_t& port) {
    const std::size_t at = peer_id.rfind('@');
    const std::string_view endpoint = at == std::string_view::npos ? peer_id : peer_id.substr(at + 1);
    const std::size_t colon = endpoint.rfind(':');
    if (colon == std::string_view::npos || colon == 0 || colon + 1 >= endpoint.size()) {
        return false;
    }

    host = std::string(endpoint.substr(0, colon));
    const std::string port_raw(endpoint.substr(colon + 1));
    char* end = nullptr;
    const unsigned long parsed = std::strtoul(port_raw.c_str(), &end, 10);
    if (end == nullptr || *end != '\0' || parsed > 65535UL) {
        return false;
    }
    port = static_cast<std::uint16_t>(parsed);
    return true;
}
#endif

}  // namespace

EnetTransport::~EnetTransport() {
    shutdown();
}

bool EnetTransport::start(std::string self_peer_id) {
    shutdown();
    self_peer_id_ = std::move(self_peer_id);
    started_ = !self_peer_id_.empty();
#ifdef BREACH_TEAM_HAS_ENET
    if (!started_) {
        return false;
    }

    if (enet_initialize() != 0) {
        std::cerr << "ENet init failed\n";
        started_ = false;
        return false;
    }

    ENetAddress bind_address{};
    ENetAddress* bind_ptr = nullptr;
    std::string endpoint_host;
    std::uint16_t endpoint_port = 0;
    if (parse_peer_endpoint(self_peer_id_, endpoint_host, endpoint_port)) {
        bind_address.host = ENET_HOST_ANY;
        bind_address.port = endpoint_port;
        bind_ptr = &bind_address;
    }

    ENetHost* const host = enet_host_create(bind_ptr, 32, 2, 0, 0);
    if (host == nullptr) {
        std::cerr << "ENet host bind failed";
        if (bind_ptr != nullptr) {
            std::cerr << " on port " << bind_address.port;
        }
        std::cerr << "\n";
        enet_deinitialize();
        started_ = false;
        return false;
    }
    host_ = host;
    return started_;
#else
    started_ = false;
    return false;
#endif
}

bool EnetTransport::send(std::string_view peer_id, std::span<const std::uint8_t> payload) {
    if (!started_ || peer_id.empty()) {
        return false;
    }

    TransportPacket packet{};
    packet.peer_id = std::string(peer_id);
    packet.payload.assign(payload.begin(), payload.end());
    outbound_.push_back(std::move(packet));

#ifdef BREACH_TEAM_HAS_ENET
    if (host_ == nullptr) {
        return false;
    }

    ENetHost* const host = static_cast<ENetHost*>(host_);
    ENetPeer* peer = nullptr;
    const auto found = peers_by_id_.find(std::string(peer_id));
    if (found != peers_by_id_.end()) {
        peer = static_cast<ENetPeer*>(found->second);
    } else {
        std::string endpoint_host;
        std::uint16_t endpoint_port = 0;
        if (parse_peer_endpoint(peer_id, endpoint_host, endpoint_port)) {
            ENetAddress address{};
            address.port = endpoint_port;
            if (enet_address_set_host(&address, endpoint_host.c_str()) == 0) {
                peer = enet_host_connect(host, &address, 2, 0);
                if (peer != nullptr) {
                    peers_by_id_[std::string(peer_id)] = peer;
                    ids_by_peer_[peer] = std::string(peer_id);
                    ENetEvent event{};
                    while (enet_host_service(host, &event, 10) > 0) {
                        if (event.type == ENET_EVENT_TYPE_CONNECT) {
                            peers_by_id_[std::string(peer_id)] = event.peer;
                            ids_by_peer_[event.peer] = std::string(peer_id);
                            peer = event.peer;
                            break;
                        }
                        if (event.type == ENET_EVENT_TYPE_DISCONNECT && event.peer == peer) {
                            peer = nullptr;
                            break;
                        }
                    }
                }
            }
        }
    }

    if (peer == nullptr) {
        return false;
    }

    ENetPacket* const enet_packet =
        enet_packet_create(payload.empty() ? nullptr : payload.data(), payload.size(), ENET_PACKET_FLAG_RELIABLE);
    if (enet_packet == nullptr) {
        return false;
    }
    if (enet_peer_send(peer, 0, enet_packet) != 0) {
        enet_packet_destroy(enet_packet);
        return false;
    }
    enet_host_flush(host);
#endif
    return true;
}

std::vector<TransportPacket> EnetTransport::poll() {
    std::vector<TransportPacket> packets;
    packets.reserve(inbound_.size());

    while (!inbound_.empty()) {
        packets.push_back(std::move(inbound_.front()));
        inbound_.pop_front();
    }

#ifdef BREACH_TEAM_HAS_ENET
    if (host_ != nullptr) {
        ENetHost* const host = static_cast<ENetHost*>(host_);
        ENetEvent event{};
        while (enet_host_service(host, &event, 0) > 0) {
            if (event.type == ENET_EVENT_TYPE_CONNECT) {
                const std::string id = endpoint_key_from_peer(event.peer);
                if (!id.empty()) {
                    peers_by_id_[id] = event.peer;
                    ids_by_peer_[event.peer] = id;
                }
                continue;
            }

            if (event.type == ENET_EVENT_TYPE_RECEIVE) {
                TransportPacket packet{};
                const auto id_it = ids_by_peer_.find(event.peer);
                packet.peer_id = id_it == ids_by_peer_.end() ? endpoint_key_from_peer(event.peer) : id_it->second;
                packet.payload.assign(event.packet->data, event.packet->data + event.packet->dataLength);
                packets.push_back(std::move(packet));
                enet_packet_destroy(event.packet);
                continue;
            }

            if (event.type == ENET_EVENT_TYPE_DISCONNECT) {
                const auto id_it = ids_by_peer_.find(event.peer);
                if (id_it != ids_by_peer_.end()) {
                    peers_by_id_.erase(id_it->second);
                    ids_by_peer_.erase(id_it);
                }
            }
        }
    }
#endif

    return packets;
}

void EnetTransport::inject_inbound(TransportPacket packet) {
    inbound_.push_back(std::move(packet));
}

std::size_t EnetTransport::queued_outbound() const {
    return outbound_.size();
}

void EnetTransport::clear_outbound() {
    outbound_.clear();
}

void EnetTransport::shutdown() {
#ifdef BREACH_TEAM_HAS_ENET
    if (host_ != nullptr) {
        ENetHost* const host = static_cast<ENetHost*>(host_);
        enet_host_destroy(host);
        host_ = nullptr;
        peers_by_id_.clear();
        ids_by_peer_.clear();
        enet_deinitialize();
    }
#endif
    inbound_.clear();
    outbound_.clear();
    started_ = false;
}

}  // namespace breach_team::net
