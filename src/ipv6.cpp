#include "edgecontrol/ipv6.hpp"

#include <algorithm>
#include <charconv>
#include <stdexcept>
#include <system_error>
#include <vector>

namespace edgecontrol {
namespace {

std::vector<std::uint16_t> parseGroups(std::string_view text) {
    std::vector<std::uint16_t> groups;
    if (text.empty()) {
        return groups;
    }

    std::size_t start = 0;
    while (start < text.size()) {
        const std::size_t end = text.find(':', start);
        const std::size_t group_end = end == std::string_view::npos ? text.size() : end;
        const std::string_view group = text.substr(start, group_end - start);
        unsigned int value = 0;
        const auto [parsed_end, error] =
            std::from_chars(group.data(), group.data() + group.size(), value, 16);
        if (group.empty() || group.size() > 4U || error != std::errc{} ||
            parsed_end != group.data() + group.size() || value > 0xFFFFU) {
            throw std::invalid_argument("Invalid IPv6 address.");
        }
        groups.push_back(static_cast<std::uint16_t>(value));
        if (end == std::string_view::npos) {
            break;
        }
        start = end + 1U;
        if (start == text.size()) {
            throw std::invalid_argument("Invalid IPv6 address.");
        }
    }
    return groups;
}

} // namespace

Ipv6Address::Ipv6Address(std::array<std::uint16_t, 8> groups) noexcept : groups_(groups) {}

Ipv6Address Ipv6Address::parse(std::string_view text) {
    if (text.empty()) {
        throw std::invalid_argument("Invalid IPv6 address.");
    }

    const std::size_t compression = text.find("::");
    std::array<std::uint16_t, 8> groups{};
    if (compression == std::string_view::npos) {
        const auto parsed = parseGroups(text);
        if (parsed.size() != groups.size()) {
            throw std::invalid_argument("IPv6 address must contain eight groups or use ::.");
        }
        std::copy(parsed.begin(), parsed.end(), groups.begin());
        return Ipv6Address(groups);
    }

    if (text.find("::", compression + 2U) != std::string_view::npos) {
        throw std::invalid_argument("IPv6 address may contain :: only once.");
    }
    const auto left = parseGroups(text.substr(0, compression));
    const auto right = parseGroups(text.substr(compression + 2U));
    if (left.size() + right.size() >= groups.size()) {
        throw std::invalid_argument("IPv6 :: must replace at least one group.");
    }

    std::copy(left.begin(), left.end(), groups.begin());
    std::copy(right.begin(), right.end(), groups.end() -
                                               static_cast<std::ptrdiff_t>(right.size()));
    return Ipv6Address(groups);
}

std::string Ipv6Address::toString() const {
    std::size_t best_start = groups_.size();
    std::size_t best_length = 0;
    for (std::size_t index = 0; index < groups_.size();) {
        if (groups_[index] != 0U) {
            ++index;
            continue;
        }
        const std::size_t start = index;
        while (index < groups_.size() && groups_[index] == 0U) {
            ++index;
        }
        const std::size_t length = index - start;
        if (length > best_length && length >= 2U) {
            best_start = start;
            best_length = length;
        }
    }

    std::string result;
    for (std::size_t index = 0; index < groups_.size();) {
        if (index == best_start) {
            result += "::";
            index += best_length;
            if (index == groups_.size()) {
                break;
            }
            continue;
        }
        if (!result.empty() && result.back() != ':') {
            result += ':';
        }
        char buffer[5]{};
        const auto [end, error] =
            std::to_chars(std::begin(buffer), std::end(buffer), groups_[index], 16);
        if (error != std::errc{}) {
            throw std::runtime_error("Could not format IPv6 address.");
        }
        result.append(buffer, end);
        ++index;
    }
    return result;
}

} // namespace edgecontrol
