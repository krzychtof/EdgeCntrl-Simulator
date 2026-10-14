# EdgeCntrl Simulator

EdgeCntrl Simulator is a C++20 networking project for exploring cloud-routing concepts through two complementary environments:

1. a deterministic, unit-tested C++ model of route selection, tenant isolation, link failover, VXLAN identifiers, SRv6 segment policies, and Prometheus metrics;
2. an isolated Linux lab using Containerlab, FRRouting (FRR), eBGP, Linux bridges, and a VXLAN overlay between tenant hosts.

The C++ model and the Linux lab are intentionally separate. The simulator does not configure FRR or forward real packets. The lab does not call the simulator. This separation makes it possible to inspect routing decisions quickly in C++, then observe related behavior in a real Linux dataplane.

## Project goals

- Demonstrate deterministic, BGP-like path selection and convergence after link-state changes.
- Model tenant-scoped IPv4 prefixes and VXLAN Network Identifiers (VNIs), rejecting traffic without a matching tenant subnet.
- Model an SRv6 segment list using IPv6 Segment Identifiers (SIDs), `End`, and final `End.DX4` behavior.
- Exercise real eBGP route failover and failback between four FRR routers and verify tenant-host connectivity across VXLAN.
- Expose simulator counters in Prometheus text format and provide a Prometheus/Grafana dashboard.
- Keep behavior reproducible with GoogleTest unit and scenario-integration tests.

This is an educational simulator and lab, not a production router or a full implementation of BGP, VXLAN control plane, or SRv6. The SRv6 component is a C++ model only; it does not create Linux SRv6 routes or an SRH dataplane.

## Architecture and data flow

```text
                           C++ simulation
 CLI arguments
      |
      v
 src/main.cpp ---- selects a scenario ----> src/scenarios.cpp
                                                |
                       +------------------------+------------------+
                       |                                           |
                       v                                           v
                src/network.cpp                              src/srv6.cpp
        routers, links, route tables,               shortest active paths to SIDs,
        tenant-scoped forwarding, metrics            End / End.DX4 model
                       |                                           |
                       +-------------------+-----------------------+
                                           |
                  +------------------------+--------------------+
                  |                                             |
                  v                                             v
           CLI scenario output                         /metrics HTTP endpoint
                                                               |
                                                               v
                                                   Prometheus -> Grafana

                         Independent Linux lab
 tenant-west-host -- edge-west --+-- core-north --+-- edge-east -- tenant-east-host
                                 |                 |
                                 +-- core-south ---+
                                   FRR/eBGP underlay
          Linux bridges + VXLAN VNI 5001 connect the tenant hosts
```

### C++ modules

| Module | Responsibility |
|---|---|
| `src/main.cpp` | Validates CLI options and dispatches to a scenario. |
| `src/scenarios.cpp` | Builds sample topologies, runs demonstrations, and formats results. |
| `src/network.cpp` | Owns routers, links, tenant subnets, simplified route convergence, forwarding, and metrics. |
| `src/ipv4.cpp` | Parses IPv4 addresses and prefixes and provides containment/overlap operations. |
| `src/ipv6.cpp` | Parses and formats IPv6 addresses used as model SIDs. |
| `src/srv6.cpp` | Validates an SRv6 policy and computes paths through active links to its SIDs. |
| `src/metrics_server.cpp` | Serves `GET /metrics` over TCP/HTTP for Prometheus scraping. |
| `tests/gtest_tests.cpp` | Contains unit tests and end-to-end scenario output tests using GoogleTest. |

### Repository layout

```text
.
├── .github/workflows/ci.yml     # Windows and Ubuntu build/test workflow
├── include/edgecontrol/         # Public C++ declarations
├── src/                         # C++20 implementation
├── tests/gtest_tests.cpp        # GoogleTest unit and integration tests
├── lab/containerlab/            # FRR topology, router configs, run/stop scripts
├── observability/               # Prometheus and Grafana provisioning
├── CMakeLists.txt               # Targets and GoogleTest configuration
├── CMakePresets.json            # Windows MinGW and Linux presets
├── Dockerfile                   # Multi-stage build; tests run in build stage
└── compose.yaml                 # Simulator, Prometheus, and Grafana services
```

## Requirements

### C++ build and metrics stack

- Docker Desktop with the Linux container engine, or CMake 3.21+, a C++20 compiler, and Git.
- Internet access on the first build: CMake `FetchContent` downloads GoogleTest v1.16.0.

### Real network lab

