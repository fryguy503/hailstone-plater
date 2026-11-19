// SPDX-License-Identifier: MIT
#include "presentation_detail.hpp"
#include "settings.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

namespace crest {
namespace {

constexpr std::array<std::string_view, 16> classes{
    "Warrior", "Cleric", "Paladin", "Ranger", "Shadow Knight", "Druid", "Monk", "Bard",
    "Rogue", "Shaman", "Necromancer", "Wizard", "Magician", "Enchanter", "Beastlord", "Berserker"
};
constexpr std::array<std::uint32_t, 16> class_colors{
    0xc79c6e, 0xffffff, 0xf58cba, 0xabd473, 0xc41f3b, 0xff7d0a, 0x00ff96, 0xe6cc80,
    0xfff569, 0x0070de, 0x9482c9, 0x69ccf0, 0x40c7eb, 0xa78be8, 0x7fbb88, 0xe89569
};

bool has(const HailstoneNameplateCandidate& actor, HailstoneNameplateCandidateFlag flag)
{
    return (actor.flags & flag) != 0;
}

struct Category {
    std::string_view name, visibility_key, color_key;
};

Category category(const HailstoneNameplateCandidate& actor)
{
    if (has(actor, HAILSTONE_NAMEPLATE_SELF)) return {"self", "show_self", "player_color"};
    if (has(actor, HAILSTONE_NAMEPLATE_CORPSE)) return {"corpse", "show_corpses", "corpse_color"};
    if (has(actor, HAILSTONE_NAMEPLATE_MERCENARY)) return {"mercenary", "show_mercenaries", "npc_color"};
    if (actor.owner_id) return {"pet", "show_pets", "pet_color"};
    if (has(actor, HAILSTONE_NAMEPLATE_GROUP)) return {"group", "show_group", "group_color"};
    if (has(actor, HAILSTONE_NAMEPLATE_NPC)) return {"npc", "show_npcs", "npc_color"};
    return {"player", "show_players", "player_color"};
}

std::optional<double> percent(double value)
{
    if (!std::isfinite(value)) return std::nullopt;
    return std::clamp(value, 0.0, 100.0);
}

std::optional<double> percent(std::optional<double> value)
{
    return value ? percent(*value) : std::nullopt;
}

std::optional<double> observed_percent(const HailstoneNameplateCandidate& actor,
                                       HailstoneNameplateCandidateFlag flag, double value,
                                       std::optional<double> fallback = {})
{
    if (has(actor, flag)) {
        if (auto known = percent(value)) return known;
    }
    return percent(fallback);
}

std::string_view borrowed(HailstoneNameplateString text)
{
    return text.data ? std::string_view(text.data, text.bytes) : std::string_view{};
}

std::string clean_name(std::string_view raw)
{
    auto name = detail::clean_text(raw, 128);
    name.erase(0, name.find_first_not_of('#'));
    const auto last = name.find_last_not_of("0123456789");
    name.resize(last == std::string::npos ? 0 : last + 1);
    std::replace(name.begin(), name.end(), '_', ' ');
    return name;
}

std::string lowercase(std::string name)
{
    for (auto& c : name) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + ('a' - 'A'));
    }
    return name;
}

const Rule* match_rule(const Settings& settings, std::string_view kind, const std::string& name)
{
    if (settings.rules().empty()) return nullptr;
    const auto folded = lowercase(name);
    for (const auto& rule : settings.rules()) {
        if (!rule.enabled) continue;
        if (rule.kind != "any" && rule.kind != kind &&
            !(rule.kind == "player" && (kind == "group" || kind == "self"))) continue;
        const auto needle = lowercase(rule.name);
        const bool match = rule.exact ? folded == needle : folded.find(needle) != std::string::npos;
        if (match) return &rule;
    }
    return nullptr;
}

std::uint32_t blend(std::uint32_t a, std::uint32_t b, double amount)
{
    std::uint32_t rgb = 0;
    for (unsigned shift : {0u, 8u, 16u}) {
        const auto x = (a >> shift) & 255u;
        const auto y = (b >> shift) & 255u;
        const auto channel = static_cast<std::uint32_t>(std::floor(x * (1 - amount) + y * amount + 0.5));
        rgb |= channel << shift;
    }
    return rgb;
}

std::string decimal(double value, bool fractional = false)
{
    char buffer[96]{};
    const auto count = std::snprintf(buffer, sizeof(buffer), fractional ? "%.1f" : "%.0f", value);
    if (count < 0 || static_cast<std::size_t>(count) >= sizeof(buffer)) return {};
    return std::string(buffer, count);
}

