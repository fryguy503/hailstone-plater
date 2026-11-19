// SPDX-License-Identifier: MIT
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace crest::wire {

using Bytes = std::span<const std::byte>;

// Callers validate the packet size before reading its fields.
inline std::uint8_t byte(Bytes bytes, std::size_t index) {
    return std::to_integer<std::uint8_t>(bytes[index]);
}

inline std::uint16_t le16(Bytes bytes, std::size_t index) {
    return static_cast<std::uint16_t>(byte(bytes, index) | (byte(bytes, index + 1) << 8));
}

inline std::uint32_t le32(Bytes bytes, std::size_t index) {
    return le16(bytes, index) | (std::uint32_t{le16(bytes, index + 2)} << 16);
}

} // namespace crest::wire
