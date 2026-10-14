#pragma once

#include <cstdint>

namespace edgecontrol {

class Network;

void servePrometheusMetrics(const Network& network, std::uint16_t port);

} // namespace edgecontrol