- Ubuntu on WSL2 or a Linux host.
- Docker Engine available to that Linux environment.
- Containerlab installed in that same environment.
- Permissions to create privileged network containers and Linux interfaces.

Docker Desktop and a Docker Engine installed inside Ubuntu WSL are separate engines. Run Compose commands from Windows PowerShell to use Docker Desktop; run Containerlab commands from Ubuntu to use the Linux Engine. The Containerlab topology name is `edgecntrl-frr`, separate from the source project's original lab name.

## Build and test with Docker

From Windows PowerShell, at the repository root:

```powershell
docker build --target build -t edgecntrl-simulator-build:local .
docker run --rm --entrypoint cmake edgecntrl-simulator-build:local --build build --clean-first --parallel 2
docker run --rm --entrypoint ctest edgecntrl-simulator-build:local --test-dir build --output-on-failure
```

The Docker build stage installs CMake, g++, and Git, configures the project, builds it, and runs CTest. The explicit `--clean-first` build above verifies compilation without relying on existing CMake object files.

### Build and test locally

With CMake, a C++20 compiler, and Git on `PATH`:

```powershell
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

On Windows, `run-tests.bat` uses the `windows-mingw` preset and expects CMake plus MSYS2 MinGW UCRT64 (`g++` and `mingw32-make`) on `PATH`. `build.bat` additionally runs the `all` scenario after tests pass.

## Run the C++ scenarios

Using the Docker build image:

```powershell
docker run --rm --entrypoint ./build/edgecontrol edgecntrl-simulator-build:local --scenario all
```

Supported scenarios:

```text
all        Run failover, tenant isolation, and SRv6 model demonstrations.
failover   Forward before and after a simulated primary-link failure.
isolation  Compare a registered tenant with an unknown tenant.
srv6       Execute a segment list, inject a failure, and use a new list.
serve      Prepare sample counters and serve the Prometheus endpoint.
```

Alternatively, build and run the standalone runtime image:

```powershell
docker build -t edgecntrl-simulator:local .
docker run --rm edgecntrl-simulator:local --scenario all
```

## Recorded simulator output

The following is the captured output of `--scenario all` from the verified build:

```text
=== Scenario: BGP-like route selection and link failover ===
Topology: edge-west has two paths to edge-east.
Before failure:
[DELIVERED] tenant-acme -> 10.20.0.42 | VNI 5001 | path: edge-west -> core-north -> edge-east | 4.0 ms | Delivered to the destination VPC subnet.

Injecting failure: edge-west -- core-north goes DOWN.
After routing convergence:
[DELIVERED] tenant-acme -> 10.20.0.42 | VNI 5001 | path: edge-west -> core-south -> edge-east | 6.0 ms | Delivered to the destination VPC subnet.

Prometheus exposition format:
# HELP edgecontrol_packets_forwarded Packets delivered by the simulator.
# TYPE edgecontrol_packets_forwarded counter
edgecontrol_packets_forwarded 2
# HELP edgecontrol_packets_dropped Packets dropped by the simulator.
# TYPE edgecontrol_packets_dropped counter
edgecontrol_packets_dropped 0
# HELP edgecontrol_route_changes Route entries added, changed, or withdrawn.
# TYPE edgecontrol_route_changes counter
edgecontrol_route_changes 16
# HELP edgecontrol_route_recomputations Full routing convergence calculations.
# TYPE edgecontrol_route_recomputations counter
edgecontrol_route_recomputations 12
# HELP edgecontrol_active_links Number of active simulated links.
# TYPE edgecontrol_active_links gauge
edgecontrol_active_links 3

=== Scenario: VPC/tenant isolation and VXLAN VNI ===
Packet in tenant-acme VRF:
[DELIVERED] tenant-acme -> 10.20.0.42 | VNI 5001 | path: edge-west -> core-north -> edge-east | 4.0 ms | Delivered to the destination VPC subnet.
Same destination, unknown tenant (must not leak across VPCs):
[DROPPED]   tenant-unknown -> 10.20.0.42 | VNI 0 | path: (none) | 0.0 ms | No matching destination subnet exists in tenant tenant-unknown.

Prometheus exposition format:
# HELP edgecontrol_packets_forwarded Packets delivered by the simulator.
# TYPE edgecontrol_packets_forwarded counter
edgecontrol_packets_forwarded 1
# HELP edgecontrol_packets_dropped Packets dropped by the simulator.
# TYPE edgecontrol_packets_dropped counter
edgecontrol_packets_dropped 1
# HELP edgecontrol_route_changes Route entries added, changed, or withdrawn.
# TYPE edgecontrol_route_changes counter
edgecontrol_route_changes 12
# HELP edgecontrol_route_recomputations Full routing convergence calculations.
# TYPE edgecontrol_route_recomputations counter
edgecontrol_route_recomputations 11
# HELP edgecontrol_active_links Number of active simulated links.
# TYPE edgecontrol_active_links gauge
edgecontrol_active_links 4

