// SPDX-License-Identifier: MIT
#include "resource_observations.hpp"
#include "wire_bytes.hpp"

#include <algorithm>
#include <limits>

namespace crest {
namespace {

constexpr std::uint16_t mana_opcode = 0x3791;
constexpr std::uint16_t endurance_opcode = 0x5f42;
constexpr std::size_t packet_size = 10;
constexpr std::size_t max_entries = 128;
constexpr auto max_points = std::uint32_t{std::numeric_limits<std::int32_t>::max()};

} // namespace

std::optional<std::uint16_t> points_actor_id(std::uint16_t opcode, wire::Bytes bytes) {
    if ((opcode != mana_opcode && opcode != endurance_opcode) || bytes.size() < packet_size) return std::nullopt;
    const auto id = wire::le16(bytes, 8);
    if (id == 0) return std::nullopt;
    return id;
}

bool ResourceObservations::receive(std::uint16_t opcode, wire::Bytes bytes, ResourceActor actor) {
    const auto id = points_actor_id(opcode, bytes);
    if (!id || *id != actor.id || actor.incarnation == 0) return false;

    auto entry = entries_.find(actor.id);
    if (entry != entries_.end() && entry->second.incarnation != actor.incarnation) {
        entries_.erase(entry);
        entry = entries_.end();
    }

    const auto current = wire::le32(bytes, 0);
    const auto maximum = wire::le32(bytes, 4);
    const auto pool = opcode == mana_opcode ? &ResourceValues::mana_percent : &ResourceValues::endurance_percent;
    if (bytes.size() != packet_size || maximum == 0 || maximum > max_points || current > maximum) {
        if (entry != entries_.end()) {
            auto& values = entry->second.values;
            (values.*pool).reset();
            if (!values.mana_percent && !values.endurance_percent) entries_.erase(entry);
        }
        return false;
    }
    if (entry == entries_.end()) {
        if (entries_.size() >= max_entries) return false;
        entry = entries_.emplace(actor.id, Observation{actor.incarnation, {}}).first;
    }
    entry->second.values.*pool = 100.0 * current / maximum;
    return true;
}

ResourceValues ResourceObservations::values(ResourceActor actor) const {
    const auto entry = entries_.find(actor.id);
    if (entry == entries_.end() || entry->second.incarnation != actor.incarnation) return {};
    return entry->second.values;
}

void ResourceObservations::prune(std::span<const ResourceActor> tracked) {
    std::erase_if(entries_, [&](const auto& entry) {
        return std::ranges::find(tracked, ResourceActor{entry.first, entry.second.incarnation}) == tracked.end();
    });
}

void ResourceObservations::remove(std::uint32_t id) {
    entries_.erase(id);
}

void ResourceObservations::reset() {
    entries_.clear();
}

} // namespace crest
