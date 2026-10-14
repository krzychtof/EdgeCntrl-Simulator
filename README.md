# EdgeCntrl Simulator (Educational Prototype)

**⚠️ DISCLAIMER**: This is a **non-commercial educational prototype** created for learning purposes only. It is a free learning tool developed as a fun experiment with AI assistance. Not intended for production use.

EdgeCntrl Simulator is a modern C++ networking project that demonstrates how cloud networks can route traffic, recover from link failures, and keep customer networks separate.

The project has two parts. One is a small model that is easy to run repeatedly. The other is a hands-on lab that uses real Linux networking tools.

### 1. C++ simulator: understand the decisions

The simulator creates a small example network inside the program. It does not need real routers or send traffic over a network. It shows which path a packet would take and what changes when a link fails.

- **Routes and backup paths:** the simulator first chooses a preferred path. If that path fails, it finds another one.
- **Separate customer networks:** it keeps each customer's traffic in its own network, instead of mixing routes between customers.
- **VXLAN identifier (VNI):** the VNI is a number that identifies which virtual customer network a packet belongs to.
- **SRv6 example:** the simulator checks a planned path made of IPv6 addresses. This is an example model; it does not configure real SRv6 in Linux.
- **Counters and tests:** it prints network counters that Prometheus can read. GoogleTest runs 16 checks covering address handling, routing, customer separation, failure recovery, SRv6, metrics, and complete examples.

This part was created so routing behavior can be demonstrated and checked quickly, without first setting up a network lab.

```text
                 C++ simulator (one program)
                              |
                              v
                  Create example network
                              |
             +----------------+----------------+
             |                |                |
             v                v                v
        Choose route     Keep customer     Check planned
        to destination   networks apart    IPv6/SRv6 path
             |
             v
      Preferred path fails?
          /          \
        no            yes
        |              |
        v              v
    Keep route     Choose backup route
          \            /
           +----------+
                |
        Show result and counters
                |
        +-------+-------+
        |               |
        v               v
   Read by           Checked by
   Prometheus        GoogleTest
```

### 2. Linux lab: try it with real networking tools

The lab creates six Linux containers: **four routers** and **two client computers**. The router software, FRRouting (FRR), uses eBGP to share routes. In plain terms, eBGP lets neighboring routers tell each other which network paths they can reach. These routes form the **underlay**: the basic network that carries traffic between routers.

Linux bridges and a VXLAN tunnel connect the two client computers. VXLAN is like a virtual cable across the underlay; its number, VNI `5001`, identifies the customer network carried by that tunnel.

```text
                         4 ROUTERS
                  (routes shared with eBGP)

CLIENT 1       preferred route                 CLIENT 2
   |                                                   |
   +-- edge-west -- core-north -- edge-east -----------+
          \                         /
           +------ core-south ------+
                  backup route

      Two client computers + four router containers
          = six Linux containers in the lab
```

The client traffic uses the VXLAN overlay, while the routers carry that tunnel across the underlay:

```text
CLIENT 1 -- Linux bridge -- VXLAN endpoint       VXLAN endpoint -- Linux bridge -- CLIENT 2
                                  \                 /
                                   \--- VNI 5001 ---/
                                         |
                          The tunnel crosses the router network
                                         |
                 edge-west -- core-north -- edge-east
                     \                      /
                      +---- core-south -----+
```

The lab checks that the clients can communicate, turns off the preferred router link, and checks that traffic still reaches the other client by the backup route. It then restores the link and checks that the preferred route returns. This part was created to practice with real Linux networking, FRR, eBGP, and VXLAN—not just a model in a program.

The two parts do not depend on each other:

```text
C++ simulator                         Linux lab
example network in memory             real Linux containers
          |                                     |
          v                                     v
modeled routing decisions             FRR + eBGP + VXLAN
          |                                     |
          v                                     v
GoogleTest checks the model            real client traffic is tested
```

The simulator does not configure routers. The lab does not use the simulator.

## Project goals

```text
ROUTING       Pick a main path; switch to backup after failure
CLIENTS       Keep each client's network separate
VXLAN         Carry a client network across the router network
SRv6          Show a planned IPv6 path (simulator model)
MONITORING    Simulator ---> Prometheus ---> Grafana
QUALITY       13 feature tests + 3 full-scenario tests
REAL LAB      4 FRR routers + 2 clients; test real traffic
```