void add_joined(std::string& result, std::string_view piece)
{
    if (piece.empty()) return;
    if (!result.empty()) result += " | ";
    result.append(piece);
}

void add_badge(detail::PlateStyle& plate, const PlateBadge& badge)
{
    if (plate.badges.size() >= 8) return;
    plate.badges.push_back({badge.icon, detail::clean_text(badge.label, 48), badge.color & 0xffffff,
                            badge.spell_icon, badge.text_only});
}

} // namespace

namespace detail {

std::vector<std::size_t> utf8_offsets(std::string_view input)
{
    std::vector<std::size_t> offsets;
    for (std::size_t at = 0; at < input.size(); ++at) {
        if ((static_cast<unsigned char>(input[at]) & 0xc0) != 0x80) offsets.push_back(at);
    }
    offsets.push_back(input.size());
    return offsets;
}

std::string clean_text(std::string_view input, std::size_t limit)
{
    std::size_t end = std::min(input.size(), limit);
    while (end && end < input.size() && (static_cast<unsigned char>(input[end]) & 0xc0) == 0x80) --end;
    std::string result;
    result.reserve(end);
    for (std::size_t at = 0; at < end;) {
        const auto lead = static_cast<unsigned char>(input[at]);
        if (lead < 128) {
            result += lead < 32 || lead == 127 ? ' ' : input[at];
            ++at;
            continue;
        }
        unsigned count = 0;
        if (lead >= 0xc2 && lead <= 0xdf) count = 2;
        else if (lead >= 0xe0 && lead <= 0xef) count = 3;
        else if (lead >= 0xf0 && lead <= 0xf4) count = 4;
        if (count == 0 || at + count > end) return {};
        const auto second = static_cast<unsigned char>(input[at + 1]);
        if ((lead == 0xe0 && second < 0xa0) || (lead == 0xed && second >= 0xa0) ||
            (lead == 0xf0 && second < 0x90) || (lead == 0xf4 && second >= 0x90)) return {};
        for (unsigned index = 1; index < count; ++index) {
            if ((static_cast<unsigned char>(input[at + index]) & 0xc0) != 0x80) return {};
        }
        result.append(input.substr(at, count));
        at += count;
    }
    return result;
}

PreparedPlate hidden_plate()
{
    PreparedPlate out;
    out.hide = true;
    out.stack = false;
    out.clickable = false;
    return out;
}

} // namespace detail

HailstoneNameplateModel PreparedPlate::view(std::uint64_t frame, std::uint32_t index,
                                           float stack_offset_y, bool stack_hidden) const
{
    HailstoneNameplateModel model{};
    model.size = sizeof(model);
    model.frame = frame;
    model.candidate_index = index;
    model.command_size = sizeof(HailstoneNameplateCommand);
    if (hide || stack_hidden) {
        model.flags = HAILSTONE_NAMEPLATE_HIDE;
        return model;
    }
    if (clickable) model.flags |= HAILSTONE_NAMEPLATE_CLICKABLE;
    if (through_walls) model.flags |= HAILSTONE_NAMEPLATE_THROUGH_WALLS;
    model.command_count = static_cast<std::uint32_t>(commands.size());
    model.commands = commands.data();
    model.bounds = bounds;
    model.offset_y = stack_offset_y;
    return model;
}

void PresentationEngine::reset()
{
    actors_.clear();
    last_prune_ = 0;
}

void PresentationEngine::remove(std::uint32_t spawn_id)
{
    actors_.erase(spawn_id);
}

double PresentationEngine::smooth(const HailstoneNameplateCandidate& actor, std::size_t channel,
                                  double value, double now, double tau)
{
    auto entry = actors_.find(actor.spawn_id);
    if (entry == actors_.end()) {
        if (actors_.size() >= 1024) actors_.clear();
        entry = actors_.try_emplace(actor.spawn_id).first;
    }
    auto& actor_state = entry->second;
    if (actor_state.incarnation != actor.incarnation || now < actor_state.seen) {
        actor_state = {};
        actor_state.incarnation = actor.incarnation;
    }
    auto& reading = actor_state.bars[channel];
    const auto age = now - reading.at;
    if (reading.value && age <= 1000 && tau > 0 && value > 0) {
        value = *reading.value + (value - *reading.value) * (1 - std::exp(-std::max(0.0, age) / tau));
    }
    reading = {value, now};
    actor_state.seen = now;
    return value;
}