=== Scenario: SRv6 segment list and path change ===
This is an IPv6 SID forwarding model, not a Linux SRv6 dataplane.
Policy: 2001:db8:1::1 (End, core-north) -> 2001:db8:1::4 (End.DX4, edge-east)
Before failure:
[DELIVERED] tenant-acme -> 10.20.0.42 | path: edge-west -> core-north -> edge-east | SIDs: 2001:db8:1::1 -> 2001:db8:1::4 | 4.0 ms | The final End.DX4 SID delivered the inner IPv4 packet to the tenant gateway.

Injecting failure: core-north loses both of its links.
The old segment list cannot reach its north SID:
[DROPPED]   tenant-acme -> 10.20.0.42 | path: edge-west | SIDs: (none) | 0.0 ms | No active path to SRv6 SID 2001:db8:1::1.

Updating the segment list to use core-south:
[DELIVERED] tenant-acme -> 10.20.0.42 | path: edge-west -> core-south -> edge-east | SIDs: 2001:db8:1::3 -> 2001:db8:1::4 | 6.0 ms | The final End.DX4 SID delivered the inner IPv4 packet to the tenant gateway.
```

The latency values are configured model values, not measurements from a physical or virtual network. Each scenario creates a fresh in-memory network, so its counters are independent.

## GoogleTest suite

The suite currently contains 16 discovered tests: 13 focused unit tests and 3 scenario integration tests. GoogleMock is disabled because the tested code does not require mocks.

### Captured CTest output

```text
Internal ctest changing into directory: /src/build
Test project /src/build
      Start  1: Ipv4AddressTest.ParsesAndFormatsAddress
 1/16 Test  #1: Ipv4AddressTest.ParsesAndFormatsAddress ...............................   Passed    0.00 sec
      Start  2: Ipv4PrefixTest.NormalizesNetworkAndChecksContainment
 2/16 Test  #2: Ipv4PrefixTest.NormalizesNetworkAndChecksContainment ..................   Passed    0.00 sec
      Start  3: Ipv6AddressTest.CompressesEquivalentAddresses
 3/16 Test  #3: Ipv6AddressTest.CompressesEquivalentAddresses .........................   Passed    0.00 sec
      Start  4: Ipv6AddressTest.RejectsMalformedAddress
 4/16 Test  #4: Ipv6AddressTest.RejectsMalformedAddress ...............................   Passed    0.01 sec
      Start  5: NetworkTest.RejectsInvalidTopologyAndTenantConfiguration
 5/16 Test  #5: NetworkTest.RejectsInvalidTopologyAndTenantConfiguration ..............   Passed    0.01 sec
      Start  6: NetworkTest.UsesBackupRouteWhenPrimaryLinkFails
 6/16 Test  #6: NetworkTest.UsesBackupRouteWhenPrimaryLinkFails ......................   Passed    0.01 sec
      Start  7: NetworkTest.WithdrawsRouteWhenAllPathsAreUnavailable
 7/16 Test  #7: NetworkTest.WithdrawsRouteWhenAllPathsAreUnavailable ..................   Passed    0.01 sec
      Start  8: NetworkTest.KeepsTenantNetworksIsolated
 8/16 Test  #8: NetworkTest.KeepsTenantNetworksIsolated ...............................   Passed    0.01 sec
      Start  9: MetricsServerTest.RejectsPortZero
 9/16 Test  #9: MetricsServerTest.RejectsPortZero .....................................   Passed    0.01 sec
      Start 10: Srv6Test.ForwardsThroughConfiguredSegments
10/16 Test #10: Srv6Test.ForwardsThroughConfiguredSegments ............................   Passed    0.01 sec
      Start 11: Srv6Test.RejectsPolicyWhenItsSegmentRouterIsUnreachable
11/16 Test #11: Srv6Test.RejectsPolicyWhenItsSegmentRouterIsUnreachable ...............   Passed    0.01 sec
      Start 12: Srv6Test.ChangesSegmentListAfterFailureAndKeepsTenantIsolation
12/16 Test #12: Srv6Test.ChangesSegmentListAfterFailureAndKeepsTenantIsolation ........   Passed    0.01 sec
      Start 13: Srv6Test.RejectsInvalidFinalBehaviorAndGateway