This is a learning project, not production networking software. The simulator uses simplified models; it is not a full BGP, VXLAN, or SRv6 implementation. Its SRv6 example does not install routes or forward SRv6 packets in Linux.

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
        client-scoped forwarding, metrics            End / End.DX4 model
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
 client-west-host -- edge-west --+-- core-north --+-- edge-east -- client-east-host
                                 |                 |
                                 +-- core-south ---+
                                   FRR/eBGP underlay
          Linux bridges + VXLAN VNI 5001 connect the client hosts
```

### C++ modules

| Module | Responsibility |
|---|---|
| `src/main.cpp` | Reads the command-line option and starts the requested demonstration. |
| `src/scenarios.cpp` | Sets up example networks and prints each demonstration's results. |
| `src/network.cpp` | Models routers, links, routes, client traffic, and network counters. |
| `src/ipv4.cpp` | Checks IPv4 addresses and networks. |
| `src/ipv6.cpp` | Reads and formats IPv6 addresses used by the SRv6 model. |
| `src/srv6.cpp` | Checks an SRv6 path and works out whether its segments are reachable. |
| `src/metrics_server.cpp` | Makes simulator counters available at `GET /metrics`. |
| `tests/gtest_tests.cpp` | Tests individual features and the complete demonstrations with GoogleTest. |

### Repository layout

```text
.
├── .github/workflows/ci.yml     # Windows and Ubuntu build/test workflow
├── include/edgecontrol/         # Public C++ declarations
├── src/                         # Modern C++ implementation
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

- Docker Desktop with Linux containers, or CMake 3.21+, a modern C++ compiler, and Git.
- Internet access for the first build so CMake can download GoogleTest v1.16.0.

### Real network lab

- Ubuntu on WSL2 or a Linux host.
- Docker Engine available to that Linux environment.
- Containerlab installed in that same environment.
- Permission to create network containers and Linux network interfaces.

Docker Desktop on Windows and Docker Engine inside Ubuntu WSL are separate. Run Docker Compose commands from PowerShell. Run the network lab from Ubuntu/WSL. Its topology is named `edgecntrl-frr` so it stays separate from the original project's lab.

## Build and test with Docker

From Windows PowerShell, at the repository root:

```powershell
docker build --target build -t edgecntrl-simulator-build:local .
docker run --rm --entrypoint cmake edgecntrl-simulator-build:local --build build --clean-first --parallel 2
docker run --rm --entrypoint ctest edgecntrl-simulator-build:local --test-dir build --output-on-failure
```

The Docker build stage configures and compiles the project, then runs its tests. The `--clean-first` command repeats the compilation from a clean state.

### Build and test locally

On Windows, install CMake and MSYS2 MinGW UCRT64 (`g++` and `mingw32-make`). Add `C:\Program Files\CMake\bin` and `C:\msys64\ucrt64\bin` to `PATH`, then reopen PowerShell. Use the provided preset to select MinGW:

```powershell
cmake --preset windows-mingw
cmake --build --preset windows-mingw --parallel
ctest --preset windows-mingw
```

Alternatively, run `run-tests.bat` to build and test, or `build.bat` to also run all demonstrations.

On Linux or WSL, with CMake, a modern C++ compiler, and Git on `PATH`:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

## Run the C++ scenarios

Using the Docker build image:

```powershell
docker run --rm --entrypoint ./build/edgecontrol edgecntrl-simulator-build:local --scenario all
```

Supported scenarios:

```text
all        Run failover, client isolation, and SRv6 model demonstrations.
failover   Forward before and after a simulated primary-link failure.
isolation  Compare a registered client with an unknown client.
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
[DELIVERED] client-a -> 10.20.0.42 | VNI 5001 | path: edge-west -> core-north -> edge-east | 4.0 ms | Delivered to the destination VPC subnet.

Injecting failure: edge-west -- core-north goes DOWN.
After routing convergence:
[DELIVERED] client-a -> 10.20.0.42 | VNI 5001 | path: edge-west -> core-south -> edge-east | 6.0 ms | Delivered to the destination VPC subnet.

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

=== Scenario: VPC/client isolation and VXLAN VNI ===
Packet in client-a VRF:
[DELIVERED] client-a -> 10.20.0.42 | VNI 5001 | path: edge-west -> core-north -> edge-east | 4.0 ms | Delivered to the destination VPC subnet.
Same destination, unknown client (must not leak across VPCs):
[DROPPED]   unknown-client -> 10.20.0.42 | VNI 0 | path: (none) | 0.0 ms | No matching destination subnet exists in client unknown-client.

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
[DELIVERED] client-a -> 10.20.0.42 | path: edge-west -> core-north -> edge-east | SIDs: 2001:db8:1::1 -> 2001:db8:1::4 | 4.0 ms | The final End.DX4 SID delivered the inner IPv4 packet to the client gateway.

Injecting failure: core-north loses both of its links.
The old segment list cannot reach its north SID:
[DROPPED]   client-a -> 10.20.0.42 | path: edge-west | SIDs: (none) | 0.0 ms | No active path to SRv6 SID 2001:db8:1::1.

Updating the segment list to use core-south:
[DELIVERED] client-a -> 10.20.0.42 | path: edge-west -> core-south -> edge-east | SIDs: 2001:db8:1::3 -> 2001:db8:1::4 | 6.0 ms | The final End.DX4 SID delivered the inner IPv4 packet to the client gateway.
```

