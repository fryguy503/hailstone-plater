// SPDX-License-Identifier: MIT
#include "presentation.hpp"
#include "settings.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <source_location>
#include <string_view>

namespace {

void check(bool condition, std::string_view message, std::source_location location = std::source_location::current())
{
    if (condition) return;
    std::cerr << location.file_name() << ':' << location.line() << ": " << message << '\n';
    std::exit(EXIT_FAILURE);
}

constexpr std::uint32_t con_blue = 0x6ea8ff;

HailstoneNameplateString borrowed(std::string_view value)
{
    return {value.data(), static_cast<std::uint32_t>(value.size())};
}

const HailstoneNameplateCommand* label(const crest::PreparedPlate& plate, std::string_view text, std::uint32_t color)
{
    const auto found = std::find_if(plate.commands.begin(), plate.commands.end(), [=](const auto& command) {
        return command.kind == HAILSTONE_NAMEPLATE_TEXT && command.color == color &&
               std::string_view(command.text.data, command.text.bytes) == text;
    });
    return found == plate.commands.end() ? nullptr : &*found;
}

struct Fixture {
    crest::Settings settings;
    crest::PresentationEngine renderer;
    HailstoneNameplateFrame frame{};
    HailstoneNameplateCandidate actor{};
    crest::PlateObservations observation{};
    crest::CanvasMetrics metrics{
        [](std::string_view text, float size, std::int32_t font) -> std::optional<HailstoneNameplateTextMetrics> {
            if (font != 1) return {};
            const auto advance = static_cast<float>(text.size()) * size / 2;
            return HailstoneNameplateTextMetrics{sizeof(HailstoneNameplateTextMetrics), advance, size, -0.5f, 0, advance, size};
        },
        [](std::uint32_t id) -> std::optional<HailstoneNameplateImage> {
            if (id != 17) return {};
            return HailstoneNameplateImage{sizeof(HailstoneNameplateImage), 100, 0, 0, 1, 1};
        }
    };

    Fixture()
    {
        frame.size = sizeof(frame);
        frame.frame = 12;
        frame.font_size = 16;
        frame.font = 1;
        frame.now_ms = 1000;
        actor.spawn_id = 17;
        actor.incarnation = 1;
        actor.flags = HAILSTONE_NAMEPLATE_NPC | HAILSTONE_NAMEPLATE_HP_KNOWN | HAILSTONE_NAMEPLATE_TARGET;
        actor.native_scale = 1;
        actor.level = 39;
        actor.class_id = 1;
        actor.distance = 34;
        actor.hp_percent = 72;
        actor.spawn_name = borrowed("#An_ice_golem17");
        actor.caption = borrowed("An ice golem");
        actor.cast_name = borrowed("Ice Storm");
        observation.has_target = true;
        observation.con_color = con_blue;
    }