13/16 Test #13: Srv6Test.RejectsInvalidFinalBehaviorAndGateway ........................   Passed    0.00 sec
      Start 14: ScenarioIntegrationTest.FailoverScenarioReportsBothPathsAndMetrics
14/16 Test #14: ScenarioIntegrationTest.FailoverScenarioReportsBothPathsAndMetrics ....   Passed    0.00 sec
      Start 15: ScenarioIntegrationTest.IsolationScenarioReportsDropAndDistinctVnis
15/16 Test #15: ScenarioIntegrationTest.IsolationScenarioReportsDropAndDistinctVnis ...   Passed    0.01 sec
      Start 16: ScenarioIntegrationTest.Srv6ScenarioReportsSegmentsAndPathChange
16/16 Test #16: ScenarioIntegrationTest.Srv6ScenarioReportsSegmentsAndPathChange ......   Passed    0.00 sec

100% tests passed, 0 tests failed out of 16

Total Test time (real) =   0.21 sec
```

Test coverage by behavior:

- IPv4 and IPv6 parsing, formatting, subnet containment, and prefix overlap.
- Topology validation, tenant/VNI constraints, route selection, link failover, route withdrawal, and tenant isolation.
- Metrics-server port validation.
- SRv6 path execution, unreachable SIDs, policy validation, failover, and tenant isolation.
- Integration-level output for failover, isolation, and SRv6 scenarios.

The test runtime varies by machine; the expected invariant is `100% tests passed, 0 tests failed`.

## Run observability

From Windows PowerShell:

```powershell
docker compose up --build -d
docker compose ps
```

Endpoints:

- Simulator metrics: <http://localhost:9464/metrics>
- Prometheus: <http://localhost:9090>
- Prometheus targets: <http://localhost:9090/targets>
- Grafana: <http://localhost:3000>

Prometheus scrapes the simulator at `simulator:9464/metrics` every five seconds. Grafana uses the provisioned Prometheus datasource and dashboard. Services bind to loopback (`127.0.0.1`) rather than all host interfaces.

Stop the Compose stack:

```powershell
docker compose down
```

Named Prometheus and Grafana volumes persist across `down`. Use `docker compose down -v` only when intentionally removing that stored data.

## Run the FRR/BGP/VXLAN lab

Run this section in Ubuntu/WSL, not Windows PowerShell. The Ubuntu environment must have its own Docker Engine and Containerlab configured. The lab uses privileged Linux containers and changes only its isolated topology.

```bash
cd "/mnt/c/Users/kkowa/OneDrive/Dokumenty/Projekty/C++/EdgeCntrl-Simulator"
sudo bash lab/containerlab/scripts/run-lab.sh
```

The script deploys four FRR routers (`edge-west`, `core-north`, `core-south`, `edge-east`) and two Alpine tenant hosts. It waits for eBGP sessions, configures Linux bridges and VXLAN VNI 5001, checks the preferred north route, tests host-to-host ping, disables the primary underlay link, checks failover through the south router, restores the link, and verifies failback.

Expected final line:

```text
Real FRR/BGP/VXLAN failover and recovery passed. Lab containers remain running.
```

The script deliberately leaves the lab running for inspection. Stop only this project's topology with:

```bash
sudo bash lab/containerlab/scripts/stop-lab.sh
```

The topology uses the unique name `edgecntrl-frr`, so it does not reuse or remove the `edgecontrol-frr` topology from the source project.

## Design notes and limitations

- Route propagation and path selection in `network.cpp` are deterministic educational approximations, not full BGP protocol implementations.
- The C++ VXLAN-related behavior models tenant/VNI association. Real Linux VXLAN interfaces are configured only in the separate lab.
- SRv6 is a C++ forwarding model; no SRH packet header or Linux `seg6local` route is installed.
- Prometheus/Grafana observe the C++ simulator only; FRR exporter integration is not included.
- The metrics HTTP server is a minimal sequential demonstration endpoint, not a production HTTP server.

## Continuous integration

`.github/workflows/ci.yml` configures, builds, and runs CTest on Windows and Ubuntu for pushes and pull requests. The CI workflow verifies the C++ project; it does not deploy the privileged Containerlab topology.

## License

No `LICENSE` file is included. Choose and add a license before granting public reuse rights.

## Verification record

The README output above was captured after a clean CMake build of all C++ targets and a complete CTest run in the Docker build environment. All 16 tests passed. Scenario output is deterministic; per-test timings are machine-dependent.