### How to read this output

The output walks through three small network experiments. Think of the routers as intersections and the paths as roads: the simulator decides which road a packet should take, then reports whether it reached its destination.

```text
                         --scenario all
                              |
                              v
                  +-----------------------+
                  | Start a fresh network |
                  +-----------------------+
                     |         |         |
          +----------+         |         +-----------------+
          v                    v                           v
   ROUTE FAILOVER       CLIENT ISOLATION                 SRv6
   edge-west            Is client known?         Follow the configured
       |                  /          \               segment list
       v                yes          no                    |
   core-north            |            |                    v
       |             VNI 5001      DROP: no          Can the next SID
       v             and route     client route?       be reached?
   edge-east              |            |              /         \
       |                DELIVER       DROP           yes          no
       X link fails                                    |           |
       |                                               v           v
       v                                          End / End.DX4   DROP
   Recompute routes                                       |
       |                                                  v
       v                                              DELIVER IPv4
   core-south
       |
       v
   edge-east
       |
       v
    DELIVER
       |
       +-------------------+--------------------+
                           v
             Print this scenario's counters
       forwarded / dropped / route changes /
       route recalculations / active links
```

1. **Route selection and failover:** `edge-west` initially reaches `edge-east` through `core-north`. When the link between `edge-west` and `core-north` is marked `DOWN`, the simulator recalculates routes and sends the next packet through `core-south`. Both packets are delivered. The backup route has a modeled cost of `6.0 ms`, compared with `4.0 ms` for the original route; these are example values, not measured timings.
2. **Client isolation:** The packet belonging to the configured client `client-a` is delivered with VNI `5001`. A packet for the same destination address but an unknown client is dropped: there is no matching subnet in that client's routing context, so the simulator does not borrow another client's route. `VNI 0` and `path: (none)` show that no client overlay or forwarding path was selected for that packet.
3. **SRv6 segment list:** The first segment (`2001:db8:1::1`) identifies the `core-north` step, and the final segment (`2001:db8:1::4`) identifies `edge-east`, where `End.DX4` delivers the inner IPv4 packet toward the client gateway. After `core-north` loses its links, the old segment list cannot reach its first SID, so the packet is dropped. Replacing that SID with `2001:db8:1::3` directs the modeled path through `core-south`, and delivery succeeds again. This demonstrates updating a modeled SRv6 policy; it does not install a real SRv6 route in Linux.

The Prometheus lines are a snapshot of counters for the scenario that printed them. `packets_forwarded` and `packets_dropped` count that scenario's delivered and rejected packets. `route_changes` counts route entries the model added, changed, or withdrew, while `route_recomputations` counts its full routing recalculations; they are internal model counters, not counts of packets or real BGP messages. `active_links` reports how many simulated links are currently up. Each scenario starts with a fresh in-memory network, so its counters start over rather than accumulating across all three demonstrations.

The latency values are configured model values, not measurements from a physical or virtual network. Each scenario creates a fresh in-memory network, so its counters are independent.

## GoogleTest suite

The test suite has 16 tests: 13 check individual features and 3 check the complete demonstrations. All use GoogleTest. GoogleMock is not needed by this project.

### Captured CTest output

