#include "edgecontrol/ipv4.hpp"
#include "edgecontrol/ipv6.hpp"
#include "edgecontrol/metrics_server.hpp"
#include "edgecontrol/network.hpp"
#include "edgecontrol/scenarios.hpp"
#include "edgecontrol/srv6.hpp"

#include <gtest/gtest.h>

#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using edgecontrol::Ipv4Address;
using edgecontrol::Ipv4Prefix;
using edgecontrol::Network;
using edgecontrol::Srv6Behavior;
using edgecontrol::Srv6Segment;

Network makeNetwork() {
    Network network;
    network.addRouter("west", 64512U);
    network.addRouter("middle-a", 64513U);
    network.addRouter("middle-b", 64514U);
    network.addRouter("east", 64515U);
    network.addLink("west", "middle-a", 2.0);
    network.addLink("middle-a", "east", 2.0);
    network.addLink("west", "middle-b", 5.0);
    network.addLink("middle-b", "east", 1.0);
    network.addVpcSubnet("client-a", 5001U, Ipv4Prefix::parse("10.1.0.0/24"), "west");
    network.addVpcSubnet("client-a", 5001U, Ipv4Prefix::parse("10.2.0.0/24"), "east");
    network.addVpcSubnet("client-b", 6001U, Ipv4Prefix::parse("10.2.0.0/24"), "east");
    return network;
}

TEST(Ipv4AddressTest, ParsesAndFormatsAddress) {
    EXPECT_EQ(Ipv4Address::parse("192.168.1.7").toString(), "192.168.1.7");
    EXPECT_THROW(static_cast<void>(Ipv4Address::parse("256.0.0.1")),
                 std::invalid_argument);
    EXPECT_THROW(static_cast<void>(Ipv4Address::parse("1.2.3")), std::invalid_argument);
}

TEST(Ipv4PrefixTest, NormalizesNetworkAndChecksContainment) {
    const auto prefix = Ipv4Prefix::parse("10.5.7.99/24");
    EXPECT_EQ(prefix.toString(), "10.5.7.0/24");
    EXPECT_TRUE(prefix.contains(Ipv4Address::parse("10.5.7.255")));
    EXPECT_FALSE(prefix.contains(Ipv4Address::parse("10.5.8.1")));
    EXPECT_TRUE(Ipv4Prefix::parse("10.0.0.0/8").overlaps(
        Ipv4Prefix::parse("10.1.0.0/16")));
    EXPECT_FALSE(Ipv4Prefix::parse("10.0.0.0/8").overlaps(
        Ipv4Prefix::parse("11.0.0.0/8")));
    EXPECT_THROW(static_cast<void>(Ipv4Prefix::parse("10.0.0.0/33")),
                 std::invalid_argument);
}

TEST(Ipv6AddressTest, CompressesEquivalentAddresses) {
    const auto long_form =
        edgecontrol::Ipv6Address::parse("2001:0db8:0001:0000:0000:0000:0000:0001");
    EXPECT_EQ(long_form.toString(), "2001:db8:1::1");
    EXPECT_EQ(long_form, edgecontrol::Ipv6Address::parse("2001:db8:1::1"));
    EXPECT_EQ(edgecontrol::Ipv6Address::parse("::").toString(), "::");
    EXPECT_EQ(edgecontrol::Ipv6Address::parse("2001:db8::").toString(), "2001:db8::");
}

TEST(Ipv6AddressTest, RejectsMalformedAddress) {
    EXPECT_THROW(static_cast<void>(edgecontrol::Ipv6Address::parse("2001:db8:::1")),
                 std::invalid_argument);
    EXPECT_THROW(static_cast<void>(edgecontrol::Ipv6Address::parse("2001:db8::1::2")),
                 std::invalid_argument);
    EXPECT_THROW(static_cast<void>(edgecontrol::Ipv6Address::parse("2001:db8:1")),
                 std::invalid_argument);
    EXPECT_THROW(static_cast<void>(edgecontrol::Ipv6Address::parse("gggg::1")),
                 std::invalid_argument);
    EXPECT_THROW(static_cast<void>(
                     edgecontrol::Ipv6Address::parse("1:2:3:4:5:6:7:8::")),
                 std::invalid_argument);
}

