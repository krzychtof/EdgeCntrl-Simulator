#pragma once

#include <iosfwd>
#include <cstdint>
#include <string_view>

namespace edgecontrol {

void runFailoverScenario(std::ostream& output);
void runIsolationScenario(std::ostream& output);
void runSrv6Scenario(std::ostream& output);
void runAllScenarios(std::ostream& output);
void runMetricsServer(std::uint16_t port, std::ostream& output);
void printHelp(std::ostream& output);
[[nodiscard]] bool isKnownScenario(std::string_view name) noexcept;

} // namespace edgecontrol
