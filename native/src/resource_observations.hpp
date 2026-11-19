// SPDX-License-Identifier: MIT
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <unordered_map>

namespace crest {

struct ResourceActor {
    std::uint32_t id{};
    std::uint64_t incarnation{};
    bool operator==(const ResourceActor&) const = default;
};

struct ResourceValues {
    std::optional<double> mana_percent;
    std::optional<double> endurance_percent;
};

// Identify the actor even in an oversized update so an invalid reading can be retired.
std::optional<std::uint16_t> points_actor_id(std::uint16_t opcode, std::span<const std::byte> bytes);

class ResourceObservations {
public:
    // Supply a current group member or extended target. The caller owns that scope.
    bool receive(std::uint16_t opcode, std::span<const std::byte> bytes, ResourceActor actor);
    ResourceValues values(ResourceActor actor) const;
    void prune(std::span<const ResourceActor> tracked);
    void remove(std::uint32_t id);
    void reset();

private:
    struct Observation {
        std::uint64_t incarnation{};
        ResourceValues values;
    };

    std::unordered_map<std::uint32_t, Observation> entries_;
};

} // namespace crest
