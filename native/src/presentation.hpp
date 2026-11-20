// SPDX-License-Identifier: MIT
#pragma once

#include "hailstone_nameplates.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace crest {

class Settings;

struct PlateBadge {
    std::string_view icon;
    std::string_view label;
    std::uint32_t color = 0xffffff;
    std::optional<std::uint32_t> spell_icon;
    bool text_only = false;
};

struct PlateObservations {
    bool combat = false, has_target = false;
    std::optional<double> current_hp, max_hp, remote_mana, remote_endurance;
    std::optional<std::uint32_t> con_color;
    std::optional<std::uint8_t> npc_concealment;
    std::string_view guild_caption;
    std::span<const PlateBadge> server_indicators, auras;
};

struct CanvasMetrics {
    std::function<std::optional<HailstoneNameplateTextMetrics>(std::string_view, float, std::int32_t)> measure;
    std::function<std::optional<HailstoneNameplateImage>(std::uint32_t)> icon;
};

// A present result claims its caption. A hidden result claims no geometry. An
// absent result leaves the ordinary caption eligible to draw.
struct PreparedPlate {
    PreparedPlate() = default;
    PreparedPlate(const PreparedPlate&) = delete;
    PreparedPlate& operator=(const PreparedPlate&) = delete;
    PreparedPlate(PreparedPlate&&) = default;
    PreparedPlate& operator=(PreparedPlate&&) = default;

    HailstoneNameplateBounds bounds{};
    std::vector<HailstoneNameplateCommand> commands;
    std::deque<std::string> text_storage;
    std::size_t vertex_cost = 0;
    bool hide = false, stack = true, clickable = true, through_walls = false;
    float stack_gap = 4, max_stack = 96;

    HailstoneNameplateModel view(std::uint64_t frame, std::uint32_t index, float stack_offset_y = 0,
                                bool stack_hidden = false) const;
};

class PresentationEngine {
public:
    std::optional<PreparedPlate> prepare(const HailstoneNameplateFrame&, const HailstoneNameplateCandidate&,
                                         const Settings&, const PlateObservations&, const CanvasMetrics&);
    void reset();
    void remove(std::uint32_t spawn_id);

private:
    struct Reading { std::optional<double> value; double at = 0; };
    struct ActorState {
        std::uint64_t incarnation = 0;
        double seen = 0;
        std::array<Reading, 3> bars;
    };
    std::unordered_map<std::uint32_t, ActorState> actors_;
    double last_prune_ = 0;

    double smooth(const HailstoneNameplateCandidate&, std::size_t channel, double value, double now, double tau);
};

} // namespace crest
