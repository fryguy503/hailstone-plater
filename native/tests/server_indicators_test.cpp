// SPDX-License-Identifier: MIT
#include "server_indicators.hpp"

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <iostream>
#include <span>
#include <stdexcept>
#include <vector>

namespace {

constexpr std::uint16_t snapshot_opcode = 0x7d1d;
constexpr std::uint16_t marker_opcode = 0x7d1c;
constexpr std::uint32_t awareness = 0x4e504131;
constexpr std::uint32_t status = 0x4e505331;
constexpr std::uint32_t concealment = 0x4e504331;
using Bytes = std::vector<std::byte>;

struct Entry {
    std::uint16_t id;
    std::uint8_t flags;
    std::uint8_t reserved = 0;
};

void require(bool value, const char* failure) {
    if (!value) throw std::runtime_error(failure);
}

void append_le(Bytes& bytes, std::uint32_t value, unsigned length) {
    for (unsigned i = 0; i < length; ++i) {
        bytes.push_back(static_cast<std::byte>(value & 0xffu));
        value >>= 8;
    }
}

Bytes snapshot_from(std::uint32_t magic, std::uint32_t generation, std::uint16_t index, std::uint16_t chunks,
                    std::span<const Entry> entries) {
    Bytes bytes;
    append_le(bytes, magic, 4);
    append_le(bytes, 1, 1); // version
    append_le(bytes, 1, 1); // full snapshot
    append_le(bytes, static_cast<std::uint32_t>(entries.size()), 2);
    append_le(bytes, generation, 4);
    append_le(bytes, index, 2);
    append_le(bytes, chunks, 2);
    for (const auto& [id, flags, reserved] : entries) {
        append_le(bytes, id, 2);
        append_le(bytes, flags, 1);
        append_le(bytes, reserved, 1);
    }
    return bytes;
}

Bytes snapshot(std::uint32_t magic, std::uint32_t generation, std::uint16_t index, std::uint16_t chunks,
               std::initializer_list<Entry> entries = {}) {
    return snapshot_from(magic, generation, index, chunks, {entries.begin(), entries.size()});
}

Bytes mark(std::uint8_t action, std::uint8_t icon, std::uint8_t scope, std::uint32_t target) {
    Bytes bytes;
    append_le(bytes, 0x4e504d31, 4);
    append_le(bytes, 1, 1);
    append_le(bytes, action, 1);
    append_le(bytes, icon, 1);
    append_le(bytes, scope, 1);
    append_le(bytes, target, 4);
    append_le(bytes, 0, 4);
    return bytes;
}

void independent_snapshots_and_expiry() {
    crest::ServerIndicators decoder;
    require(decoder.receive(snapshot_opcode, snapshot(awareness, 19, 0, 1, {{1, 3}}), 0), "awareness");
    require(decoder.receive(snapshot_opcode, snapshot(status, 5, 0, 1, {{1, 2}}), 1), "status");
    require(decoder.receive(snapshot_opcode, snapshot(concealment, 4, 1, 2, {{1, 3}}), 2), "out of order");
    require(!decoder.concealment(1, 2), "incomplete snapshot published");
    const auto first = snapshot(concealment, 4, 0, 2, {{2, 1}, {3, 2}});
    require(decoder.receive(snapshot_opcode, first, 3), "complete snapshot rejected");
    require(decoder.concealment(1, 3) == 3 && decoder.concealment(2, 3) == 1 && decoder.concealment(3, 3) == 2,
            "concealment flags lost");
    const auto flags = decoder.indicators(1, 3, false);
    require(flags.awareness == 3 && flags.status == 2, "snapshot stream collision");
    require(decoder.receive(snapshot_opcode, first, 4000), "replay rejected");
    require(decoder.indicators(1, 5001, false).awareness == 0, "awareness exceeded TTL");
    require(decoder.indicators(1, 5001, false).status == 2, "status expired early");
    require(decoder.concealment(1, 5003) == 3, "concealment expired early");
    require(!decoder.concealment(1, 5004), "replayed snapshot extended its TTL");
}

void malformed_packets() {
    crest::ServerIndicators decoder;
    const auto good_snapshot = snapshot(awareness, 1, 0, 1, {{1, 1}});
    const auto good_mark = mark(1, 1, 1, 1);
    for (std::size_t length = 0; length < good_snapshot.size(); ++length)
        require(!decoder.receive(snapshot_opcode, std::span(good_snapshot).first(length), 0), "truncated snapshot");
    for (std::size_t length = 0; length < good_mark.size(); ++length)
        require(!decoder.receive(marker_opcode, std::span(good_mark).first(length), 0), "truncated marker");

    for (const auto& packet : {snapshot(0, 1, 0, 1), snapshot(awareness, 1, 0, 0), snapshot(awareness, 1, 0, 10),
                               snapshot(awareness, 1, 0, 2), snapshot(status, 1, 2, 2, {{1, 1}}),
                               snapshot(status, 1, 0, 1, {{0, 1}}), snapshot(status, 1, 0, 1, {{1, 0}}),
                               snapshot(status, 1, 0, 1, {{1, 4}}), snapshot(status, 1, 0, 1, {{1, 1, 1}}),
                               snapshot(status, 1, 0, 1, {{1, 1}, {1, 2}})})
        require(!decoder.receive(snapshot_opcode, packet, 0), "malformed snapshot accepted");

    for (const auto& packet : {mark(1, 1, 3, 1), mark(1, 9, 1, 1), mark(1, 1, 1, 0), mark(2, 1, 1, 1),
                               mark(3, 1, 1, 0), mark(4, 0, 1, 1), mark(5, 1, 1, 1), mark(6, 1, 1, 1)})
        require(!decoder.receive(marker_opcode, packet, 0), "malformed marker accepted");

    auto reserved = good_mark;
    reserved[12] = std::byte{1};
    require(!decoder.receive(marker_opcode, reserved, 0), "marker reserved data accepted");
    auto version = good_snapshot;
    version[4] = std::byte{2};
    require(!decoder.receive(snapshot_opcode, version, 0), "unsupported version accepted");
    auto flags = good_snapshot;
    flags[5] = std::byte{3};
    require(!decoder.receive(snapshot_opcode, flags, 0), "unsupported flags accepted");
    require(!decoder.receive(0, good_snapshot, 0), "unknown opcode accepted");
}

void staging_and_generation_order() {
    crest::ServerIndicators decoder;
    require(decoder.receive(snapshot_opcode, snapshot(awareness, 0xffffffffu, 0, 1, {{7, 1}}), 0), "generation max");
    require(decoder.receive(snapshot_opcode, snapshot(awareness, 1, 0, 2, {{1, 1}}), 1), "wrapped generation");
    require(!decoder.receive(snapshot_opcode, snapshot(awareness, 1, 1, 2, {{1, 2}}), 2), "cross-chunk duplicate");
    require(decoder.indicators(7, 2, false).awareness == 1, "lost prior snapshot on bad chunk");
    require(decoder.receive(snapshot_opcode, snapshot(awareness, 1, 0, 2, {{1, 1}, {2, 2}}), 3), "restart staging");
    require(decoder.receive(snapshot_opcode, snapshot(awareness, 1, 0, 2, {{2, 2}, {1, 1}}), 4), "reordered replay");
    require(!decoder.receive(snapshot_opcode, snapshot(awareness, 1, 0, 2, {{1, 3}}), 5), "conflicting replay");
    require(decoder.receive(snapshot_opcode, snapshot(awareness, 2, 0, 1, {{2, 1}}), 6), "later generation");
    require(!decoder.receive(snapshot_opcode, snapshot(awareness, 0x80000002u, 0, 1, {{3, 1}}), 7), "ambiguous wrap");
    require(!decoder.receive(snapshot_opcode, snapshot(awareness, 1, 0, 1, {{3, 1}}), 8), "old generation");
    require(decoder.receive(snapshot_opcode, snapshot(awareness, 3, 0, 1), 9), "empty snapshot");
    require(decoder.indicators(2, 9, false).awareness == 0, "empty snapshot failed to clear");
}

void snapshot_size_limits() {
    crest::ServerIndicators decoder;
    for (std::uint32_t generation = 1; generation <= 2; ++generation) {
        for (std::uint16_t chunk = 0; chunk < 9; ++chunk) {
            std::vector<Entry> entries;
            const auto count = generation == 1 && chunk == 8 ? 48 : 122;
            for (int index = 1; index <= count; ++index)
                entries.push_back({static_cast<std::uint16_t>(chunk * 122 + index), 1});
            const bool accepted = decoder.receive(snapshot_opcode, snapshot_from(status, generation, chunk, 9, entries),
                                                  (generation - 1) * 10 + chunk);
            require(accepted == (generation == 1 || chunk != 8), "aggregate snapshot limit");
        }
    }
    require(decoder.indicators(1024, 19, false).status == 1, "maximum valid snapshot lost");
    require(decoder.indicators(1025, 19, false).status == 0, "oversized snapshot published");
}

void marks_and_identity() {
    crest::ServerIndicators decoder;
    require(decoder.receive(marker_opcode, mark(1, 1, 1, 1), 0), "group mark");
    require(decoder.receive(marker_opcode, mark(1, 2, 1, 1), 0), "replace mark");
    require(decoder.receive(marker_opcode, mark(1, 3, 2, 1), 0), "raid mark");
    require(decoder.indicators(1, 10'000, false).mark == 2 && decoder.indicators(1, 10'000, true).mark == 3,
            "lost separate group and raid marks");
    require(decoder.receive(marker_opcode, mark(2, 2, 1, 0), 10'000), "clear mark");
    require(decoder.indicators(1, 10'000, false).mark == 0, "mark clear retained old icon");
    require(decoder.receive(marker_opcode, mark(5, 0, 2, 1), 10'000), "clear target mark");
    require(decoder.indicators(1, 10'000, true).mark == 0, "target mark left behind");
    require(decoder.receive(marker_opcode, mark(1, 8, 2, 70'001), 10'000), "wide marker id");
    require(decoder.indicators(70'001, 10'000, true).mark == 8, "marker id narrowed");

    require(decoder.receive(snapshot_opcode, snapshot(concealment, 2, 0, 1, {{1, 1}}), 10'000), "initial snapshot");
    require(decoder.receive(snapshot_opcode, snapshot(concealment, 3, 0, 2, {{1, 3}}), 10'000), "staged snapshot");
    require(decoder.receive(marker_opcode, mark(1, 4, 1, 1), 10'000), "mark before removal");
    decoder.remove(1);
    require(!decoder.concealment(1, 10'000) && decoder.indicators(1, 10'000, false).mark == 0, "stale removed actor");
    require(!decoder.receive(snapshot_opcode, snapshot(concealment, 3, 1, 2, {{2, 3}}), 10'001), "late removed chunk");
    require(decoder.receive(snapshot_opcode, snapshot(concealment, 4, 0, 1, {{1, 2}}), 10'002), "fresh reuse");
    require(decoder.concealment(1, 10'002) == 2, "new actor could not establish concealment");
    require(decoder.indicators(70'001, 9'999, true).mark == 0, "backwards clock retained mark");
    require(!decoder.concealment(1, 9'999), "backwards clock retained concealment");
}

} // namespace

int main() {
    try {
        independent_snapshots_and_expiry();
        malformed_packets();
        staging_and_generation_order();
        snapshot_size_limits();
        marks_and_identity();
        std::cout << "Crest server indicator tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Crest server indicator test failed: " << error.what() << '\n';
        return 1;
    }
}
