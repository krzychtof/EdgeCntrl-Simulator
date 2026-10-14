#include "edgecontrol/network.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace edgecontrol {
namespace {

std::string routeKey(const std::string& tenant_id, const Ipv4Prefix& prefix) {
    return tenant_id + "|" + prefix.toString();
}

bool isPreferred(const Route& candidate, const Route& current) {
    if (candidate.local_preference != current.local_preference) {
        return candidate.local_preference > current.local_preference;
    }
    if (candidate.as_path.size() != current.as_path.size()) {
        return candidate.as_path.size() < current.as_path.size();
    }
    if (candidate.total_latency_ms != current.total_latency_ms) {
        return candidate.total_latency_ms < current.total_latency_ms;
    }
    return candidate.next_hop < current.next_hop;
}

bool sameRoute(const Route& lhs, const Route& rhs) {
    return lhs.tenant_id == rhs.tenant_id && lhs.prefix == rhs.prefix &&
           lhs.origin_router == rhs.origin_router && lhs.next_hop == rhs.next_hop &&
           lhs.as_path == rhs.as_path && lhs.local_preference == rhs.local_preference &&
           lhs.total_latency_ms == rhs.total_latency_ms && lhs.vni == rhs.vni;
}

} // namespace

void Network::addRouter(std::string id, std::uint32_t autonomous_system) {
    if (id.empty() || autonomous_system == 0U) {
        throw std::invalid_argument("Router ID and ASN must be non-empty and non-zero.");
    }
    if (std::any_of(routers_.begin(), routers_.end(),
                    [&id](const Router& router) { return router.id == id; })) {
        throw std::invalid_argument("Router already exists: " + id);
    }
    if (std::any_of(routers_.begin(), routers_.end(), [autonomous_system](const Router& router) {
            return router.autonomous_system == autonomous_system;
        })) {
        throw std::invalid_argument("Each simulated router must have a unique ASN.");
    }

    routers_.push_back({std::move(id), autonomous_system});
    recomputeRoutes();
}

void Network::addLink(std::string first_router, std::string second_router, double latency_ms) {
    if (first_router == second_router) {
        throw std::invalid_argument("A link must connect two different routers.");
    }
    static_cast<void>(findRouter(first_router));
    static_cast<void>(findRouter(second_router));
    if (!std::isfinite(latency_ms) || latency_ms <= 0.0) {
        throw std::invalid_argument("Link latency must be a positive finite number.");
    }
    if (findLink(first_router, second_router) != nullptr) {
        throw std::invalid_argument("A link between these routers already exists.");
    }

    links_.push_back({std::move(first_router), std::move(second_router), latency_ms, true});
    recomputeRoutes();
}

void Network::addVpcSubnet(std::string tenant_id, std::uint32_t vni, Ipv4Prefix prefix,
                           std::string gateway_router) {
    if (tenant_id.empty() || vni == 0U || vni > 16777215U) {
        throw std::invalid_argument("Tenant ID must be set and VNI must be in range 1..16777215.");
    }
    static_cast<void>(findRouter(gateway_router));

    for (const auto& subnet : vpc_subnets_) {
        if (subnet.tenant_id != tenant_id) {
            continue;
        }
        if (subnet.vni != vni) {
            throw std::invalid_argument("All subnets belonging to one tenant must use one VNI.");
        }
        if (subnet.prefix.overlaps(prefix)) {
            throw std::invalid_argument("Overlapping subnets are not allowed within one tenant.");
        }
    }

    vpc_subnets_.push_back({std::move(tenant_id), vni, prefix, std::move(gateway_router)});
    recomputeRoutes();
}

void Network::setLinkState(const std::string& first_router, const std::string& second_router,
                           bool active) {
    for (auto& link : links_) {
        if ((link.first_router == first_router && link.second_router == second_router) ||
            (link.first_router == second_router && link.second_router == first_router)) {
            if (link.active != active) {
                link.active = active;
                recomputeRoutes();
            }
            return;
        }
    }
    throw std::invalid_argument("No link exists between " + first_router + " and " + second_router);
}

const std::vector<Router>& Network::routers() const noexcept {
    return routers_;
}

const std::vector<Link>& Network::links() const noexcept {
    return links_;
}

