// SPDX-License-Identifier: MIT
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <unordered_map>

namespace crest {

struct IndicatorState {
    std::uint8_t mark{};      // 1–8, or 0 for no mark
    std::uint8_t awareness{}; // 1 detects, 2 engaged, 3 both
    std::uint8_t status{};    // 1 mez, 2 pacify, 3 both
};

class ServerIndicators {
public:
    // The return value describes accepted data only. The host should always keep the packet.
    bool receive(std::uint16_t opcode, std::span<const std::byte> bytes, std::uint64_t now_ms);
    IndicatorState indicators(std::uint32_t id, std::uint64_t now_ms, bool in_raid);
    std::optional<std::uint8_t> concealment(std::uint32_t id, std::uint64_t now_ms);
    void remove(std::uint32_t id);
    void reset();

private:
    static constexpr std::size_t max_chunks = 9;
    enum class Stream : std::size_t { Awareness, Status, Concealment, Count };
    using Entries = std::unordered_map<std::uint16_t, std::uint8_t>;

    struct Published {
        std::uint32_t generation{};
        std::uint64_t received_at{};
        Entries entries;
    };
    struct Pending {
        std::uint32_t generation{};
        std::uint64_t started_at{};
        std::size_t chunk_count{};
        std::size_t received_count{};
        std::array<std::optional<Entries>, max_chunks> chunks;
        Entries entries;
    };
    struct Snapshot {
        std::optional<Published> published;
        std::optional<Pending> pending;
        std::optional<std::uint32_t> removed_generation;
    };

    bool receive_snapshot(std::span<const std::byte> bytes, std::uint64_t now_ms);
    bool receive_marker(std::span<const std::byte> bytes, std::uint64_t now_ms);
    std::optional<std::uint8_t> lookup(Stream stream, std::uint32_t id) const;
    void advance(std::uint64_t now_ms);

    std::array<Snapshot, static_cast<std::size_t>(Stream::Count)> snapshots_{};
    std::array<std::array<std::uint32_t, 8>, 2> marks_{};
    std::optional<std::uint64_t> last_time_;
};

} // namespace crest