TEST(NetworkTest, RejectsInvalidTopologyAndClientConfiguration) {
    Network network;
    network.addRouter("r1", 64512U);
    network.addRouter("r2", 64513U);

    EXPECT_THROW(network.addRouter("r1", 64514U), std::invalid_argument);
    EXPECT_THROW(network.addRouter("r3", 64512U), std::invalid_argument);
    EXPECT_THROW(network.addLink("r1", "missing", 1.0), std::invalid_argument);
    EXPECT_THROW(network.addLink("r1", "r2", 0.0), std::invalid_argument);

    network.addLink("r1", "r2", 1.0);
    EXPECT_THROW(network.addLink("r2", "r1", 2.0), std::invalid_argument);

    network.addVpcSubnet("client-a", 5001U, Ipv4Prefix::parse("10.0.0.0/24"), "r1");
    EXPECT_THROW(network.addVpcSubnet(
                     "client-a", 5001U, Ipv4Prefix::parse("10.0.0.128/25"), "r2"),
                 std::invalid_argument);
    EXPECT_THROW(network.addVpcSubnet(
                     "client-a", 5002U, Ipv4Prefix::parse("10.0.1.0/24"), "r2"),
                 std::invalid_argument);
}

TEST(NetworkTest, UsesBackupRouteWhenPrimaryLinkFails) {
    auto network = makeNetwork();
    const auto destination = Ipv4Address::parse("10.2.0.42");

    const auto primary = network.forward("west", "client-a", destination);
    const auto* primary_route =
        network.routeFor("west", "client-a", Ipv4Prefix::parse("10.2.0.0/24"));
    ASSERT_NE(primary_route, nullptr);
    EXPECT_EQ(primary_route->next_hop, "middle-a");
    EXPECT_EQ(primary_route->as_path.size(), 3U);
    ASSERT_TRUE(primary.delivered);
    ASSERT_EQ(primary.traversed_routers.size(), 3U);
    EXPECT_EQ(primary.traversed_routers[1], "middle-a");
    EXPECT_DOUBLE_EQ(primary.path_latency_ms, 4.0);

    network.setLinkState("west", "middle-a", false);
    const auto backup = network.forward("west", "client-a", destination);
    ASSERT_TRUE(backup.delivered);
    ASSERT_EQ(backup.traversed_routers.size(), 3U);
    EXPECT_EQ(backup.traversed_routers[1], "middle-b");
    EXPECT_DOUBLE_EQ(backup.path_latency_ms, 6.0);

    const auto* backup_route =
        network.routeFor("west", "client-a", Ipv4Prefix::parse("10.2.0.0/24"));
    ASSERT_NE(backup_route, nullptr);
    EXPECT_EQ(backup_route->next_hop, "middle-b");
}

TEST(NetworkTest, WithdrawsRouteWhenAllPathsAreUnavailable) {
    auto network = makeNetwork();
    network.setLinkState("west", "middle-a", false);
    network.setLinkState("west", "middle-b", false);

    const auto result =
        network.forward("west", "client-a", Ipv4Address::parse("10.2.0.42"));

    EXPECT_FALSE(result.delivered);
    EXPECT_NE(result.reason.find("No usable route"), std::string::npos);
    EXPECT_EQ(network.routeFor("west", "client-a", Ipv4Prefix::parse("10.2.0.0/24")),
              nullptr);
}

