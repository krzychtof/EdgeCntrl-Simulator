#include "edgecontrol/scenarios.hpp"

#include <charconv>
#include <exception>
#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
    std::string scenario = "all";
    std::uint16_t metrics_port = 9464U;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--help" || argument == "-h") {
            edgecontrol::printHelp(std::cout);
            return 0;
        }
        if (argument == "--scenario" && index + 1 < argc) {
            scenario = argv[++index];
            continue;
        }
        if (argument == "--metrics-port" && index + 1 < argc) {
            const std::string port_text = argv[++index];
            unsigned int port = 0;
            const auto [end, error] =
                std::from_chars(port_text.data(), port_text.data() + port_text.size(), port);
            if (error != std::errc{} || end != port_text.data() + port_text.size() ||
                port == 0U || port > 65535U) {
                std::cerr << "Metrics port must be an integer in range 1..65535.\n";
                return 2;
            }
            metrics_port = static_cast<std::uint16_t>(port);
            continue;
        }
        std::cerr << "Unknown or incomplete argument: " << argument << "\n\n";
        edgecontrol::printHelp(std::cerr);
        return 2;
    }

    if (!edgecontrol::isKnownScenario(scenario)) {
        std::cerr << "Unknown scenario: " << scenario << "\n\n";
        edgecontrol::printHelp(std::cerr);
        return 2;
    }

    try {
        if (scenario == "all") {
            edgecontrol::runAllScenarios(std::cout);
        } else if (scenario == "failover") {
            edgecontrol::runFailoverScenario(std::cout);
        } else if (scenario == "isolation") {
            edgecontrol::runIsolationScenario(std::cout);
        } else if (scenario == "srv6") {
            edgecontrol::runSrv6Scenario(std::cout);
        } else {
            edgecontrol::runMetricsServer(metrics_port, std::cout);
        }
    } catch (const std::exception& error) {
        std::cerr << "Simulation failed: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