    crest::PreparedPlate draw()
    {
        auto plate = renderer.prepare(frame, actor, settings, observation, metrics);
        check(plate && !plate->hide && !plate->commands.empty(), "expected a visible drawing");
        return std::move(*plate);
    }
};

void wide_target_clearance()
{
    Fixture f;
    check(f.settings.set_text("frame_style", "runic") &&
          f.settings.set_text("target_style", "chevrons") &&
          f.settings.set_text("target_scale", "1") &&
          f.settings.set_text("target_width", "270") &&
          f.settings.set_text("cast_width", "360") &&
          f.settings.set_text("badge_position", "right"), "wide chevron settings");
    f.actor.flags |= HAILSTONE_NAMEPLATE_CASTING;
    f.actor.cast_elapsed_ms = 750;
    f.actor.cast_duration_ms = 2000;
    f.observation.npc_concealment = std::uint8_t{2};
    const auto plate = f.draw();
    float left_nearest = 0, right_nearest = 0, right_farthest = 0;
    std::size_t left_arms = 0, right_arms = 0;
    for (const auto& command : plate.commands) {
        if (command.kind != HAILSTONE_NAMEPLATE_LINE || command.thickness != 2) continue;
        if (command.x0 < 0) {
            const auto inner = std::max(command.x0, command.x1);
            left_nearest = left_arms ? std::max(left_nearest, inner) : inner;
            ++left_arms;
        } else {
            const auto inner = std::min(command.x0, command.x1);
            right_nearest = right_arms ? std::min(right_nearest, inner) : inner;
            right_farthest = std::max({right_farthest, command.x0, command.x1});
            ++right_arms;
        }
    }
    check(left_arms == 4 && right_arms == 4, "two chevrons remain visible on each side");
    check(left_nearest < -195 && right_nearest > 195 && left_nearest == -right_nearest,
          "chevrons clear both ends of the widest bar and the runic frame");
    const auto badge = label(plate, "IVU", 0xc4a6ff);
    check(badge && badge->x > right_farthest + 2, "right badge clears the outer chevron stroke");
    check(plate.bounds.x < left_nearest && plate.bounds.x + plate.bounds.width > badge->x,
          "bounds include decoration and badge");
}

void name_level_and_visibility()
{
    Fixture f;
    check(f.settings.set("con_name_color", true) && f.settings.set("show_class", true) &&
          f.settings.set_text("color_mode", "custom") && f.settings.set_text("level_position", "below"),
          "configure independent name and level");
    auto plate = f.draw();
    const auto name = label(plate, "An ice golem", con_blue);
    const auto level = label(plate, "Lv 39", con_blue);
    const auto health = label(plate, "72%", 0xf2f5fa);
    const auto class_name = label(plate, "Warrior", 0xf2f5fa);
    check(name && level && health && class_name && name->y < health->y && level->y > health->y && class_name->y < health->y,
          "con colors name and level independently, and level can sit under the bar");
    // Move assignment is used by the plugin while collecting its pending frame.
    crest::PreparedPlate moved;
    moved = std::move(plate);
    check(label(moved, "Lv 39", con_blue), "text pointers survive moving the drawing");
    auto model = moved.view(f.frame.frame, 2, -12);
    check(model.commands == moved.commands.data() && model.command_size == sizeof(HailstoneNameplateCommand) &&
          model.frame == f.frame.frame && model.candidate_index == 2 && model.offset_y == -12 &&
          model.flags == HAILSTONE_NAMEPLATE_CLICKABLE, "native view uses the callback claim and copies public flag defaults");

    check(f.settings.set("through_walls", true) && f.settings.set("clickable", false), "toggle model flags");
    f.actor.spawn_name = {};
    f.actor.level = 0;
    auto fallback = f.draw();
    check(label(fallback, "An ice golem", con_blue) && !label(fallback, "Lv 0", con_blue) &&
          fallback.view(f.frame.frame, 0).flags == HAILSTONE_NAMEPLATE_THROUGH_WALLS,
          "use a borrowed caption for empty short names and avoid claiming a known level");
    check(f.settings.set("show_npcs", false), "hide NPCs");
    const auto hidden = f.renderer.prepare(f.frame, f.actor, f.settings, f.observation, f.metrics);
    check(hidden && hidden->view(f.frame.frame, 0).flags == HAILSTONE_NAMEPLATE_HIDE &&
          !hidden->view(f.frame.frame, 0).commands, "visibility claims the stock caption without emitting geometry");
    check(f.settings.set("enabled", false), "disable Crest");
    check(!f.renderer.prepare(f.frame, f.actor, f.settings, f.observation, f.metrics), "disable leaves claims to the host");
}

void textures_and_badges()
{
    Fixture f;
    check(f.settings.set("show_auras", true) && f.settings.set_text("width", "260") &&
          f.settings.set_text("concealment_display", "Name suffix"),
          "enable aura and concealment badges");
    f.observation.npc_concealment = std::uint8_t{3};
    const std::array auras{crest::PlateBadge{"circle", "Root 9s", 0xf38696, 17}};
    f.observation.auras = auras;
    for (const auto frame : {"minimal", "solid", "bevel", "glass", "runic"}) {
        check(f.settings.set_text("frame_style", frame), "switch frame design");
        for (const auto texture : {"flat", "gradient", "gloss", "segments"}) {
            check(f.settings.set_text("texture", texture), "switch texture");
            const auto plate = f.draw();
            check(label(plate, "An ice golem [INV+IVU]", 0xf2f5fa), "exact combined state uses the selected suffix");
            const auto kind_count = [&](std::uint32_t kind) {
                return std::count_if(plate.commands.begin(), plate.commands.end(),
                                     [=](const auto& c) { return c.kind == kind; });
            };
            check(kind_count(HAILSTONE_NAMEPLATE_IMAGE) == 1 && label(plate, "Root 9s", 0xf38696),
                  "emit the exact aura icon and label");
            const std::string_view layout(texture);
            check((kind_count(HAILSTONE_NAMEPLATE_GRADIENT) > 0) == (layout == "gradient" || layout == "gloss"),
                  "emit gradients only for their selected fills");
            if (layout == "segments") check(kind_count(HAILSTONE_NAMEPLATE_RECT_FILLED) >= 6, "render the segmented fill");
        }
    }
    f.observation.npc_concealment.reset();
    f.actor.flags |= HAILSTONE_NAMEPLATE_CONCEALED;
    check(f.settings.set_text("concealment_display", "Parentheses + tags"), "toggle generic concealment");
    const auto generic = f.draw();
    check(label(generic, "(An ice golem)", 0xf2f5fa) && label(generic, "HID", 0xaebccc),
          "unknown concealment keeps the generic badge");
}

} // namespace

int main()
{
    wide_target_clearance();
    name_level_and_visibility();
    textures_and_badges();
}
