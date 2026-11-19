// SPDX-License-Identifier: MIT
#include "server_indicators.hpp"
#include "wire_bytes.hpp"

#include <algorithm>
#include <limits>
#include <utility>

namespace crest {
namespace {

constexpr std::uint16_t snapshot_opcode = 0x7d1d;
constexpr std::uint16_t marker_opcode = 0x7d1c;
constexpr std::uint32_t awareness_magic = 0x4e504131;
constexpr std::uint32_t status_magic = 0x4e505331;
constexpr std::uint32_t concealment_magic = 0x4e504331;
constexpr std::uint32_t marker_magic = 0x4e504d31;
constexpr std::size_t header_size = 16;
constexpr std::size_t entry_size = 4;
constexpr std::size_t max_chunk_entries = 122;
constexpr std::size_t max_entries = 1024;
constexpr std::uint64_t expiry_ms = 5000;

using wire::byte;
using wire::Bytes;
using wire::le16;
using wire::le32;

bool newer(std::uint32_t candidate, std::uint32_t previous) {
    const auto difference = candidate - previous;
    return difference != 0 && difference < 0x80000000u;
}

} // namespace

bool ServerIndicators::receive(std::uint16_t opcode, Bytes bytes, std::uint64_t now_ms) {
    switch (opcode) {
    case snapshot_opcode: return receive_snapshot(bytes, now_ms);
    case marker_opcode: return receive_marker(bytes, now_ms);
    default: return false;
    }
}

bool ServerIndicators::receive_snapshot(Bytes bytes, std::uint64_t now_ms) {
    if (bytes.size() < header_size || bytes.size() > header_size + max_chunk_entries * entry_size) return false;
    Stream stream{};
    switch (le32(bytes, 0)) {
    case awareness_magic: stream = Stream::Awareness; break;
    case status_magic: stream = Stream::Status; break;
    case concealment_magic: stream = Stream::Concealment; break;
    default: return false;
    }

    const auto count = le16(bytes, 6);
    const auto generation = le32(bytes, 8);
    const auto index = le16(bytes, 12);
    const auto chunk_count = le16(bytes, 14);
    if (byte(bytes, 4) != 1 || byte(bytes, 5) != 1 || count > max_chunk_entries ||
        chunk_count == 0 || chunk_count > max_chunks || index >= chunk_count ||
        bytes.size() != header_size + count * entry_size || (count == 0 && chunk_count != 1)) return false;

    Entries chunk;
    chunk.reserve(count);
    for (std::size_t offset = header_size; offset < bytes.size(); offset += entry_size) {
        const auto id = le16(bytes, offset);
        const auto flags = byte(bytes, offset + 2);
        if (id == 0 || flags < 1 || flags > 3 || byte(bytes, offset + 3) != 0 || !chunk.emplace(id, flags).second)
            return false;
    }

    advance(now_ms);
    auto& snapshot = snapshots_[static_cast<std::size_t>(stream)];
    if (snapshot.removed_generation && !newer(generation, *snapshot.removed_generation)) return false;
    if (snapshot.published) {
        if (generation == snapshot.published->generation) return true; // A replay cannot keep data alive.
        if (!newer(generation, snapshot.published->generation)) return false;
    }

    if (snapshot.pending && generation != snapshot.pending->generation) {
        if (!newer(generation, snapshot.pending->generation)) return false;
        snapshot.pending.reset();
    }
    if (!snapshot.pending) {
        auto& initial = snapshot.pending.emplace();
        initial.generation = generation;
        initial.started_at = now_ms;
        initial.chunk_count = chunk_count;
    }
    auto& pending = *snapshot.pending;
    if (pending.chunks[index] && pending.chunk_count == chunk_count && *pending.chunks[index] == chunk) return true;
    if (pending.chunk_count != chunk_count || pending.chunks[index] || pending.entries.size() + count > max_entries) {
        snapshot.pending.reset();
        return false;
    }
    for (const auto& [id, flags] : chunk) {
        if (!pending.entries.emplace(id, flags).second) {
            snapshot.pending.reset();
            return false;
        }
    }
    pending.chunks[index] = std::move(chunk);
    if (++pending.received_count == pending.chunk_count) {
        snapshot.published = Published{generation, now_ms, std::move(pending.entries)};
        snapshot.pending.reset();
        snapshot.removed_generation.reset();
    }
    return true;
}

bool ServerIndicators::receive_marker(Bytes bytes, std::uint64_t now_ms) {
    if (bytes.size() != header_size || le32(bytes, 0) != marker_magic || byte(bytes, 4) != 1 ||
        le32(bytes, 12) != 0) return false;
    const auto action = byte(bytes, 5);
    const auto mark = byte(bytes, 6);
    const auto scope = byte(bytes, 7);
    const auto target = le32(bytes, 8);
    if (scope != 1 && scope != 2) return false;
    auto& marks = marks_[scope - 1];
    const bool mark_valid = mark >= 1 && mark <= marks.size();
    const bool valid = (action == 1 && mark_valid && target != 0) ||
                       (action == 2 && mark_valid && target == 0) ||
                       ((action == 3 || action == 4) && mark == 0 && target == 0) ||
                       (action == 5 && mark == 0 && target != 0);
    if (!valid) return false;

    advance(now_ms);
    switch (action) {
    case 1:
        std::ranges::replace(marks, target, 0u);
        marks[mark - 1] = target;
        break;
    case 2:
        marks[mark - 1] = 0;
        break;
    case 3:
    case 4:
        marks.fill(0);
        break;
    case 5:
        std::ranges::replace(marks, target, 0u);
        break;
    }
    return true;
}

void ServerIndicators::advance(std::uint64_t now_ms) {
    if (last_time_ == now_ms) return;
    if (last_time_ && now_ms < *last_time_) reset();
    last_time_ = now_ms;
    for (auto& snapshot : snapshots_) {
        if (snapshot.published && now_ms - snapshot.published->received_at > expiry_ms) snapshot.published.reset();
        if (snapshot.pending && now_ms - snapshot.pending->started_at > expiry_ms) snapshot.pending.reset();
    }
}

std::optional<std::uint8_t> ServerIndicators::lookup(Stream stream, std::uint32_t id) const {
    const auto& snapshot = snapshots_[static_cast<std::size_t>(stream)].published;
    if (id == 0 || id > std::numeric_limits<std::uint16_t>::max() || !snapshot) return std::nullopt;
    const auto entry = snapshot->entries.find(static_cast<std::uint16_t>(id));
    if (entry == snapshot->entries.end()) return std::nullopt;
    return entry->second;
}

IndicatorState ServerIndicators::indicators(std::uint32_t id, std::uint64_t now_ms, bool in_raid) {
    advance(now_ms);
    if (id == 0) return {};
    const auto& marks = marks_[in_raid ? 1 : 0];
    const auto mark = std::ranges::find(marks, id);
    return {mark == marks.end() ? std::uint8_t{} : static_cast<std::uint8_t>(mark - marks.begin() + 1),
            lookup(Stream::Awareness, id).value_or(0), lookup(Stream::Status, id).value_or(0)};
}

std::optional<std::uint8_t> ServerIndicators::concealment(std::uint32_t id, std::uint64_t now_ms) {
    advance(now_ms);
    return lookup(Stream::Concealment, id);
}

void ServerIndicators::remove(std::uint32_t id) {
    if (id == 0) return;
    for (auto& snapshot : snapshots_) {
        if (snapshot.published && id <= std::numeric_limits<std::uint16_t>::max())
            snapshot.published->entries.erase(static_cast<std::uint16_t>(id));
        if (snapshot.pending) {
            // Late chunks from this generation must not restore a despawned actor.
            snapshot.removed_generation = snapshot.pending->generation;
            snapshot.pending.reset();
        }
    }
    for (auto& marks : marks_) std::ranges::replace(marks, id, 0u);
}

void ServerIndicators::reset() {
    snapshots_ = {};
    marks_ = {};
    last_time_.reset();
}

} // namespace crest
