#pragma once

#include "edgecontrol/ipv4.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace edgecontrol {

struct Router {
    std::string id;
    std::uint32_t autonomous_system;
};

struct Link {
    std::string first_router;
    std::string second_router;
    double latency_ms;
    bool active{true};
};

struct VpcSubnet {
    std::string client_id;
    std::uint32_t vni;
    Ipv4Prefix prefix;
    std::string gateway_router;
};

struct Route {
    std::string client_id;
    Ipv4Prefix prefix;
    std::string origin_router;
    std::string next_hop;
    std::vector<std::uint32_t> as_path;
    std::uint32_t local_preference{100};
    double total_latency_ms{};
    std::uint32_t vni{};
};

struct ForwardingResult {
    bool delivered{};
    std::string reason;
    std::string client_id;
    std::string destination;
    std::uint32_t vni{};
    double path_latency_ms{};
    std::vector<std::string> traversed_routers;
};

struct Metrics {
    std::uint64_t packets_forwarded{};
    std::uint64_t packets_dropped{};
    std::uint64_t route_changes{};
    std::uint64_t route_recomputations{};
};

class Network {
public:
    void addRouter(std::string id, std::uint32_t autonomous_system);
    void addLink(std::string first_router, std::string second_router, double latency_ms);
    void addVpcSubnet(std::string client_id, std::uint32_t vni, Ipv4Prefix prefix,
                      std::string gateway_router);
    void setLinkState(const std::string& first_router, const std::string& second_router,
                      bool active);

    [[nodiscard]] const std::vector<Router>& routers() const noexcept;
    [[nodiscard]] const std::vector<Link>& links() const noexcept;
    [[nodiscard]] const std::vector<VpcSubnet>& vpcSubnets() const noexcept;
    [[nodiscard]] const Metrics& metrics() const noexcept;
    [[nodiscard]] const Route* routeFor(const std::string& router_id,
                                        const std::string& client_id,
                                        const Ipv4Prefix& prefix) const;
    [[nodiscard]] ForwardingResult forward(const std::string& source_router,
                                           const std::string& client_id,
                                           Ipv4Address destination);
    [[nodiscard]] std::string prometheusMetrics() const;

private:
    using RouteTable = std::unordered_map<std::string, Route>;
    using ClientRouteTables = std::unordered_map<std::string, RouteTable>;

    [[nodiscard]] const Router& findRouter(const std::string& id) const;
    [[nodiscard]] const VpcSubnet* findSubnet(const std::string& client_id,
                                               Ipv4Address address) const noexcept;
    [[nodiscard]] const Link* findLink(const std::string& first_router,
                                       const std::string& second_router) const noexcept;
    void recomputeRoutes();

    std::vector<Router> routers_;
    std::vector<Link> links_;
    std::vector<VpcSubnet> vpc_subnets_;
    ClientRouteTables route_tables_;
    Metrics metrics_;
};

} // namespace edgecontrol