```text
Internal ctest changing into directory: C:/Users/kkowa/OneDrive/Dokumenty/Projekty/C++/GitHub/EdgeCntrl-Simulator/build/windows-mingw
Test project C:/Users/kkowa/OneDrive/Dokumenty/Projekty/C++/GitHub/EdgeCntrl-Simulator/build/windows-mingw
      Start  1: Ipv4AddressTest.ParsesAndFormatsAddress
 1/16 Test  #1: Ipv4AddressTest.ParsesAndFormatsAddress ...............................   Passed    0.02 sec
      Start  2: Ipv4PrefixTest.NormalizesNetworkAndChecksContainment
 2/16 Test  #2: Ipv4PrefixTest.NormalizesNetworkAndChecksContainment ..................   Passed    0.02 sec
      Start  3: Ipv6AddressTest.CompressesEquivalentAddresses
 3/16 Test  #3: Ipv6AddressTest.CompressesEquivalentAddresses .........................   Passed    0.03 sec
      Start  4: Ipv6AddressTest.RejectsMalformedAddress
 4/16 Test  #4: Ipv6AddressTest.RejectsMalformedAddress ...............................   Passed    0.03 sec
      Start  5: NetworkTest.RejectsInvalidTopologyAndClientConfiguration
 5/16 Test  #5: NetworkTest.RejectsInvalidTopologyAndClientConfiguration ..............   Passed    0.03 sec
      Start  6: NetworkTest.UsesBackupRouteWhenPrimaryLinkFails
 6/16 Test  #6: NetworkTest.UsesBackupRouteWhenPrimaryLinkFails ......................   Passed    0.03 sec
      Start  7: NetworkTest.WithdrawsRouteWhenAllPathsAreUnavailable
 7/16 Test  #7: NetworkTest.WithdrawsRouteWhenAllPathsAreUnavailable ..................   Passed    0.03 sec
      Start  8: NetworkTest.KeepsClientNetworksIsolated
 8/16 Test  #8: NetworkTest.KeepsClientNetworksIsolated ...............................   Passed    0.03 sec
      Start  9: MetricsServerTest.RejectsPortZero
 9/16 Test  #9: MetricsServerTest.RejectsPortZero .....................................   Passed    0.03 sec
      Start 10: Srv6Test.ForwardsThroughConfiguredSegments
10/16 Test #10: Srv6Test.ForwardsThroughConfiguredSegments ............................   Passed    0.02 sec
      Start 11: Srv6Test.RejectsPolicyWhenItsSegmentRouterIsUnreachable
11/16 Test #11: Srv6Test.RejectsPolicyWhenItsSegmentRouterIsUnreachable ...............   Passed    0.02 sec
      Start 12: Srv6Test.ChangesSegmentListAfterFailureAndKeepsClientIsolation
12/16 Test #12: Srv6Test.ChangesSegmentListAfterFailureAndKeepsClientIsolation ........   Passed    0.03 sec
      Start 13: Srv6Test.RejectsInvalidFinalBehaviorAndGateway
13/16 Test #13: Srv6Test.RejectsInvalidFinalBehaviorAndGateway ........................   Passed    0.03 sec
      Start 14: ScenarioIntegrationTest.FailoverScenarioReportsBothPathsAndMetrics
14/16 Test #14: ScenarioIntegrationTest.FailoverScenarioReportsBothPathsAndMetrics ....   Passed    0.03 sec
      Start 15: ScenarioIntegrationTest.IsolationScenarioReportsDropAndDistinctVnis
15/16 Test #15: ScenarioIntegrationTest.IsolationScenarioReportsDropAndDistinctVnis ...   Passed    0.03 sec
      Start 16: ScenarioIntegrationTest.Srv6ScenarioReportsSegmentsAndPathChange
16/16 Test #16: ScenarioIntegrationTest.Srv6ScenarioReportsSegmentsAndPathChange ......   Passed    0.03 sec

100% tests passed, 0 tests failed out of 16

Total Test time (real) =   0.50 sec
```

The tests check:

- IPv4 and IPv6 addresses and networks.
- Network setup, client separation, route selection, and recovery after a link failure.
- The metrics server's port settings.
- SRv6 paths, including what happens when a segment cannot be reached.
- The output from all three demonstrations.

The test runtime varies by machine; the expected invariant is `100% tests passed, 0 tests failed`.

The test run is simple:

```text
Configure project  --->  Compile C++  --->  Run 16 tests  --->  All pass?
       CMake               MinGW / C++         GoogleTest        yes = ready
```

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

The data flow is:

```text
C++ simulator
     |
     | counters every 5 seconds
     v
/metrics ---> Prometheus ---> Grafana dashboard
              stores data      shows charts
```

