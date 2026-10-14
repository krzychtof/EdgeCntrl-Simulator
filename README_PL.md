# EdgeCntrl Simulator

EdgeCntrl Simulator to projekt sieciowy w C++20, który pozwala poznawać zagadnienia routingu w chmurze w dwóch uzupełniających się środowiskach:

1. deterministycznym, testowanym jednostkowo modelu C++, obejmującym wybór tras, izolację tenantów (logicznie odseparowanych sieci klientów), przełączanie łączy, identyfikatory VXLAN, polityki segmentów SRv6 i metryki Prometheusa;
2. odizolowanym laboratorium Linux opartym na Containerlab, FRRouting (FRR), eBGP, mostach Linux i nakładkowej sieci VXLAN pomiędzy hostami tenantów.

Model C++ i laboratorium Linux są celowo rozdzielone. Symulator nie konfiguruje FRR ani nie przekazuje rzeczywistych pakietów. Laboratorium nie wywołuje symulatora. Dzięki temu można szybko analizować decyzje routingu w C++, a następnie obserwować pokrewne zachowanie w rzeczywistym dataplane Linux (warstwie przekazywania pakietów).

## Cele projektu

- Pokazanie deterministycznego wyboru ścieżek podobnego do BGP i zbieżności routingu po zmianie stanu łącza.
- Modelowanie prefiksów IPv4 i identyfikatorów VXLAN Network Identifier (VNI) przypisanych do tenantów; odrzucanie ruchu, jeśli nie pasuje do podsieci danego tenanta.
- Modelowanie listy segmentów SRv6 z użyciem IPv6 Segment Identifier (SID), zachowań `End` i końcowego `End.DX4`.
- Testowanie rzeczywistego przełączania tras eBGP i powrotu na trasę podstawową między czterema routerami FRR oraz weryfikacja łączności hostów tenantów przez VXLAN.
- Udostępnianie liczników symulatora w formacie tekstowym Prometheusa oraz dostarczanie pulpitu Prometheus/Grafana.
- Zapewnienie powtarzalności działania za pomocą testów jednostkowych i integracyjnych scenariuszy w GoogleTest.

To projekt edukacyjny — symulator i laboratorium, a nie router produkcyjny ani pełna implementacja protokołu BGP, płaszczyzny sterowania VXLAN czy SRv6. Komponent SRv6 jest wyłącznie modelem w C++; nie tworzy tras SRv6 w Linuxie ani dataplane SRH (nagłówka routingu segmentowego).

## Architektura i przepływ danych

```text
                           Symulacja C++
 Argumenty CLI
      |
      v
 src/main.cpp ---- wybór scenariusza ----> src/scenarios.cpp
                                                |
                       +------------------------+------------------+
                       |                                           |
                       v                                           v
                src/network.cpp                              src/srv6.cpp
        routery, łącza, tablice tras,                najkrótsze aktywne ścieżki do SID,
        przekazywanie tenantów, metryki               model zachowań End / End.DX4
                       |                                           |
                       +-------------------+-----------------------+
                                           |
                  +------------------------+--------------------+
                  |                                             |
                  v                                             v
        Wynik scenariusza CLI                       Endpoint HTTP /metrics
                                                               |
                                                               v
                                                   Prometheus -> Grafana

                       Niezależne laboratorium Linux
 tenant-west-host -- edge-west --+-- core-north --+-- edge-east -- tenant-east-host
                                 |                 |
                                 +-- core-south ---+
                                   FRR/eBGP underlay (sieć bazowa)
          Mosty Linux + VXLAN VNI 5001 łączą hosty tenantów
```

### Moduły C++

| Moduł | Odpowiedzialność |
|---|---|
| `src/main.cpp` | Sprawdza opcje CLI (wiersza poleceń) i uruchamia wybrany scenariusz. |
| `src/scenarios.cpp` | Buduje przykładowe topologie, uruchamia demonstracje i formatuje wyniki. |
| `src/network.cpp` | Zarządza routerami, łączami, podsieciami tenantów, uproszczoną zbieżnością tras, przekazywaniem pakietów i metrykami. |
| `src/ipv4.cpp` | Parsuje adresy i prefiksy IPv4 oraz udostępnia operacje sprawdzania przynależności i nakładania się prefiksów. |
| `src/ipv6.cpp` | Parsuje i formatuje adresy IPv6 używane jako SID w modelu. |
| `src/srv6.cpp` | Sprawdza poprawność polityki SRv6 i oblicza ścieżki przez aktywne łącza do wskazanych SID. |
| `src/metrics_server.cpp` | Udostępnia `GET /metrics` przez TCP/HTTP, aby Prometheus mógł pobierać metryki (scraping). |
| `tests/gtest_tests.cpp` | Zawiera testy jednostkowe oraz testy integracyjne sprawdzające wyniki scenariuszy za pomocą GoogleTest. |

