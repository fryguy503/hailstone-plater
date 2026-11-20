// SPDX-License-Identifier: MIT
#pragma once

#include "presentation.hpp"

namespace crest::detail {

struct Bar {
    double value = 0;
    std::uint32_t color = 0xffffff;
    std::string text;
};

struct Badge {
    std::string_view icon;
    std::string text;
    std::uint32_t color = 0xffffff;
    std::optional<std::uint32_t> spell_icon;
    bool text_only = false;
};

struct PlateStyle {
    std::string name, secondary_caption, level, subtitle, name_suffix;
    std::string_view name_prefix;
    std::uint32_t name_color = 0xffffff, level_color = 0xffffff, text_color = 0xffffff;
    std::uint32_t border = 0, background = 0;
    float width = 142, height = 12, mana_height = 4, cast_height = 9, cast_gap = 2, cast_width = 0;
    float font_scale = 1, scale = 1, alpha = 1, offset_y = 0, border_width = 1, rounding = 2, badge_scale = 1;
    float stack_gap = 4, max_stack = 96;
    std::string_view frame_style, texture, target_style, name_position, level_position, badge_position;
    std::string_view text_align, health_text_align, cast_text_align;
    bool show_name = true, show_marker = false, stack = false, through_walls = false, clickable = true;
    std::optional<Bar> health, resource, cast;
    std::vector<Badge> badges;
    std::optional<Badge> name_fallback_badge;
};

std::optional<PreparedPlate> render(const PlateStyle&, const HailstoneNameplateFrame&,
                                    const HailstoneNameplateCandidate&, const CanvasMetrics&);
std::vector<std::size_t> utf8_offsets(std::string_view);
std::string clean_text(std::string_view, std::size_t);
PreparedPlate hidden_plate();

} // namespace crest::detail