The monitoring stack reads the **simulator**, not the separate FRR lab.

### Example response from `/metrics`

Open <http://localhost:9464/metrics> to see the simulator's current counters in Prometheus text format. Here is a captured example:

```text
# HELP edgecontrol_packets_forwarded Packets delivered by the simulator.
# TYPE edgecontrol_packets_forwarded counter
edgecontrol_packets_forwarded 2
# HELP edgecontrol_packets_dropped Packets dropped by the simulator.
# TYPE edgecontrol_packets_dropped counter
edgecontrol_packets_dropped 1
# HELP edgecontrol_route_changes Route entries added, changed, or withdrawn.
# TYPE edgecontrol_route_changes counter
edgecontrol_route_changes 16
# HELP edgecontrol_route_recomputations Full routing convergence calculations.
# TYPE edgecontrol_route_recomputations counter
edgecontrol_route_recomputations 12
# HELP edgecontrol_active_links Number of active simulated links.
# TYPE edgecontrol_active_links gauge
edgecontrol_active_links 3
```

Read the values like this:

```text
Packets delivered / dropped       = how many reached a destination / did not
Route changes / recalculations   = how often the model updated its routes
Active links                     = how many simulated links are up right now
```

The first four metrics in the example are totals; active links is a current value. These numbers describe the simulator, not the FRR lab, and can change between runs.

Prometheus reads the simulator's counters every five seconds. Grafana displays them using the dashboard included with the project. The services are available only from the local computer (`127.0.0.1`).

Stop the Compose stack:

```powershell
docker compose down
```

Prometheus and Grafana keep their saved data after `down`. Use `docker compose down -v` only if you also want to delete that data.

## Run the FRR/BGP/VXLAN lab

Run these commands in Ubuntu/WSL, not PowerShell. Ubuntu needs Docker Engine and Containerlab. The lab creates its own isolated network using Linux containers with network privileges.

```bash
cd EdgeCntrl-Simulator
sudo bash lab/containerlab/scripts/run-lab.sh
```

The lab runs this sequence:

```text
Start 6 containers
4 routers + 2 clients
       |
       v
Routers share routes (eBGP / underlay)
       |
       v
Connect client networks (VXLAN / overlay)
       |
       v
Check client-to-client traffic
       |
       v
Turn off preferred link
       |
       v
Check backup route still carries traffic
       |
       v
Restore link and check preferred route
```

This test uses real Linux networking, unlike the C++ model.

Expected final line:

```text
Real FRR/BGP/VXLAN failover and recovery passed. Lab containers remain running.
```

The script leaves the lab running so you can inspect it. Stop this project's lab with:

```bash
sudo bash lab/containerlab/scripts/stop-lab.sh
```

The lab uses the separate name `edgecntrl-frr`; it will not stop or replace the original project's `edgecontrol-frr` lab.

## Design notes and limitations

- The simulator simplifies how BGP shares routes and chooses paths; it is not a full BGP implementation.
- VXLAN in the simulator represents how clients are associated with VNIs. The separate Linux lab creates real VXLAN interfaces.
- SRv6 is demonstrated in the simulator only; the project does not configure SRv6 forwarding in Linux.
- Prometheus and Grafana monitor the simulator, not the FRR lab.
- The metrics endpoint is a small example server, not intended for production.

## Continuous integration

When code is pushed or a pull request is opened, GitHub Actions checks the project on Windows and Ubuntu:

```text
Push / pull request
        |
        +----> Windows: build ---> tests
        |
        +----> Ubuntu:  build ---> tests
                                   |
                                   v
                              pass or fail
```

CI does not start the separate network lab.

## Verification record

The output above comes from a Windows MinGW build using the project's preset. All 16 tests passed. The demonstrations produce repeatable results; test runtimes vary by computer.

## License

[](https://github.com/krzychtof/5G-UE-Simulator#-license)
**Free Educational Prototype** - Non-commercial use only.

This project is:

- ✅ Free to use for learning
- ✅ Free to modify for educational purposes
- ✅ Free to share with attribution
- ❌ Not for commercial use
- ❌ Not production-ready
- ❌ No warranty or support provided
**Created with AI assistance** as a learning experiment.

**🎓 Educational Prototype Project**

**🆓 Free & Non-Commercial**

**🤖 Created with AI Assistance**

**📚 For Learning Purposes Only**
