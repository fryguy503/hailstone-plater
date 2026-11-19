// SPDX-License-Identifier: MIT
#pragma once

#include "hailstone_plugin.h"
#include "resource_observations.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace crest {

class Settings;

struct PlayerGameplay {
    bool in_zone{}, in_combat{}, in_raid{};
    std::uint32_t self_id{}, target_id{}, level{};
    std::int64_t hp{}, max_hp{};
    std::uint32_t mana{}, max_mana{}, endurance{}, max_endurance{};
    std::string cast_name;
};

struct AuraObservation {
    std::string label;
    std::optional<std::uint32_t> spell_icon;
    bool detrimental{};
};

struct ActorGameplay {
    std::optional<std::uint32_t> consider_color;
    ResourceValues resources;
    std::span<const AuraObservation> auras;
};

class Gameplay {
public:
    void refresh(const PluginHostApi* api, const Settings& settings, std::uint64_t now_ms, bool packets_allowed);
    bool receive_points(const PluginHostApi* api, std::uint16_t opcode, std::span<const std::byte> packet);
    bool refresh_identity(const PluginHostApi* api);
    void remove(std::uint32_t id);
    void reset();

    const PlayerGameplay& player() const { return player_; }
    std::uint64_t scope_revision() const { return scope_revision_; }
    // The aura span is borrowed until the next refresh, remove or reset.
    ActorGameplay actor(const PluginHostApi* api, const HailstoneNameplateCandidate& actor, bool need_consider);

private:
    struct AuraSet {
        ResourceActor actor;
        std::vector<AuraObservation> auras;
    };

    bool sync_player(const PluginHostApi* api, HailstonePlayer& player);
    ResourceActor refresh_scope(const PluginHostApi* api, const HailstonePlayer& player, std::uint32_t incoming_id = 0);
    void refresh_auras(const PluginHostApi* api, const Settings& settings);
    std::optional<std::uint32_t> consider(const PluginHostApi* api, const HailstoneNameplateCandidate& actor);

    PlayerGameplay player_;
    std::uint32_t zone_id_{}, zone_instance_{}, casting_spell_{};
    std::string character_;
    bool grouped_{};
    std::uint64_t scope_revision_{};
    std::optional<std::uint64_t> combat_at_, resources_at_, auras_at_;
    ResourceObservations resources_;
    std::array<AuraSet, 2> auras_;
    std::array<std::uint32_t, 512> consider_cache_{};
};

} // namespace crest
