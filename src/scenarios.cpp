#include "edgecontrol/scenarios.hpp"

#include "edgecontrol/network.hpp"
#include "edgecontrol/metrics_server.hpp"
#include "edgecontrol/srv6.hpp"

#include <iomanip>
#include <ostream>
#include <stdexcept>
#include <string>

namespace edgecontrol {
namespace {

Network makeSampleNetwork() {
    Network network;
    network.addRouter("edge-west", 64512U);
    network.addRouter("core-north", 64513U);
    network.addRouter("core-south", 64514U);
    network.addRouter("edge-east", 64515U);

    network.addLink("edge-west", "core-north", 2.0);
    network.addLink("core-north", "edge-east", 2.0);
    network.addLink("edge-west", "core-south", 5.0);
    network.addLink("core-south", "edge-east", 1.0);

    network.addVpcSubnet("tenant-acme", 5001U, Ipv4Prefix::parse("10.10.0.0/24"),
                         "edge-west");
    network.addVpcSubnet("tenant-acme", 5001U, Ipv4Prefix::parse("10.20.0.0/24"),
                         "edge-east");
    network.addVpcSubnet("tenant-other", 6001U, Ipv4Prefix::parse("10.20.0.0/24"),
                         "edge-east");
    return network;
}

void printResult(std::ostream& output, const ForwardingResult& result) {
    output << (result.delivered ? "[DELIVERED] " : "[DROPPED]   ")
           << result.tenant_id << " -> " << result.destination << " | VNI " << result.vni
           << " | path: ";
    if (result.traversed_routers.empty()) {
        output << "(none)";
    } else {
        for (std::size_t index = 0; index < result.traversed_routers.size(); ++index) {
            if (index > 0U) {
                output << " -> ";
            }
            output << result.traversed_routers[index];
        }
    }
    output << " | " << std::fixed << std::setprecision(1) << result.path_latency_ms
           << " ms | " << result.reason << '\n';
}

void printMetrics(std::ostream& output, const Network& network) {
    output << "\nPrometheus exposition format:\n" << network.prometheusMetrics();
}

void printSrv6Result(std::ostream& output, const Srv6ForwardingResult& result) {
    output << (result.delivered ? "[DELIVERED] " : "[DROPPED]   ")
           << result.tenant_id << " -> " << result.destination << " | path: ";
    if (result.traversed_routers.empty()) {
        output << "(none)";
    } else {
        for (std::size_t index = 0; index < result.traversed_routers.size(); ++index) {
            if (index > 0U) {
                output << " -> ";
            }
            output << result.traversed_routers[index];
        }
    }
    output << " | SIDs: ";
    if (result.executed_segments.empty()) {
        output << "(none)";
    } else {
        for (std::size_t index = 0; index < result.executed_segments.size(); ++index) {
            if (index > 0U) {
                output << " -> ";
            }
            output << result.executed_segments[index].toString();
        }
    }
    output << " | " << std::fixed << std::setprecision(1) << result.path_latency_ms
           << " ms | " << result.reason << '\n';
}

} // namespace

void runFailoverScenario(std::ostream& output) {
    output << "=== Scenario: BGP-like route selection and link failover ===\n"
           << "Topology: edge-west has two paths to edge-east.\n";
    Network network = makeSampleNetwork();

    const auto before =
        network.forward("edge-west", "tenant-acme", Ipv4Address::parse("10.20.0.42"));
    output << "Before failure:\n";
    printResult(output, before);

    output << "\nInjecting failure: edge-west -- core-north goes DOWN.\n";
    network.setLinkState("edge-west", "core-north", false);
    const auto after =
        network.forward("edge-west", "tenant-acme", Ipv4Address::parse("10.20.0.42"));
    output << "After routing convergence:\n";
    printResult(output, after);
    printMetrics(output, network);
}

void runIsolationScenario(std::ostream& output) {
    output << "=== Scenario: VPC/tenant isolation and VXLAN VNI ===\n";
    Network network = makeSampleNetwork();

    const auto permitted =
        network.forward("edge-west", "tenant-acme", Ipv4Address::parse("10.20.0.42"));
    output << "Packet in tenant-acme VRF:\n";
    printResult(output, permitted);

    const auto isolated =
        network.forward("edge-west", "tenant-unknown", Ipv4Address::parse("10.20.0.42"));
    output << "Same destination, unknown tenant (must not leak across VPCs):\n";
    printResult(output, isolated);
    printMetrics(output, network);
}

void runSrv6Scenario(std::ostream& output) {
    output << "=== Scenario: SRv6 segment list and path change ===\n"
           << "This is an IPv6 SID forwarding model, not a Linux SRv6 dataplane.\n";
    Network network = makeSampleNetwork();

    const auto north_policy = std::vector<Srv6Segment>{
        {Ipv6Address::parse("2001:db8:1::1"), "core-north", Srv6Behavior::End},
        {Ipv6Address::parse("2001:db8:1::4"), "edge-east", Srv6Behavior::EndDx4}};
    output << "Policy: 2001:db8:1::1 (End, core-north) -> "
              "2001:db8:1::4 (End.DX4, edge-east)\n";
    const auto first = simulateSrv6Forwarding(
        network, "edge-west", "tenant-acme", Ipv4Address::parse("10.20.0.42"), north_policy);
    output << "Before failure:\n";
    printSrv6Result(output, first);

    output << "\nInjecting failure: core-north loses both of its links.\n";
    network.setLinkState("edge-west", "core-north", false);
    network.setLinkState("core-north", "edge-east", false);
    const auto broken = simulateSrv6Forwarding(
        network, "edge-west", "tenant-acme", Ipv4Address::parse("10.20.0.42"), north_policy);
    output << "The old segment list cannot reach its north SID:\n";
    printSrv6Result(output, broken);

    const auto south_policy = std::vector<Srv6Segment>{
        {Ipv6Address::parse("2001:db8:1::3"), "core-south", Srv6Behavior::End},
        {Ipv6Address::parse("2001:db8:1::4"), "edge-east", Srv6Behavior::EndDx4}};
    output << "\nUpdating the segment list to use core-south:\n";
    const auto backup = simulateSrv6Forwarding(
        network, "edge-west", "tenant-acme", Ipv4Address::parse("10.20.0.42"), south_policy);
    printSrv6Result(output, backup);
}

void runAllScenarios(std::ostream& output) {
    runFailoverScenario(output);
    output << '\n';
    runIsolationScenario(output);
    output << '\n';
    runSrv6Scenario(output);
}

void runMetricsServer(std::uint16_t port, std::ostream& output) {
    Network network = makeSampleNetwork();
    static_cast<void>(
        network.forward("edge-west", "tenant-acme", Ipv4Address::parse("10.20.0.42")));
    network.setLinkState("edge-west", "core-north", false);
    static_cast<void>(
        network.forward("edge-west", "tenant-acme", Ipv4Address::parse("10.20.0.42")));
    static_cast<void>(
        network.forward("edge-west", "tenant-unknown", Ipv4Address::parse("10.20.0.42")));

    output << "Prometheus metrics endpoint listening on http://127.0.0.1:" << port
           << "/metrics\nPress Ctrl+C to stop.\n";
    output.flush();
    servePrometheusMetrics(network, port);
}

void printHelp(std::ostream& output) {
    output << "EdgeControl Simulator - deterministic cloud routing/overlay demo\n\n"
           << "Usage:\n"
           << "  edgecontrol --scenario all\n"
           << "  edgecontrol --scenario failover\n"
           << "  edgecontrol --scenario isolation\n"
           << "  edgecontrol --scenario srv6\n"
           << "  edgecontrol --scenario serve [--metrics-port 9464]\n"
           << "  edgecontrol --help\n\n"
           << "Scenarios model BGP-like routing, VPC tenant isolation, VXLAN VNIs, an SRv6\n"
           << "segment-list policy, link failures, forwarding and Prometheus metrics.\n";
}

bool isKnownScenario(std::string_view name) noexcept {
    return name == "all" || name == "failover" || name == "isolation" ||
           name == "srv6" || name == "serve";
}

} // namespace edgecontrol
