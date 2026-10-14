#include "edgecontrol/ipv4.hpp"

#include <charconv>
#include <stdexcept>
#include <string>
#include <system_error>

namespace edgecontrol {

Ipv4Address::Ipv4Address(std::uint32_t value) noexcept : value_(value) {}

Ipv4Address Ipv4Address::parse(std::string_view text) {
    std::uint32_t address = 0;
    std::size_t start = 0;

    for (int octet_index = 0; octet_index < 4; ++octet_index) {
        const std::size_t end = text.find('.', start);
        const bool last_octet = octet_index == 3;
        if ((last_octet && end != std::string_view::npos) ||
            (!last_octet && end == std::string_view::npos)) {
            throw std::invalid_argument("Invalid IPv4 address: " + std::string(text));
        }

        const std::size_t octet_end = last_octet ? text.size() : end;
        const std::string_view part = text.substr(start, octet_end - start);
        unsigned int octet = 0;
        const auto [parsed_end, error] =
            std::from_chars(part.data(), part.data() + part.size(), octet);
        if (part.empty() || error != std::errc{} || parsed_end != part.data() + part.size() ||
            octet > 255U) {
            throw std::invalid_argument("Invalid IPv4 address: " + std::string(text));
        }

        address = (address << 8U) | octet;
        start = octet_end + 1;
    }

    return Ipv4Address(address);
}

std::string Ipv4Address::toString() const {
    return std::to_string((value_ >> 24U) & 0xFFU) + "." +
           std::to_string((value_ >> 16U) & 0xFFU) + "." +
           std::to_string((value_ >> 8U) & 0xFFU) + "." +
           std::to_string(value_ & 0xFFU);
}

std::uint32_t Ipv4Address::value() const noexcept {
    return value_;
}

Ipv4Prefix::Ipv4Prefix(Ipv4Address network, std::uint8_t prefix_length) noexcept
    : network_(network), prefix_length_(prefix_length) {}

Ipv4Prefix Ipv4Prefix::parse(std::string_view text) {
    const std::size_t slash = text.find('/');
    if (slash == std::string_view::npos || text.find('/', slash + 1) != std::string_view::npos) {
        throw std::invalid_argument("Invalid IPv4 prefix: " + std::string(text));
    }

    const Ipv4Address address = Ipv4Address::parse(text.substr(0, slash));
    const std::string_view length_text = text.substr(slash + 1);
    unsigned int length = 0;
    const auto [parsed_end, error] =
        std::from_chars(length_text.data(), length_text.data() + length_text.size(), length);
    if (length_text.empty() || error != std::errc{} ||
        parsed_end != length_text.data() + length_text.size() || length > 32U) {
        throw std::invalid_argument("Invalid IPv4 prefix length: " + std::string(text));
    }

    const std::uint32_t mask =
        length == 0U ? 0U : (0xFFFFFFFFU << (32U - length));
    return Ipv4Prefix(Ipv4Address(address.value() & mask), static_cast<std::uint8_t>(length));
}

bool Ipv4Prefix::contains(Ipv4Address address) const noexcept {
    const std::uint32_t mask =
        prefix_length_ == 0U ? 0U : (0xFFFFFFFFU << (32U - prefix_length_));
    return (address.value() & mask) == network_.value();
}

std::string Ipv4Prefix::toString() const {
    return network_.toString() + "/" + std::to_string(prefix_length_);
}

Ipv4Address Ipv4Prefix::networkAddress() const noexcept {
    return network_;
}

std::uint8_t Ipv4Prefix::prefixLength() const noexcept {
    return prefix_length_;
}

bool Ipv4Prefix::overlaps(const Ipv4Prefix& other) const noexcept {
    return prefix_length_ <= other.prefix_length_ ? contains(other.network_)
                                                  : other.contains(network_);
}

} // namespace edgecontrol
