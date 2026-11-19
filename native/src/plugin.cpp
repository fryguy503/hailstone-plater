// SPDX-License-Identifier: MIT
#include "hailstone_plugin.h"
#include "server_indicators.hpp"
#include "stacking.hpp"
#include "wire_bytes.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

HAILSTONE_PLUGIN

namespace {
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;
constexpr auto canvas_caps = HAILSTONE_CAP_READ | HAILSTONE_CAP_UI | HAILSTONE_CAP_EXTEND;
constexpr std::size_t max_observed_actors = 1024;
constexpr std::uint32_t dark = 0x111823, white = 0xf2f5fa;
constexpr std::uint32_t hp_color = 0xe5b454, mana_color = 0x4d94ff, cast_color = 0xf4bb55;

enum class ConcealmentDisplay { ParenthesesTags, ParenthesesOnly, Tags, Suffix, Off };
ConcealmentDisplay concealment_display = ConcealmentDisplay::ParenthesesTags;
bool enabled = true, registered = false, packets_allowed = false;
crest::ServerIndicators indicators;
std::unordered_map<std::uint32_t, std::uint64_t> incarnations;

void reset_observations() {
    indicators.reset();
    incarnations.clear();
}

void forget_actor(std::uint32_t id) {
    indicators.remove(id);
    incarnations.erase(id);
}

void observe_actor(const HailstoneNameplateCandidate& actor) {
    if (!packets_allowed || !(actor.flags & HAILSTONE_NAMEPLATE_NPC) || !actor.spawn_id || !actor.incarnation) return;
    const auto previous = incarnations.find(actor.spawn_id);
    if (previous != incarnations.end()) {
        if (previous->second == actor.incarnation) return;
        // A full spawn replacement can reuse the ID without an OnRemoveSpawn event.
        indicators.remove(actor.spawn_id);
        previous->second = actor.incarnation;
        return;
    }
    if (incarnations.size() >= max_observed_actors) reset_observations();
    incarnations.emplace(actor.spawn_id, actor.incarnation);
}

std::uint64_t now_ms() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        Clock::now().time_since_epoch()).count());
}

struct ConcealmentTag {
    std::string_view text;
    std::uint32_t color;
};

ConcealmentTag concealment(const HailstoneNameplateCandidate& candidate, std::uint64_t now) {
    if (concealment_display == ConcealmentDisplay::Off) return {};
    const auto flags = candidate.flags;
    const auto observed = static_cast<std::uint8_t>(((flags & HAILSTONE_NAMEPLATE_INVISIBLE) ? 1 : 0) |
                                                    ((flags & HAILSTONE_NAMEPLATE_INVISIBLE_UNDEAD) ? 2 : 0));
    const auto reported = packets_allowed && candidate.incarnation && (flags & HAILSTONE_NAMEPLATE_NPC)
        ? indicators.concealment(candidate.spawn_id, now) : std::nullopt;
    switch (reported.value_or(observed)) {
    case 1: return {"INV", 0x79def3};
    case 2: return {"IVU", 0xc4a6ff};
    case 3: return {"INV+IVU", 0xffd989};
    default: return (flags & HAILSTONE_NAMEPLATE_CONCEALED) ? ConcealmentTag{"HID", 0xaebccc} : ConcealmentTag{};
    }
}

struct Plate {
    std::array<HailstoneNameplateCommand, 16> commands{};
    std::uint32_t count = 0;
    std::string name, health_text, cast_text;
    HailstoneNameplateBounds bounds{};

    void include(float left, float top, float right, float bottom) {
        const float x = std::min(bounds.x, left), y = std::min(bounds.y, top);
        bounds = {x, y, std::max(bounds.x + bounds.width, right) - x,
                        std::max(bounds.y + bounds.height, bottom) - y};
    }

    void rect(float left, float top, float right, float bottom, std::uint32_t color) {
        if (right <= left || bottom <= top) return;
        auto& cmd = commands[count++];
        cmd.kind = HAILSTONE_NAMEPLATE_RECT_FILLED;
        cmd.color = color;
        cmd.alpha = 1;
        cmd.x0 = left; cmd.y0 = top; cmd.x1 = right; cmd.y1 = bottom;
        include(left - 1, top - 1, right + 1, bottom + 1);
    }

    bool label(std::string_view text, float baseline, float size, int font, std::uint32_t color) {
        if (text.empty() || text.size() > 1024) return false;
        const auto length = static_cast<std::uint32_t>(text.size());
        HailstoneNameplateTextMetrics metrics{};
        metrics.size = sizeof(metrics);
        if (!host->nameplate_measure_text(host->host, text.data(), length, size, font, &metrics)) return false;
        auto& cmd = commands[count++];
        cmd.kind = HAILSTONE_NAMEPLATE_TEXT;
        cmd.color = color;
        cmd.alpha = 1;
        cmd.font_size = size;
        cmd.font = font;
        cmd.x = -metrics.width / 2;
        cmd.y = baseline;
        cmd.text = {text.data(), length};
        include(cmd.x + metrics.ink_x0, cmd.y + metrics.ink_y0,
                cmd.x + metrics.ink_x1, cmd.y + metrics.ink_y1);
        return true;
    }
};

