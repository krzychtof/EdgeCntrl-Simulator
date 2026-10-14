#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace edgecontrol {

class Ipv4Address {
public:
    explicit Ipv4Address(std::uint32_t value = 0) noexcept;

    [[nodiscard]] static Ipv4Address parse(std::string_view text);
    [[nodiscard]] std::string toString() const;
    [[nodiscard]] std::uint32_t value() const noexcept;

    friend bool operator==(Ipv4Address lhs, Ipv4Address rhs) noexcept = default;

private:
    std::uint32_t value_;
};

class Ipv4Prefix {
public:
    [[nodiscard]] static Ipv4Prefix parse(std::string_view text);
    [[nodiscard]] bool contains(Ipv4Address address) const noexcept;
    [[nodiscard]] std::string toString() const;
    [[nodiscard]] Ipv4Address networkAddress() const noexcept;
    [[nodiscard]] std::uint8_t prefixLength() const noexcept;
    [[nodiscard]] bool overlaps(const Ipv4Prefix& other) const noexcept;

    friend bool operator==(const Ipv4Prefix& lhs, const Ipv4Prefix& rhs) noexcept = default;

private:
    Ipv4Prefix(Ipv4Address network, std::uint8_t prefix_length) noexcept;

    Ipv4Address network_;
    std::uint8_t prefix_length_;
};

} // namespace edgecontrol