### Struktura repozytorium

```text
.
├── .github/workflows/ci.yml     # Workflow CI: budowanie i testy w Windows i Ubuntu
├── include/edgecontrol/         # Publiczne deklaracje C++
├── src/                         # Implementacja C++20
├── tests/gtest_tests.cpp        # Testy jednostkowe i integracyjne GoogleTest
├── lab/containerlab/            # Topologia FRR, konfiguracje routerów, skrypty start/stop
├── observability/               # Konfiguracja Prometheusa i Grafany
├── CMakeLists.txt               # Cele budowania i konfiguracja GoogleTest
├── CMakePresets.json            # Presety Windows MinGW i Linux
├── Dockerfile                   # Wieloetapowy build; testy uruchamiane w etapie build
└── compose.yaml                 # Usługi symulatora, Prometheusa i Grafany
```

## Wymagania

### Budowanie C++ i stos metryk

- Docker Desktop z silnikiem kontenerów Linux albo CMake 3.21+, kompilator zgodny z C++20 i Git.
- Dostęp do Internetu przy pierwszym budowaniu: CMake `FetchContent` pobiera GoogleTest v1.16.0.

### Rzeczywiste laboratorium sieciowe

- Ubuntu w WSL2 albo host Linux.
- Docker Engine dostępny z tego środowiska Linux.
- Containerlab zainstalowany w tym samym środowisku.
- Uprawnienia do tworzenia uprzywilejowanych kontenerów sieciowych i interfejsów Linux.

Docker Desktop i Docker Engine zainstalowany wewnątrz Ubuntu WSL to osobne silniki. Polecenia Compose uruchamiaj z Windows PowerShell, aby korzystać z Docker Desktop; polecenia Containerlab uruchamiaj z Ubuntu, aby korzystać z Linux Engine. Nazwa topologii Containerlab to `edgecntrl-frr`; różni się od nazwy laboratorium w projekcie źródłowym.

## Budowanie i testowanie za pomocą Dockera

W Windows PowerShell, w katalogu głównym repozytorium:

```powershell
docker build --target build -t edgecntrl-simulator-build:local .
docker run --rm --entrypoint cmake edgecntrl-simulator-build:local --build build --clean-first --parallel 2
docker run --rm --entrypoint ctest edgecntrl-simulator-build:local --test-dir build --output-on-failure
```

Etap `build` w Dockerze instaluje CMake, g++ i Git, konfiguruje projekt, buduje go i uruchamia CTest. Jawne budowanie z opcją `--clean-first` sprawdza kompilację bez polegania na istniejących plikach obiektowych CMake.

### Budowanie i testowanie lokalnie

Jeśli CMake, kompilator C++20 i Git są dostępne w `PATH`:

