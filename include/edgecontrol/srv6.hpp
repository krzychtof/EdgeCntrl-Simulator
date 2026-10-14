#pragma once

#include "edgecontrol/ipv6.hpp"
#include "edgecontrol/network.hpp"

#include <string>
#include <vector>

namespace edgecontrol {

enum class Srv6Behavior {
    End,
    EndDx4
};

struct Srv6Segment {
    Ipv6Address sid;
    std::string router_id;
    Srv6Behavior behavior;
};

struct Srv6ForwardingResult {
    bool delivered{};
    std::string reason;
    std::string client_id;
    std::string destination;
    std::vector<std::string> traversed_routers;
    std::vector<Ipv6Address> executed_segments;
    double path_latency_ms{};
};

[[nodiscard]] Srv6ForwardingResult simulateSrv6Forwarding(
    const Network& network, const std::string& source_router, const std::string& client_id,
    Ipv4Address destination, const std::vector<Srv6Segment>& segments);

} // namespace edgecontrol