const std::vector<VpcSubnet>& Network::vpcSubnets() const noexcept {
    return vpc_subnets_;
}

const Metrics& Network::metrics() const noexcept {
    return metrics_;
}

const Route* Network::routeFor(const std::string& router_id, const std::string& tenant_id,
                               const Ipv4Prefix& prefix) const {
    const auto router = route_tables_.find(router_id);
    if (router == route_tables_.end()) {
        return nullptr;
    }
    const auto route = router->second.find(routeKey(tenant_id, prefix));
    return route == router->second.end() ? nullptr : &route->second;
}

ForwardingResult Network::forward(const std::string& source_router, const std::string& tenant_id,
                                  Ipv4Address destination) {
    static_cast<void>(findRouter(source_router));
    ForwardingResult result;
    result.tenant_id = tenant_id;
    result.destination = destination.toString();

    const VpcSubnet* subnet = findSubnet(tenant_id, destination);
    if (subnet == nullptr) {
        result.reason = "No matching destination subnet exists in tenant " + tenant_id + ".";
        ++metrics_.packets_dropped;
        return result;
    }
    result.vni = subnet->vni;

    std::string current_router = source_router;
    for (std::size_t hop = 0; hop <= routers_.size(); ++hop) {
        result.traversed_routers.push_back(current_router);
        if (current_router == subnet->gateway_router) {
            result.delivered = true;
            result.reason = "Delivered to the destination VPC subnet.";
            ++metrics_.packets_forwarded;
            return result;
        }

        const Route* route = routeFor(current_router, tenant_id, subnet->prefix);
        if (route == nullptr || route->next_hop.empty()) {
            result.reason = "No usable route to " + subnet->prefix.toString() + " from " +
                            current_router + ".";
            ++metrics_.packets_dropped;
            return result;
        }
        const Link* link = findLink(current_router, route->next_hop);
        if (link == nullptr || !link->active) {
            result.reason = "The selected next hop is unavailable.";
            ++metrics_.packets_dropped;
            return result;
        }

        result.path_latency_ms += link->latency_ms;
        current_router = route->next_hop;
    }

    result.reason = "Forwarding stopped because the hop limit was exceeded.";
    ++metrics_.packets_dropped;
    return result;
}

std::string Network::prometheusMetrics() const {
    std::ostringstream output;
    output << "# HELP edgecontrol_packets_forwarded Packets delivered by the simulator.\n"
           << "# TYPE edgecontrol_packets_forwarded counter\n"
           << "edgecontrol_packets_forwarded " << metrics_.packets_forwarded << '\n'
           << "# HELP edgecontrol_packets_dropped Packets dropped by the simulator.\n"
           << "# TYPE edgecontrol_packets_dropped counter\n"
           << "edgecontrol_packets_dropped " << metrics_.packets_dropped << '\n'
           << "# HELP edgecontrol_route_changes Route entries added, changed, or withdrawn.\n"
           << "# TYPE edgecontrol_route_changes counter\n"
           << "edgecontrol_route_changes " << metrics_.route_changes << '\n'
           << "# HELP edgecontrol_route_recomputations Full routing convergence calculations.\n"
           << "# TYPE edgecontrol_route_recomputations counter\n"
           << "edgecontrol_route_recomputations " << metrics_.route_recomputations << '\n'
           << "# HELP edgecontrol_active_links Number of active simulated links.\n"
           << "# TYPE edgecontrol_active_links gauge\n"
           << "edgecontrol_active_links "
           << std::count_if(links_.begin(), links_.end(),
                            [](const Link& link) { return link.active; })
           << '\n';
    return output.str();
}

const Router& Network::findRouter(const std::string& id) const {
    const auto router = std::find_if(routers_.begin(), routers_.end(),
                                     [&id](const Router& candidate) { return candidate.id == id; });
    if (router == routers_.end()) {
        throw std::invalid_argument("Unknown router: " + id);
    }
    return *router;
}