```powershell
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

W Windows skrypt `run-tests.bat` używa presetu `windows-mingw` i wymaga CMake oraz MSYS2 MinGW UCRT64 (`g++` i `mingw32-make`) w `PATH`. Skrypt `build.bat` dodatkowo uruchamia scenariusz `all`, jeśli testy przejdą pomyślnie.

## Uruchamianie scenariuszy C++

Za pomocą obrazu Docker z etapem budowania:

```powershell
docker run --rm --entrypoint ./build/edgecontrol edgecntrl-simulator-build:local --scenario all
```

Dostępne scenariusze:

```text
all        Uruchamia demonstracje przełączania trasy, izolacji tenantów i modelu SRv6.
failover   Przekazuje pakiety przed awarią głównego łącza i po niej.
isolation  Porównuje zarejestrowanego tenanta z nieznanym tenantem.
srv6       Wykonuje listę segmentów, symuluje awarię i używa nowej listy.
serve      Przygotowuje przykładowe liczniki i udostępnia endpoint Prometheusa.
```

Możesz też zbudować i uruchomić samodzielny obraz runtime:

```powershell
docker build -t edgecntrl-simulator:local .
docker run --rm edgecntrl-simulator:local --scenario all
```

## Zapisany wynik symulatora

Poniżej znajduje się przechwycony wynik `--scenario all` z wersji, której działanie zostało zweryfikowane:

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

Wartości opóźnień są skonfigurowanymi wartościami modelu, a nie pomiarami z sieci fizycznej lub wirtualnej. Każdy scenariusz tworzy nową sieć w pamięci, więc jego liczniki są niezależne.

## Zestaw testów GoogleTest

Zestaw zawiera obecnie 16 wykrywanych testów: 13 ukierunkowanych testów jednostkowych i 3 testy integracyjne scenariuszy. GoogleMock jest wyłączony, ponieważ testowany kod nie wymaga atrap (mocków).

### Zapisany wynik CTest

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

Zakres testów:

- Parsowanie i formatowanie IPv4 i IPv6, sprawdzanie przynależności adresu do podsieci i nakładania się prefiksów.
- Walidacja topologii, ograniczeń tenant/VNI, wybór trasy, przełączanie po awarii łącza, wycofywanie tras i izolacja tenantów.
- Walidacja portu serwera metryk.
- Wykonywanie ścieżki SRv6, nieosiągalne SID, walidacja polityki, przełączanie po awarii i izolacja tenantów.
- Integracyjne sprawdzanie wyników scenariuszy przełączania trasy, izolacji i SRv6.

Czas testów zależy od komputera; oczekiwanym niezmiennikiem jest `100% tests passed, 0 tests failed`.

## Uruchamianie obserwowalności

W Windows PowerShell:

```powershell
docker compose up --build -d
docker compose ps
```

Adresy usług:

- Metryki symulatora: <http://localhost:9464/metrics>
- Prometheus: <http://localhost:9090>
- Cele monitorowania Prometheusa: <http://localhost:9090/targets>
- Grafana: <http://localhost:3000>

Prometheus odpytuje symulator pod adresem `simulator:9464/metrics` co pięć sekund. Grafana korzysta z przygotowanego źródła danych Prometheus i pulpitu. Usługi nasłuchują lokalnie (`127.0.0.1`), a nie na wszystkich interfejsach hosta.

Zatrzymanie stosu Compose:

```powershell
docker compose down
```

Nazwane wolumeny Prometheusa i Grafany zachowują dane po `down`. Używaj `docker compose down -v` tylko wtedy, gdy świadomie chcesz usunąć zapisane dane.

## Uruchamianie laboratorium FRR/BGP/VXLAN

Ten rozdział wykonuj w Ubuntu/WSL, a nie w Windows PowerShell. Środowisko Ubuntu musi mieć własny Docker Engine i skonfigurowany Containerlab. Laboratorium używa uprzywilejowanych kontenerów Linux i modyfikuje wyłącznie własną, odizolowaną topologię.

```bash
cd "/mnt/c/Users/kkowa/OneDrive/Dokumenty/Projekty/C++/EdgeCntrl-Simulator"
sudo bash lab/containerlab/scripts/run-lab.sh
```

Skrypt wdraża cztery routery FRR (`edge-west`, `core-north`, `core-south`, `edge-east`) oraz dwa hosty tenantów Alpine. Czeka na sesje eBGP, konfiguruje mosty Linux i VXLAN VNI 5001, sprawdza preferowaną trasę przez północny router, testuje ping pomiędzy hostami, wyłącza podstawowe łącze sieci bazowej, sprawdza przełączenie przez południowy router, przywraca łącze i weryfikuje powrót na trasę podstawową.

Oczekiwana końcowa linia:

```text
Real FRR/BGP/VXLAN failover and recovery passed. Lab containers remain running.
```

Skrypt celowo pozostawia laboratorium uruchomione do dalszej inspekcji. Zatrzymaj wyłącznie topologię tego projektu:

```bash
sudo bash lab/containerlab/scripts/stop-lab.sh
```

Topologia ma unikalną nazwę `edgecntrl-frr`, więc nie używa ponownie ani nie usuwa topologii `edgecontrol-frr` z projektu źródłowego.

## Uwagi projektowe i ograniczenia

- Rozgłaszanie tras i wybór ścieżki w `network.cpp` są deterministycznym, edukacyjnym przybliżeniem, a nie pełną implementacją protokołu BGP.
- Zachowanie powiązane z VXLAN w C++ modeluje przypisanie tenant/VNI. Rzeczywiste interfejsy VXLAN są konfigurowane wyłącznie w oddzielnym laboratorium.
- SRv6 jest modelem przekazywania pakietów w C++; nie jest instalowany nagłówek SRH ani trasa Linux `seg6local`.
- Prometheus/Grafana monitorują wyłącznie symulator C++; integracja z eksporterem FRR nie jest uwzględniona.
- Serwer HTTP metryk jest minimalnym, sekwencyjnym przykładem, a nie serwerem HTTP do produkcji.

## Ciągła integracja

`.github/workflows/ci.yml` konfiguruje projekt, buduje go i uruchamia CTest w Windows i Ubuntu przy pushach i pull requestach. Workflow CI weryfikuje projekt C++; nie wdraża uprzywilejowanej topologii Containerlab.

## Licencja

Plik `LICENSE` nie został dodany. Przed przyznaniem innym osobom praw do publicznego wykorzystania projektu wybierz i dodaj licencję.

## Zapis weryfikacji

Powyższy output README został przechwycony po czystej konfiguracji i kompilacji wszystkich celów C++ przez CMake oraz pełnym uruchomieniu CTest w środowisku budowania Docker. Wszystkie 16 testów zakończyło się powodzeniem. Wyniki scenariuszy są deterministyczne; czasy poszczególnych testów zależą od maszyny.