TEST(NetworkTest, KeepsClientNetworksIsolated) {
    auto network = makeNetwork();
    const auto destination = Ipv4Address::parse("10.2.0.42");

    const auto client_a = network.forward("west", "client-a", destination);
    const auto client_b = network.forward("west", "client-b", destination);
    const auto unknown = network.forward("west", "unknown-client", destination);

    EXPECT_TRUE(client_a.delivered);
    EXPECT_EQ(client_a.vni, 5001U);
    EXPECT_TRUE(client_b.delivered);
    EXPECT_EQ(client_b.vni, 6001U);
    EXPECT_FALSE(unknown.delivered);
    EXPECT_EQ(network.metrics().packets_dropped, 1U);
    EXPECT_EQ(network.metrics().packets_forwarded, 2U);
    EXPECT_NE(network.prometheusMetrics().find("edgecontrol_packets_dropped 1"),
              std::string::npos);
}

TEST(MetricsServerTest, RejectsPortZero) {
    const Network network;
    EXPECT_THROW(edgecontrol::servePrometheusMetrics(network, 0U), std::invalid_argument);
}

TEST(Srv6Test, ForwardsThroughConfiguredSegments) {
    const auto network = makeNetwork();
    const std::vector<Srv6Segment> segments{
        {edgecontrol::Ipv6Address::parse("2001:db8:1::1"), "middle-a",
         Srv6Behavior::End},
        {edgecontrol::Ipv6Address::parse("2001:db8:1::4"), "east",
         Srv6Behavior::EndDx4}};

    const auto result = edgecontrol::simulateSrv6Forwarding(
        network, "west", "client-a", Ipv4Address::parse("10.2.0.42"), segments);

    ASSERT_TRUE(result.delivered) << result.reason;
    ASSERT_EQ(result.traversed_routers.size(), 3U);
    EXPECT_EQ(result.traversed_routers[1], "middle-a");
    EXPECT_EQ(result.executed_segments.size(), 2U);
    EXPECT_DOUBLE_EQ(result.path_latency_ms, 4.0);
}

TEST(Srv6Test, RejectsPolicyWhenItsSegmentRouterIsUnreachable) {
    auto network = makeNetwork();
    network.setLinkState("west", "middle-a", false);
    network.setLinkState("middle-a", "east", false);
    const std::vector<Srv6Segment> segments{
        {edgecontrol::Ipv6Address::parse("2001:db8:1::1"), "middle-a",
         Srv6Behavior::End},
        {edgecontrol::Ipv6Address::parse("2001:db8:1::4"), "east",
         Srv6Behavior::EndDx4}};

    const auto result = edgecontrol::simulateSrv6Forwarding(
        network, "west", "client-a", Ipv4Address::parse("10.2.0.42"), segments);

    EXPECT_FALSE(result.delivered);
    EXPECT_NE(result.reason.find("No active path"), std::string::npos);
}

TEST(Srv6Test, ChangesSegmentListAfterFailureAndKeepsClientIsolation) {
    auto network = makeNetwork();
    const std::vector<Srv6Segment> north_policy{
        {edgecontrol::Ipv6Address::parse("2001:db8:1::1"), "middle-a",
         Srv6Behavior::End},
        {edgecontrol::Ipv6Address::parse("2001:db8:1::4"), "east",
         Srv6Behavior::EndDx4}};
    const std::vector<Srv6Segment> south_policy{
        {edgecontrol::Ipv6Address::parse("2001:db8:1::3"), "middle-b",
         Srv6Behavior::End},
        {edgecontrol::Ipv6Address::parse("2001:db8:1::4"), "east",
         Srv6Behavior::EndDx4}};

    const auto first = edgecontrol::simulateSrv6Forwarding(
        network, "west", "client-a", Ipv4Address::parse("10.2.0.42"), north_policy);
    ASSERT_TRUE(first.delivered);
    ASSERT_EQ(first.executed_segments.size(), 2U);
    EXPECT_EQ(first.executed_segments.front().toString(), "2001:db8:1::1");

    network.setLinkState("west", "middle-a", false);
    network.setLinkState("middle-a", "east", false);
    const auto stale = edgecontrol::simulateSrv6Forwarding(
        network, "west", "client-a", Ipv4Address::parse("10.2.0.42"), north_policy);
    EXPECT_FALSE(stale.delivered);
    EXPECT_NE(stale.reason.find("No active path"), std::string::npos);

    const auto backup = edgecontrol::simulateSrv6Forwarding(
        network, "west", "client-a", Ipv4Address::parse("10.2.0.42"), south_policy);
    ASSERT_TRUE(backup.delivered);
    ASSERT_GE(backup.traversed_routers.size(), 2U);
    EXPECT_EQ(backup.traversed_routers[1], "middle-b");
    EXPECT_DOUBLE_EQ(backup.path_latency_ms, 6.0);

    const auto isolated = edgecontrol::simulateSrv6Forwarding(
        network, "west", "unknown-client", Ipv4Address::parse("10.2.0.42"), south_policy);
    EXPECT_FALSE(isolated.delivered);
    EXPECT_TRUE(isolated.executed_segments.empty());
}

