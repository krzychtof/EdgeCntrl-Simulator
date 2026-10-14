#include "edgecontrol/srv6.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace edgecontrol {
namespace {

const Router& findRouter(const Network& network, const std::string& router_id) {
    const auto& routers = network.routers();
    const auto router = std::find_if(routers.begin(), routers.end(),
                                     [&router_id](const Router& item) {
                                         return item.id == router_id;
                                     });
    if (router == routers.end()) {
        throw std::invalid_argument("Unknown SRv6 router: " + router_id);
    }
    return *router;
}

const Link* findActiveLink(const Network& network, const std::string& first,
                           const std::string& second) {
    const auto& links = network.links();
    const auto link = std::find_if(links.begin(), links.end(),
                                   [&first, &second](const Link& item) {
                                       return item.active &&
                                              ((item.first_router == first &&
                                                item.second_router == second) ||
                                               (item.first_router == second &&
                                                item.second_router == first));
                                   });
    return link == links.end() ? nullptr : &*link;
}

std::vector<std::string> shortestPath(const Network& network, const std::string& source,
                                      const std::string& destination) {
    const auto& routers = network.routers();
    const auto source_it = std::find_if(routers.begin(), routers.end(),
                                        [&source](const Router& router) {
                                            return router.id == source;
                                        });
    const auto destination_it =
        std::find_if(routers.begin(), routers.end(), [&destination](const Router& router) {
            return router.id == destination;
        });
    const std::size_t count = routers.size();
    const std::size_t source_index =
        static_cast<std::size_t>(std::distance(routers.begin(), source_it));
    const std::size_t destination_index =
        static_cast<std::size_t>(std::distance(routers.begin(), destination_it));
    if (source_index == count || destination_index == count) {
        return {};
    }
    if (source_index == destination_index) {
        return {source};
    }

    const double infinity = std::numeric_limits<double>::infinity();
    std::vector<double> distances(count, infinity);
    std::vector<std::size_t> previous(count, count);
    std::vector<bool> visited(count, false);
    distances[source_index] = 0.0;

    for (std::size_t step = 0; step < count; ++step) {
        std::size_t current = count;
        for (std::size_t index = 0; index < count; ++index) {
            if (visited[index]) {
                continue;
            }
            if (current == count || distances[index] < distances[current] ||
                (distances[index] == distances[current] &&
                 routers[index].id < routers[current].id)) {
                current = index;
            }
        }
        if (current == count || !std::isfinite(distances[current])) {
            break;
        }
        if (current == destination_index) {
            break;
        }
        visited[current] = true;

        for (const auto& link : network.links()) {
            if (!link.active) {
                continue;
            }
            std::string neighbor_id;
            if (link.first_router == routers[current].id) {
                neighbor_id = link.second_router;
            } else if (link.second_router == routers[current].id) {
                neighbor_id = link.first_router;
            } else {
                continue;
            }

            const auto neighbor = std::find_if(routers.begin(), routers.end(),
                                               [&neighbor_id](const Router& router) {
                                                   return router.id == neighbor_id;
                                               });
            if (neighbor == routers.end()) {
                continue;
            }
            const std::size_t neighbor_index =
                static_cast<std::size_t>(std::distance(routers.begin(), neighbor));
            const double candidate = distances[current] + link.latency_ms;
            if (candidate < distances[neighbor_index] ||
                (candidate == distances[neighbor_index] &&
                 (previous[neighbor_index] == count ||
                  routers[current].id < routers[previous[neighbor_index]].id))) {
                distances[neighbor_index] = candidate;
                previous[neighbor_index] = current;
            }
        }
    }

    if (!std::isfinite(distances[destination_index])) {
        return {};
    }
    std::vector<std::string> path;
    for (std::size_t current = destination_index; current != count;
         current = previous[current]) {
        path.push_back(routers[current].id);
        if (current == source_index) {
            break;
        }
    }
    if (path.empty() || path.back() != source) {
        return {};
    }
    std::reverse(path.begin(), path.end());
    return path;
}

} // namespace

Srv6ForwardingResult simulateSrv6Forwarding(const Network& network,
                                            const std::string& source_router,
                                            const std::string& client_id,
                                            Ipv4Address destination,
                                            const std::vector<Srv6Segment>& segments) {
    static_cast<void>(findRouter(network, source_router));

    Srv6ForwardingResult result;
    result.client_id = client_id;
    result.destination = destination.toString();

    const auto& subnets = network.vpcSubnets();
    const auto subnet = std::find_if(subnets.begin(), subnets.end(),
                                     [&client_id, destination](const VpcSubnet& candidate) {
                                         return candidate.client_id == client_id &&
                                                candidate.prefix.contains(destination);
                                     });
    if (subnet == subnets.end()) {
        result.reason = "No matching destination subnet exists for client " + client_id + ".";
        return result;
    }
    if (segments.empty()) {
        throw std::invalid_argument("An SRv6 policy must contain at least one SID.");
    }

    std::unordered_set<std::string> seen_sids;
    for (std::size_t index = 0; index < segments.size(); ++index) {
        const auto& segment = segments[index];
        static_cast<void>(findRouter(network, segment.router_id));
        if (!seen_sids.insert(segment.sid.toString()).second) {
            throw std::invalid_argument("An SRv6 policy cannot repeat a SID.");
        }
        const bool final_segment = index + 1U == segments.size();
        if (final_segment != (segment.behavior == Srv6Behavior::EndDx4)) {
            throw std::invalid_argument(
                "Intermediate SIDs must use End; the final IPv4 SID must use End.DX4.");
        }
    }
    if (segments.back().router_id != subnet->gateway_router) {
        throw std::invalid_argument("The final End.DX4 SID must be on the destination gateway.");
    }

    result.traversed_routers.push_back(source_router);
    std::string current_router = source_router;
    for (const auto& segment : segments) {
        const auto path = shortestPath(network, current_router, segment.router_id);
        if (path.empty()) {
            result.reason = "No active path to SRv6 SID " + segment.sid.toString() + ".";
            return result;
        }
        for (std::size_t index = 1; index < path.size(); ++index) {
            const Link* link = findActiveLink(network, path[index - 1U], path[index]);
            if (link == nullptr) {
                result.reason = "A link on the path to the SRv6 SID is unavailable.";
                return result;
            }
            result.path_latency_ms += link->latency_ms;
            result.traversed_routers.push_back(path[index]);
        }
        current_router = segment.router_id;
        result.executed_segments.push_back(segment.sid);
    }

    result.delivered = true;
    result.reason = "The final End.DX4 SID delivered the inner IPv4 packet to the client gateway.";
    return result;
}

} // namespace edgecontrol