void meter(Plate& plate, float width, float bottom, float height, double fraction, std::uint32_t color) {
    const float left = -width / 2, top = bottom - height;
    const float fill = static_cast<float>(std::clamp(fraction, 0.0, 1.0));
    plate.rect(left, top, width / 2, bottom, dark);
    plate.rect(left + 1, top + 1, left + 1 + (width - 2) * fill, bottom - 1, color);
}

bool prepare(Plate& plate, const HailstoneNameplateCandidate& actor, const HailstoneNameplateFrame& frame,
             std::uint64_t ms) {
    const auto flags = actor.flags;
    const float unit = std::clamp(actor.native_scale * ((flags & HAILSTONE_NAMEPLATE_TARGET) ? 1.15f : 1.f), .4f, 2.f);
    const float font = std::clamp(frame.font_size * unit, 4.f, 96.f);
    const float width = 142.f * unit, small_font = std::max(4.f, font * .75f);
    const auto tag = concealment(actor, ms);
    const auto caption = actor.caption;
    if (!caption.data || !caption.bytes || caption.bytes > 1000) return false;
    plate.name.assign(caption.data, caption.bytes);
    if (!tag.text.empty()) {
        if (concealment_display == ConcealmentDisplay::ParenthesesTags ||
            concealment_display == ConcealmentDisplay::ParenthesesOnly) {
            plate.name.insert(plate.name.begin(), '(');
            plate.name.push_back(')');
        } else if (concealment_display == ConcealmentDisplay::Suffix) {
            plate.name += " [";
            plate.name += tag.text;
            plate.name += ']';
        }
    }
    float bottom = -2.f * unit;
    if ((flags & HAILSTONE_NAMEPLATE_CASTING) && actor.cast_duration_ms > 0) {
        const float height = std::max(9.f * unit, small_font + 2);
        meter(plate, width, bottom, height, actor.cast_elapsed_ms / actor.cast_duration_ms, cast_color);
        if (actor.cast_name.data && actor.cast_name.bytes && actor.cast_name.bytes <= 1024) {
            plate.cast_text.assign(actor.cast_name.data, actor.cast_name.bytes);
            plate.label(plate.cast_text, bottom - height + 1, small_font, frame.font, white);
        }
        bottom -= height + 2.f * unit;
    }
    if (flags & HAILSTONE_NAMEPLATE_MANA_KNOWN) {
        meter(plate, width, bottom, std::max(3.f, 4.f * unit), actor.mana_percent / 100., mana_color);
        bottom -= std::max(3.f, 4.f * unit) + 2.f * unit;
    }
    if (flags & HAILSTONE_NAMEPLATE_HP_KNOWN) {
        const float height = std::max(12.f * unit, small_font + 2);
        meter(plate, width, bottom, height, actor.hp_percent / 100., hp_color);
        char percent[16];
        std::snprintf(percent, sizeof(percent), "%.0f%%", actor.hp_percent);
        plate.health_text = percent;
        plate.label(plate.health_text, bottom - height + 1, small_font, frame.font, white);
        bottom -= height + 2.f * unit;
    }
    bottom -= font;
    if (!plate.label(plate.name, bottom, font, frame.font, white)) return false;
    if (!tag.text.empty() && (concealment_display == ConcealmentDisplay::Tags ||
                             concealment_display == ConcealmentDisplay::ParenthesesTags))
        plate.label(tag.text, bottom - small_font - 2.f * unit, small_font, frame.font, tag.color);
    return true;
}