TEST(Srv6Test, RejectsInvalidFinalBehaviorAndGateway) {
    const auto network = makeNetwork();
    const std::vector<Srv6Segment> missing_final_behavior{
        {edgecontrol::Ipv6Address::parse("2001:db8:1::4"), "east",
         Srv6Behavior::End}};
    const std::vector<Srv6Segment> wrong_gateway{
        {edgecontrol::Ipv6Address::parse("2001:db8:1::1"), "middle-a",
         Srv6Behavior::EndDx4}};

    EXPECT_THROW(edgecontrol::simulateSrv6Forwarding(
                     network, "west", "client-a", Ipv4Address::parse("10.2.0.42"),
                     missing_final_behavior),
                 std::invalid_argument);
    EXPECT_THROW(edgecontrol::simulateSrv6Forwarding(
                     network, "west", "client-a", Ipv4Address::parse("10.2.0.42"),
                     wrong_gateway),
                 std::invalid_argument);
}

TEST(ScenarioIntegrationTest, FailoverScenarioReportsBothPathsAndMetrics) {
    std::ostringstream output;
    edgecontrol::runFailoverScenario(output);
    const auto text = output.str();

    EXPECT_NE(text.find("edge-west -> core-north -> edge-east"), std::string::npos);
    EXPECT_NE(text.find("edge-west -> core-south -> edge-east"), std::string::npos);
    EXPECT_NE(text.find("edgecontrol_route_recomputations"), std::string::npos);
    EXPECT_NE(text.find("edgecontrol_packets_forwarded 2"), std::string::npos);
}

TEST(ScenarioIntegrationTest, IsolationScenarioReportsDropAndDistinctVnis) {
    std::ostringstream output;
    edgecontrol::runIsolationScenario(output);
    const auto text = output.str();

    EXPECT_NE(text.find("client-a -> 10.20.0.42 | VNI 5001"),
              std::string::npos);
    EXPECT_NE(text.find("unknown-client -> 10.20.0.42 | VNI 0"),
              std::string::npos);
    EXPECT_NE(text.find("[DROPPED]"), std::string::npos);
    EXPECT_NE(text.find("edgecontrol_packets_dropped 1"), std::string::npos);
}

TEST(ScenarioIntegrationTest, Srv6ScenarioReportsSegmentsAndPathChange) {
    std::ostringstream output;
    edgecontrol::runSrv6Scenario(output);
    const auto text = output.str();

    EXPECT_NE(text.find("2001:db8:1::1"), std::string::npos);
    EXPECT_NE(text.find("End.DX4"), std::string::npos);
    EXPECT_NE(text.find("No active path to SRv6 SID"), std::string::npos);
    EXPECT_NE(text.find("edge-west -> core-south -> edge-east"), std::string::npos);
    EXPECT_NE(text.find("6.0 ms"), std::string::npos);
}

} // namespace