const VpcSubnet* Network::findSubnet(const std::string& tenant_id,
                                     Ipv4Address address) const noexcept {
    const auto subnet = std::find_if(vpc_subnets_.begin(), vpc_subnets_.end(),
                                     [&tenant_id, address](const VpcSubnet& candidate) {
                                         return candidate.tenant_id == tenant_id &&
                                                candidate.prefix.contains(address);
                                     });
    return subnet == vpc_subnets_.end() ? nullptr : &*subnet;
}

const Link* Network::findLink(const std::string& first_router,
                              const std::string& second_router) const noexcept {
    const auto link = std::find_if(links_.begin(), links_.end(),
                                   [&first_router, &second_router](const Link& candidate) {
                                       return (candidate.first_router == first_router &&
                                               candidate.second_router == second_router) ||
                                              (candidate.first_router == second_router &&
                                               candidate.second_router == first_router);
                                   });
    return link == links_.end() ? nullptr : &*link;
}

void Network::recomputeRoutes() {
    const auto previous_tables = route_tables_;
    TenantRouteTables converged;

    for (const auto& subnet : vpc_subnets_) {
        const Router& gateway = findRouter(subnet.gateway_router);
        Route local{subnet.tenant_id, subnet.prefix, subnet.gateway_router, "",
                    {gateway.autonomous_system}, 200U, 0.0, subnet.vni};
        converged[subnet.gateway_router].insert_or_assign(
            routeKey(subnet.tenant_id, subnet.prefix), std::move(local));
    }

    TenantRouteTables current = converged;
    for (std::size_t iteration = 0; iteration < routers_.size(); ++iteration) {
        TenantRouteTables next = converged;
        for (const auto& link : links_) {
            if (!link.active) {
                continue;
            }
            for (const auto& [from_router, to_router] :
                 {std::pair{link.first_router, link.second_router},
                  std::pair{link.second_router, link.first_router}}) {
                const Router& receiving_router = findRouter(to_router);
                const auto routes = current.find(from_router);
                if (routes == current.end()) {
                    continue;
                }
                for (const auto& [key, advertised_route] : routes->second) {
                    if (advertised_route.as_path.empty() ||
                        std::find(advertised_route.as_path.begin(), advertised_route.as_path.end(),
                                  receiving_router.autonomous_system) !=
                            advertised_route.as_path.end()) {
                        continue;
                    }

                    Route candidate = advertised_route;
                    candidate.next_hop = from_router;
                    candidate.as_path.insert(candidate.as_path.begin(),
                                             receiving_router.autonomous_system);
                    candidate.local_preference = 100U;
                    candidate.total_latency_ms += link.latency_ms;
                    RouteTable& receiver_table = next[to_router];
                    const auto existing = receiver_table.find(key);
                    if (existing == receiver_table.end() ||
                        isPreferred(candidate, existing->second)) {
                        receiver_table.insert_or_assign(key, std::move(candidate));
                    }
                }
            }
        }

        bool changed = false;
        for (const auto& router : routers_) {
            for (const auto& [key, route] : next[router.id]) {
                const auto old_router = current.find(router.id);
                if (old_router == current.end()) {
                    changed = true;
                    break;
                }
                const auto old_route = old_router->second.find(key);
                if (old_route == old_router->second.end() ||
                    !sameRoute(route, old_route->second)) {
                    changed = true;
                    break;
                }
            }
            if (changed) {
                break;
            }
        }
        current = std::move(next);
        if (!changed) {
            break;
        }
    }

    for (const auto& router : routers_) {
        const auto new_router = current.find(router.id);
        const auto old_router = previous_tables.find(router.id);
        if (new_router == current.end()) {
            continue;
        }
        for (const auto& [key, route] : new_router->second) {
            const auto old_route =
                old_router == previous_tables.end() ? RouteTable::const_iterator{}
                                                    : old_router->second.find(key);
            if (old_router == previous_tables.end() || old_route == old_router->second.end() ||
                !sameRoute(route, old_route->second)) {
                ++metrics_.route_changes;
            }
        }
        if (old_router != previous_tables.end()) {
            for (const auto& [key, route] : old_router->second) {
                static_cast<void>(route);
                if (new_router->second.find(key) == new_router->second.end()) {
                    ++metrics_.route_changes;
                }
            }
        }
    }

    route_tables_ = std::move(current);
    ++metrics_.route_recomputations;
}

} // namespace edgecontrol