void canvas(void*, const HailstoneNameplateFrame* frame) {
    if (!enabled || !frame || frame->size < sizeof(*frame) || frame->candidate_count > 1024 ||
        (frame->candidate_count && (!frame->candidates || !HAILSTONE_NAMEPLATE_CANDIDATE_HAS(frame, cast_name)))) return;
    const auto started = Clock::now();
    const auto ms = now_ms();
    std::vector<Plate> plates(frame->candidate_count);
    std::vector<crest::StackPlate> layout(frame->candidate_count);
    std::vector<bool> available(frame->candidate_count, true);
    for (std::uint32_t index = 0; index < frame->candidate_count; ++index) {
        const auto& actor = *hailstone_nameplate_candidate(frame, index);
        observe_actor(actor);
        auto& item = layout[index];
        item.x = actor.x; item.y = actor.y; item.depth = actor.depth; item.id = actor.spawn_id;
        item.target = (actor.flags & HAILSTONE_NAMEPLATE_TARGET) != 0;
        item.hide = (actor.flags & (HAILSTONE_NAMEPLATE_CORPSE | HAILSTONE_NAMEPLATE_SELF)) ||
                    actor.distance > 200 || (!item.target && Clock::now() - started >= 12ms);
        if (item.hide) continue;
        auto& plate = plates[index];
        if (!prepare(plate, actor, *frame, ms)) { available[index] = false; item.hide = true; continue; }
        const auto& b = plate.bounds;
        item.bounds = {b.x, b.y, b.width, b.height};
    }
    const auto& viewport = frame->viewport;
    crest::stack_plates(layout, {viewport.x, viewport.y, viewport.width, viewport.height},
                       [&] { return Clock::now() - started >= 18ms; });
    for (std::uint32_t index = 0; index < frame->candidate_count; ++index) {
        if (!available[index]) continue;
        const auto& item = layout[index];
        const auto& plate = plates[index];
        HailstoneNameplateModel model{};
        model.size = sizeof(model);
        model.frame = frame->frame;
        model.candidate_index = index;
        model.flags = item.hidden || item.hide ? HAILSTONE_NAMEPLATE_HIDE : HAILSTONE_NAMEPLATE_CLICKABLE;
        if (!(model.flags & HAILSTONE_NAMEPLATE_HIDE)) {
            model.bounds = plate.bounds;
            model.offset_y = static_cast<float>(item.offset_y);
            model.command_count = plate.count;
            model.command_size = sizeof(HailstoneNameplateCommand);
            model.commands = plate.commands.data();
        }
        host->nameplate_submit(host->host, &model);
    }
}

void command(void*, const char* argument) {
    const std::string_view value = argument ? argument : "";
    if (value == "on") enabled = true;
    else if (value == "off") enabled = false;
    else if (value == "parens-tags") concealment_display = ConcealmentDisplay::ParenthesesTags;
    else if (value == "parens") concealment_display = ConcealmentDisplay::ParenthesesOnly;
    else if (value == "tags") concealment_display = ConcealmentDisplay::Tags;
    else if (value == "suffix") concealment_display = ConcealmentDisplay::Suffix;
    else if (value == "concealment-off") concealment_display = ConcealmentDisplay::Off;
    else if (host->log) host->log(host->host, 1, "Crest: /crest on|off|parens-tags|parens|tags|suffix|concealment-off");
}

bool nameplate_api_available() {
    // Older hosts report ABI 3 too. Check the entire current table prefix before
    // accessing its nameplate slots.
    return HAILSTONE_HOST_HAS(host, nameplate_spell_icon) && host->caps && host->nameplate_register &&
           host->nameplate_unregister && host->nameplate_submit && host->nameplate_measure_text;
}

void refresh_registration() {
    if (!nameplate_api_available()) return;
    const auto caps = host->caps(host->host);
    if ((caps & canvas_caps) != canvas_caps) registered = false;
    else if (!enabled && registered) {
        host->nameplate_unregister(host->host);
        registered = false;
    } else if (enabled && !registered) {
        registered = host->nameplate_register(host->host, canvas, nullptr) != 0;
    }
    constexpr auto observation_caps = canvas_caps | HAILSTONE_CAP_PACKETS;
    const bool new_packets = (caps & observation_caps) == observation_caps;
    if (!new_packets && packets_allowed) reset_observations();
    packets_allowed = new_packets;
}
} // namespace

HAILSTONE_API void InitializePlugin() {
    if (!nameplate_api_available() && HAILSTONE_HOST_HAS(host, log)) {
        host->log(host->host, 2, "Crest requires a Hailstone build with the merged native nameplate SDK.");
    }
    refresh_registration();
    if (HAILSTONE_HOST_HAS(host, add_command)) AddCommand("/crest", command, nullptr);
}

HAILSTONE_API void ShutdownPlugin() {
    if (registered && HAILSTONE_HOST_HAS(host, nameplate_unregister)) host->nameplate_unregister(host->host);
    registered = false;
    packets_allowed = false;
    reset_observations();
}

HAILSTONE_API void OnPulse() { refresh_registration(); }

HAILSTONE_API void OnBeginZone() { reset_observations(); }

HAILSTONE_API void OnRemoveSpawn(std::uint32_t id) { forget_actor(id); }

HAILSTONE_API int OnPacket(int stream, int direction, HailstonePacket* packet) {
    if (!packets_allowed || stream != HAILSTONE_STREAM_ZONE || direction != HAILSTONE_IN || !packet || !packet->data) return 0;
    const auto bytes = std::as_bytes(std::span{packet->data, packet->size});
    if (packet->opcode == 0x7d1c || packet->opcode == 0x7d1d) indicators.receive(packet->opcode, bytes, now_ms());
    if (packet->opcode == 0x7280 && bytes.size() >= 4) forget_actor(crest::wire::le32(bytes, 0));
    return 0;
}
