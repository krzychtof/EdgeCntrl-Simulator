#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace edgecontrol {

class Ipv6Address {
public:
    Ipv6Address() noexcept = default;

    [[nodiscard]] static Ipv6Address parse(std::string_view text);
    [[nodiscard]] std::string toString() const;

    friend bool operator==(const Ipv6Address& lhs, const Ipv6Address& rhs) noexcept = default;

private:
    explicit Ipv6Address(std::array<std::uint16_t, 8> groups) noexcept;

    std::array<std::uint16_t, 8> groups_{};
};

} // namespace edgecontrol