std::optional<PreparedPlate> PresentationEngine::prepare(const HailstoneNameplateFrame& frame,
    const HailstoneNameplateCandidate& actor, const Settings& settings, const PlateObservations& extra,
    const CanvasMetrics& metrics)
{
    if (!settings.boolean("enabled")) return std::nullopt;
    const auto now = std::isfinite(frame.now_ms) ? frame.now_ms : 0.0;
    if (now < last_prune_ || now - last_prune_ >= 2000) {
        std::erase_if(actors_, [now](const auto& pair) { return now < pair.second.seen || now - pair.second.seen > 5000; });
        last_prune_ = now;
    }
    const auto kind = category(actor);
    const auto hp = observed_percent(actor, HAILSTONE_NAMEPLATE_HP_KNOWN, actor.hp_percent);
    const auto target = has(actor, HAILSTONE_NAMEPLATE_TARGET);
    const auto self = has(actor, HAILSTONE_NAMEPLATE_SELF);
    const auto corpse = has(actor, HAILSTONE_NAMEPLATE_CORPSE);
    const auto distance = std::isfinite(actor.distance) ? actor.distance : 0.0;
    const auto max_distance = settings.number("max_distance");
    if (!settings.boolean(kind.visibility_key) || distance > max_distance ||
        (settings.boolean("target_only") && !target) || (settings.boolean("combat_only") && !extra.combat) ||
        (settings.boolean("injured_only") && !target && hp && *hp >= 100)) return detail::hidden_plate();

    const auto name = clean_name(borrowed(actor.spawn_name));
    const auto* rule = match_rule(settings, kind.name, name);
    if (rule && rule->hide) return detail::hidden_plate();

    const auto unit_prefix = std::string(kind.name) + "_";
    const auto custom_unit = settings.boolean(unit_prefix + "override");
    const auto unit_num = [&](const char* key) {
        if (custom_unit) return static_cast<float>(settings.number(unit_prefix + key));
        return static_cast<float>(settings.number(key));
    };
    const auto unit_text = [&](const char* key) {
        if (custom_unit) return settings.text(unit_prefix + key);
        return settings.text(key);
    };
    const auto num = [&](std::string_view key) { return static_cast<float>(settings.number(key)); };
    const auto color = [&](std::string_view key) { return static_cast<std::uint32_t>(settings.number(key)); };
    const auto setting_style = settings.text("color_mode");
    const bool con_name = settings.boolean("con_name_color") && !corpse;
    const auto class_index = static_cast<std::size_t>(actor.class_id) - 1;
    auto fill = color(kind.color_key);
    if (setting_style == "class" && class_index < class_colors.size()) fill = class_colors[class_index];
    else if (setting_style == "health" && hp) {
        fill = *hp < 50 ? blend(0xf15260, 0xecc55a, *hp / 50) : blend(0xecc55a, 0x59d9a0, (*hp - 50) / 50);
    } else if (setting_style == "consider" && kind.name == "npc" && extra.con_color) fill = *extra.con_color;
    else if (setting_style == "custom") fill = color("npc_color");
    const auto low = hp && *hp <= settings.number("low_health");
    if (low && settings.boolean("low_health_color")) fill = color("low_color");
    if (rule) fill = rule->color;
    else if (custom_unit) {
        const auto override_color = settings.number(unit_prefix + "custom_color");
        if (override_color >= 0) fill = static_cast<std::uint32_t>(override_color);
    }

    detail::PlateStyle plate;
    const auto caption = detail::clean_text(borrowed(actor.caption), 256);
    plate.name = caption;
    if (name.size() && (settings.boolean("short_names") || caption.empty())) plate.name = name;
    if (plate.name.size() > settings.number("max_name")) {
        const auto offsets = detail::utf8_offsets(plate.name);
        const auto maximum = static_cast<std::size_t>(settings.number("max_name"));
        if (maximum + 1 < offsets.size()) {
            plate.name.resize(offsets[maximum]);
            plate.name = detail::clean_text(plate.name + "...", 256);
        }
    }
    plate.show_name = settings.boolean("show_name");
    plate.text_color = color("text_color");
    plate.name_color = con_name && extra.con_color ? *extra.con_color : plate.text_color;
    plate.level_color = plate.name_color;
    if (actor.level && settings.boolean("show_level")) plate.level = "Lv " + std::to_string(actor.level);
    if (settings.boolean("show_class") && class_index < classes.size()) add_joined(plate.subtitle, classes[class_index]);
    if (settings.boolean("show_distance")) add_joined(plate.subtitle, decimal(distance) + "u");

    const auto target_width = num("target_width");
    const auto combat_width = num(extra.combat ? "combat_width" : "rest_width");
    const auto combat_height = num(extra.combat ? "combat_height" : "rest_height");
    plate.width = unit_num("width");
    if (combat_width > 0) plate.width = std::max(40.0f, combat_width);
    if (target && target_width > 0) plate.width = std::max(40.0f, target_width);
    plate.height = combat_height > 0 ? std::max(3.0f, combat_height) : unit_num("height");
    plate.mana_height = unit_num("mana_height");
    plate.cast_height = unit_num("cast_height");
    plate.cast_width = num("cast_width");
    plate.cast_gap = num("cast_gap");
    plate.font_scale = unit_num("font_scale");
    const auto combat_scale = num(extra.combat ? "combat_scale" : "rest_scale");
    const auto effective_scale = std::clamp(unit_num("scale") * combat_scale, 0.5f, 2.0f);
    plate.scale = std::clamp(effective_scale * (target ? num("target_scale") : 1.0f) *
                            (rule ? static_cast<float>(rule->scale) : 1.0f), 0.5f, 2.0f);
    plate.alpha = unit_num("alpha");
    if (!extra.combat) plate.alpha *= num("rest_alpha");
    if (!target && extra.has_target) plate.alpha *= num("non_target_alpha");
    if (!target && settings.boolean("distance_fade") && max_distance > 0) {
        plate.alpha *= static_cast<float>(1 - 0.65 * std::max(0.0, (distance / max_distance - 0.65) / 0.35));
    }
    plate.alpha = std::clamp(plate.alpha, 0.05f, 1.0f);
    plate.offset_y = unit_num("offset_y");
    plate.border = color(target ? "target_color" : "border_color");
    if (low && settings.boolean("low_health_pulse")) {
        plate.border = blend(color("border_color"), color("low_color"), 0.5 + 0.5 * std::sin(now / 180));
    }
    plate.background = color("background_color");
    plate.frame_style = unit_text("frame_style");
    plate.texture = unit_text("texture");
    plate.name_position = unit_text("name_position");
    plate.badge_position = unit_text("badge_position");
    plate.level_position = settings.text("level_position");
    plate.target_style = settings.text("target_style");
    plate.text_align = settings.text("text_align");
    plate.health_text_align = settings.text("health_text_align");
    plate.cast_text_align = settings.text("cast_text_align");
    plate.border_width = num("border_width");
    plate.rounding = num("rounding");
    plate.badge_scale = num("badge_scale");
    plate.show_marker = target && settings.boolean("target_marker");
    plate.stack = settings.boolean("stack");
    plate.stack_gap = num("stack_gap");
    plate.max_stack = num("max_stack");
    plate.through_walls = settings.boolean("through_walls");
    plate.clickable = settings.boolean("clickable");

    const bool names_only = (custom_unit && settings.boolean(unit_prefix + "names_only")) ||
        (settings.boolean("friendly_names_only") && (kind.name == "player" || kind.name == "group"));
    const auto smoothing = settings.number("smoothing_ms");
    if (!names_only && !corpse) {
        if (hp && settings.boolean("show_health")) {
            std::string hp_text;
            const auto mode = settings.text("health_text");
            if (mode != "none") {
                hp_text = decimal(*hp) + "%";
                if (self && mode != "percent" && extra.current_hp && extra.max_hp &&
                    std::isfinite(*extra.current_hp) && std::isfinite(*extra.max_hp)) {
                    auto absolute = decimal(*extra.current_hp) + " / " + decimal(*extra.max_hp);
                    if (mode == "both") absolute += "  " + hp_text;
                    hp_text = std::move(absolute);
                }
            }
            plate.health = detail::Bar{smooth(actor, 0, *hp, now, smoothing), fill, std::move(hp_text)};
        }
        if ((!custom_unit || settings.boolean(unit_prefix + "resource")) &&
            (target || !settings.boolean("resource_target_only"))) {
            const auto resource = settings.text("resource");
            const bool mana = resource == "mana";
            std::optional<double> reading;
            if (mana) reading = observed_percent(actor, HAILSTONE_NAMEPLATE_MANA_KNOWN, actor.mana_percent, extra.remote_mana);
            else if (resource == "endurance") {
                reading = observed_percent(actor, HAILSTONE_NAMEPLATE_ENDURANCE_KNOWN, actor.endurance_percent, extra.remote_endurance);
            }
            if (reading) {
                auto text = settings.boolean("resource_text") ? decimal(*reading) + "%" : std::string{};
                plate.resource = detail::Bar{smooth(actor, mana ? 1 : 2, *reading, now, smoothing),
                                             color(mana ? "mana_color" : "endurance_color"), std::move(text)};
            }
        }
        if (settings.boolean("show_cast") && (!custom_unit || settings.boolean(unit_prefix + "cast")) &&
            (target || !settings.boolean("cast_target_only")) && has(actor, HAILSTONE_NAMEPLATE_CASTING) &&
            std::isfinite(actor.cast_duration_ms) && std::isfinite(actor.cast_elapsed_ms) &&
            actor.cast_elapsed_ms >= 0 && actor.cast_elapsed_ms < actor.cast_duration_ms) {
            auto text = settings.boolean("cast_name") ? detail::clean_text(borrowed(actor.cast_name), 80) : std::string{};
            if (settings.boolean("cast_time")) {
                if (!text.empty()) text += " ";
                text += decimal((actor.cast_duration_ms - actor.cast_elapsed_ms) / 1000, true) + "s";
            }
            const auto progress = 100 * (actor.cast_elapsed_ms / actor.cast_duration_ms);
            const auto cast_color = color("cast_color");
            plate.cast = detail::Bar{settings.boolean("cast_reverse") ? 100 - progress : progress, cast_color, std::move(text)};
            if (!target && settings.boolean("highlight_cast")) plate.border = cast_color;
        }
    }

    const auto concealment = settings.text("concealment_display");
    if (concealment != "Off" && !corpse) {
        constexpr std::array<std::string_view, 4> tags{"INV", "IVU", "INV+IVU", "HID"};
        constexpr std::array<std::uint32_t, 4> colors{0x79def3, 0xc4a6ff, 0xffd989, 0xaebccc};
        const auto actor_flags = (has(actor, HAILSTONE_NAMEPLATE_INVISIBLE) ? 1u : 0u) |
                                 (has(actor, HAILSTONE_NAMEPLATE_INVISIBLE_UNDEAD) ? 2u : 0u);
        auto state = extra.npc_concealment ? static_cast<unsigned>(*extra.npc_concealment) : actor_flags;
        if ((state == 0 || state > 3) && has(actor, HAILSTONE_NAMEPLATE_CONCEALED)) state = 4;
        if (state > 0 && state <= tags.size()) {
            const PlateBadge badge{{}, tags[state - 1], colors[state - 1], {}, true};
            if (concealment == "Parentheses only" || concealment == "Parentheses + tags") {
                plate.name_prefix = "(";
                plate.name_suffix = ")";
            }
            if (concealment == "Name suffix" && plate.show_name && !plate.name.empty()) {
                plate.name_suffix = " [" + std::string(badge.label) + "]";
                plate.name_fallback_badge = detail::Badge{{}, std::string(badge.label), badge.color, {}, true};
            } else if (concealment != "Parentheses only") add_badge(plate, badge);
        }
    }
    for (const auto& badge : extra.server_indicators) {
        if (plate.badges.size() >= 8) break;
        auto key = "server_indicators_marks";
        if (badge.icon == "eye") key = "server_indicators_awareness";
        else if (badge.icon == "peace" || badge.icon == "sleep") key = "server_indicators_status";
        if (settings.boolean(key)) add_badge(plate, badge);
    }
    if (settings.boolean("stock_marks")) {
        if (actor.raid_mark) add_badge(plate, {"circle", "R" + std::to_string(actor.raid_mark), 0xffb974, {}, false});
        if (actor.group_mark) add_badge(plate, {"circle", "G" + std::to_string(actor.group_mark), 0xffe073, {}, false});
    }
    if (auto aggro = observed_percent(actor, HAILSTONE_NAMEPLATE_AGGRO_KNOWN, actor.aggro_percent)) {
        if (settings.boolean("show_aggro"))
            add_badge(plate, {"sword", decimal(*aggro) + "%", color("aggro_color"), {}, false});
        if (!target && settings.boolean("highlight_aggro") && *aggro >= settings.number("aggro_threshold")) {
            plate.border = color("aggro_color");
        }
    }
    if (settings.boolean("show_auras")) {
        const auto take = std::min(extra.auras.size(), static_cast<std::size_t>(settings.number("max_auras")));
        for (const auto& badge : extra.auras.first(take)) add_badge(plate, badge);
    }
    return detail::render(plate, frame, actor, metrics);
}

} // namespace crest
