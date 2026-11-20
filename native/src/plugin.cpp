// SPDX-License-Identifier: MIT
#include "hailstone_plugin.h"
#include "gameplay.hpp"
#include "presentation.hpp"
#include "server_indicators.hpp"
#include "settings.hpp"
#include "stacking.hpp"
#include "wire_bytes.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <span>
#include <string_view>
#include <unordered_map>
#include <vector>

HAILSTONE_PLUGIN

namespace {
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;
constexpr auto canvas_caps = HAILSTONE_CAP_READ | HAILSTONE_CAP_UI | HAILSTONE_CAP_EXTEND;
constexpr std::size_t max_observed_actors = 1024;
constexpr std::size_t max_frame_commands = 32768, max_frame_vertices = 262144;
// Hailstone appends guild_caption after cast_name in ABI 3. Read it by stride
// so builds against the 0.7.2 SDK accept both old and extended candidates.
constexpr auto guild_caption_offset = offsetof(HailstoneNameplateCandidate, cast_name) + sizeof(HailstoneNameplateString);

crest::SettingsStore store;
crest::Gameplay gameplay;
crest::PresentationEngine presentation;
crest::ServerIndicators indicators;
std::unordered_map<std::uint32_t, std::uint64_t> incarnations;

bool registered = false, packets_allowed = false, ui_registered = false;
bool settings_open = false, storage_ready = true, server_data = false, stopped = false;
std::uint64_t revision = 0, indicator_scope = 0;
Clock::time_point next_character_poll{}, next_gameplay_poll{}, save_at{};

std::uint64_t now_ms() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        Clock::now().time_since_epoch()).count());
}

bool nameplate_api_available() {
    return HAILSTONE_HOST_HAS(host, nameplate_spell_icon) && host->caps && host->nameplate_register &&
           host->nameplate_unregister && host->nameplate_submit && host->nameplate_measure_text;
}

void reply(std::string_view message) {
    const auto text = std::string("[Crest] ") + std::string(message);
    if (HAILSTONE_HOST_HAS(host, write_chat)) host->write_chat(host->host, text.c_str(), 0);
    else if (HAILSTONE_HOST_HAS(host, log)) host->log(host->host, 1, text.c_str());
}

void reset_indicators() {
    indicators.reset();
    incarnations.clear();
    server_data = false;
}

void reset_world() {
    reset_indicators();
    gameplay.reset();
    indicator_scope = gameplay.scope_revision();
    next_gameplay_poll = {};
    presentation.reset();
}

void sync_indicator_scope() {
    if (indicator_scope == gameplay.scope_revision()) return;
    reset_indicators();
    indicator_scope = gameplay.scope_revision();
}

void forget_actor(std::uint32_t id) {
    indicators.remove(id);
    incarnations.erase(id);
    gameplay.remove(id);
    presentation.remove(id);
}

void observe_actor(const HailstoneNameplateCandidate& actor) {
    if (!packets_allowed || !actor.spawn_id || !actor.incarnation) return;
    const auto previous = incarnations.find(actor.spawn_id);
    if (previous != incarnations.end()) {
        if (previous->second == actor.incarnation) return;
        indicators.remove(actor.spawn_id);
        previous->second = actor.incarnation;
        return;
    }
    if (incarnations.size() >= max_observed_actors) reset_indicators();
    incarnations.emplace(actor.spawn_id, actor.incarnation);
}

std::filesystem::path host_directory(int (*method)(void*, char*, std::size_t)) {
    std::array<char, 4096> dir{};
    const int length = method(host->host, dir.data(), dir.size());
    if (length <= 0 || static_cast<std::size_t>(length) >= dir.size()) return {};
    return std::filesystem::path(dir.data());
}

std::filesystem::path legacy_settings_path(std::string_view character) {
    if (!HAILSTONE_HOST_HAS(host, server_info) || !HAILSTONE_HOST_HAS(host, config_dir)) return {};
    HailstoneServer server{};
    server.size = sizeof(server);
    if (!host->server_info(host->host, &server)) return {};
    std::string_view identity = server.world_host;
    if (identity.empty()) identity = server.host;
    if (identity.empty()) identity = server.name;
    const std::string_view world = server.world[0] ? server.world : server.name;
    const auto base = host_directory(host->config_dir);
    if (base.empty()) return {};
    // The managed Lua package's private mq.configDir is scripts/config/crest.
    return base.parent_path().parent_path() / "scripts" / "config" / "crest" / "crest" / "characters" /
           (crest::hex_encode(identity) + '_' + crest::hex_encode(world)) /
           (crest::hex_encode(character) + ".lua");
}

void refresh_character(Clock::time_point now) {
    if (now < next_character_poll) return;
    next_character_poll = now + 250ms;
    HailstonePlayer player{};
    player.size = sizeof(player);
    std::filesystem::path next_path;
    if (HAILSTONE_HOST_HAS(host, get_player) && HAILSTONE_HOST_HAS(host, character_config_dir) &&
        HAILSTONE_HOST_HAS(host, caps) &&
        (host->caps(host->host) & HAILSTONE_CAP_READ) && host->get_player(host->host, &player) &&
        player.in_zone && player.game_state == GAMESTATE_INGAME && player.name[0]) {
        const auto dir = host_directory(host->character_config_dir);
        if (!dir.empty()) next_path = dir / "crest-settings.txt";
    }
    if (next_path == store.file) {
        storage_ready = true;
        return;
    }
    if (store.changed && !store.file.empty() && !store.save()) {
        storage_ready = false;
        return;
    }
    const auto legacy_path = next_path.empty() ? std::filesystem::path{} : legacy_settings_path(player.name);
    store.open(std::move(next_path), legacy_path);
    storage_ready = true;
    save_at = now + 1s;
    ++revision;
    reset_world();
}

void changed(bool reset_presentation = true) {
    store.changed = !store.file.empty();
    if (reset_presentation) presentation.reset();
    save_at = Clock::now() + 1s;
    ++revision;
}

std::span<const crest::PlateBadge> server_badges(std::array<crest::PlateBadge, 4>& output,
                                                const HailstoneNameplateCandidate& actor, std::uint64_t ms) {
    const auto& s = store.data.settings;
    if (!packets_allowed || !actor.incarnation ||
        !(s.boolean("server_indicators_marks") || s.boolean("server_indicators_status") ||
          s.boolean("server_indicators_awareness"))) return {};
    const auto state = indicators.indicators(actor.spawn_id, ms, gameplay.player().in_raid);
    static constexpr std::array mark_icons{"star", "sun", "moon", "diamond", "triangle", "cross", "skull", "sword"};
    static constexpr std::array mark_labels{"Star", "Sun", "Moon", "Diamond", "Triangle", "Cross", "Skull", "Sword"};
    static constexpr std::array<std::uint32_t, 8> mark_colors{
        0xffd250, 0xffa845, 0xc4a6ff, 0x65e1ff, 0x66df91, 0xff7575, 0xf1edf8, 0xe2e8ef};
    std::size_t count = 0;
    if (s.boolean("server_indicators_marks") && state.mark && state.mark <= mark_icons.size()) {
        const std::size_t index = state.mark - 1;
        output[count++] = {mark_icons[index], mark_labels[index], mark_colors[index], {}, false};
    }
    if (s.boolean("server_indicators_awareness") && state.awareness)
        output[count++] = {"eye", state.awareness == 2 ? "Engaged" : "Detects you", 0xffc857, {}, false};
    if (s.boolean("server_indicators_status")) {
        if (state.status & 1) output[count++] = {"sleep", "Mezzed", 0xd89bff, {}, false};
        if (state.status & 2) output[count++] = {"peace", "Pacified", 0x74deff, {}, false};
    }
    return std::span{output}.first(count);
}

void canvas(void*, const HailstoneNameplateFrame* frame) {
    const auto& settings = store.data.settings;
    if (!storage_ready || stopped || !settings.boolean("enabled") || !frame || frame->size < sizeof(*frame) ||
        frame->candidate_count > max_observed_actors ||
        (frame->candidate_count && (!frame->candidates || !HAILSTONE_NAMEPLATE_CANDIDATE_HAS(frame, cast_name)))) return;

    const auto started = Clock::now();
    const auto ms = now_ms();
    const auto& player = gameplay.player();
    const bool need_consider = settings.boolean("con_name_color") || settings.text("color_mode") == "consider";
    const bool exact_concealment = packets_allowed && settings.text("concealment_display") != "Off";
    const bool has_guild_caption = frame->candidate_size >= guild_caption_offset + sizeof(HailstoneNameplateString);
    const crest::CanvasMetrics metrics{
        [](std::string_view text, float size, std::int32_t font) -> std::optional<HailstoneNameplateTextMetrics> {
            HailstoneNameplateTextMetrics result{};
            result.size = sizeof(result);
            if (!host->nameplate_measure_text(host->host, text.data(), static_cast<std::uint32_t>(text.size()),
                                              size, font, &result)) return {};
            return result;
        },
        [](std::uint32_t id) -> std::optional<HailstoneNameplateImage> {
            HailstoneNameplateImage result{};
            result.size = sizeof(result);
            if (!host->nameplate_spell_icon(host->host, id, &result)) return {};
            return result;
        },
    };
    std::vector<std::optional<crest::PreparedPlate>> plates(frame->candidate_count);
    std::vector<crest::StackPlate> layout(frame->candidate_count);
    std::size_t remaining = max_frame_commands, vertices = max_frame_vertices;
    for (std::uint32_t index = 0; index < frame->candidate_count; ++index) {
        const auto& actor = *hailstone_nameplate_candidate(frame, index);
        auto& item = layout[index];
        item.x = actor.x; item.y = actor.y; item.depth = actor.depth; item.id = actor.spawn_id;
        item.target = (actor.flags & HAILSTONE_NAMEPLATE_TARGET) != 0;
        observe_actor(actor);
        if (!item.target && (Clock::now() - started >= 12ms || !remaining || !vertices)) {
            item.hide = true;
            plates[index].emplace().hide = true;
            continue;
        }

        const auto source = gameplay.actor(host, actor, need_consider);
        crest::PlateObservations observation;
        observation.combat = player.in_combat;
        observation.has_target = player.target_id != 0 || item.target;
        if (player.max_hp > 0) {
            observation.current_hp = static_cast<double>(player.hp);
            observation.max_hp = static_cast<double>(player.max_hp);
        }
        observation.remote_mana = source.resources.mana_percent;
        observation.remote_endurance = source.resources.endurance_percent;
        observation.con_color = source.consider_color;
        if (has_guild_caption && !(actor.flags & (HAILSTONE_NAMEPLATE_NPC | HAILSTONE_NAMEPLATE_CORPSE))) {
            const auto* record = static_cast<const std::byte*>(frame->candidates) +
                                 static_cast<std::size_t>(index) * frame->candidate_size;
            HailstoneNameplateString guild{};
            std::memcpy(&guild, record + guild_caption_offset, sizeof(guild));
            if (guild.data) observation.guild_caption = {guild.data, guild.bytes};
        }
        const auto concealment_npc = (actor.flags & (HAILSTONE_NAMEPLATE_NPC | HAILSTONE_NAMEPLATE_CORPSE)) ==
                                      HAILSTONE_NAMEPLATE_NPC;
        if (exact_concealment && actor.incarnation && concealment_npc)
            observation.npc_concealment = indicators.concealment(actor.spawn_id, ms);

        std::array<crest::PlateBadge, 4> badges{};
        observation.server_indicators = server_badges(badges, actor, ms);
        std::array<crest::PlateBadge, 5> aura_badges{};
        const auto aura_count = std::min(source.auras.size(), aura_badges.size());
        for (std::size_t i = 0; i < aura_count; ++i) {
            const auto& aura = source.auras[i];
            aura_badges[i] = {"circle", aura.label, aura.detrimental ? 0xf38696u : 0x83dca2u, aura.spell_icon, false};
        }
        observation.auras = std::span{aura_badges}.first(aura_count);

        auto& plate = plates[index];
        plate = presentation.prepare(*frame, actor, settings, observation, metrics);
        if (!plate || plate->hide || plate->commands.size() > remaining || plate->vertex_cost > vertices) {
            item.hide = true;
            continue;
        }
        remaining -= plate->commands.size();
        vertices -= plate->vertex_cost;
        const auto& b = plate->bounds;
        item.bounds = {b.x, b.y, b.width, b.height};
        item.stack = plate->stack;
        item.gap = plate->stack_gap;
        item.max_offset = plate->max_stack;
    }

    const auto& viewport = frame->viewport;
    crest::stack_plates(layout, {viewport.x, viewport.y, viewport.width, viewport.height},
                       [&] { return Clock::now() - started >= 18ms; });
    for (std::uint32_t index = 0; index < frame->candidate_count; ++index) {
        const auto& item = layout[index];
        const auto& plate = plates[index];
        if (!plate) continue;
        auto model = plate->view(frame->frame, index, static_cast<float>(item.offset_y), item.hidden || item.hide);
        host->nameplate_submit(host->host, &model);
    }
}

void refresh_registration() {
    if (!nameplate_api_available()) return;
    const auto caps = host->caps(host->host);
    const bool wanted = storage_ready && !stopped && store.data.settings.boolean("enabled");
    if ((caps & canvas_caps) != canvas_caps) registered = false;
    else if (!wanted && registered) {
        host->nameplate_unregister(host->host);
        registered = false;
    } else if (wanted && !registered) {
        registered = host->nameplate_register(host->host, canvas, nullptr) != 0;
    }
    constexpr auto observation_caps = canvas_caps | HAILSTONE_CAP_PACKETS;
    const bool new_packets = (caps & observation_caps) == observation_caps;
    if (!new_packets && packets_allowed) reset_indicators();
    packets_allowed = new_packets;
}

void set_enabled(bool value) {
    if (!storage_ready) return;
    stopped = false;
    store.data.settings.set("enabled", value);
    changed();
}

void command(void*, const char* argument) {
    std::string_view value = argument ? argument : "";
    const auto first = value.find_first_not_of(" \t");
    value = first == value.npos ? std::string_view{} : value.substr(first, value.find_last_not_of(" \t") - first + 1);
    std::string lower(value);
    for (char& c : lower) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    value = lower;
    auto& settings = store.data.settings;
    static constexpr std::array display_modes{
        std::pair{"parens-tags", "Parentheses + tags"}, std::pair{"parens", "Parentheses only"},
        std::pair{"tags", "Tags"}, std::pair{"suffix", "Name suffix"}, std::pair{"concealment-off", "Off"},
    };
    if (value.empty()) settings_open = true;
    else if (value == "on" || value == "off" || value == "toggle")
        set_enabled(value == "on" || (value == "toggle" && (stopped || !settings.boolean("enabled"))));
    else if (value == "save") {
        reply(storage_ready && store.save() ? "Settings saved." : "Settings could not be saved. See Preview & status.");
        return;
    } else if (value == "stop") {
        if (store.changed && !store.save()) {
            reply("Settings could not be saved. See Preview & status.");
            return;
        }
        stopped = true;
        settings_open = false;
        reply("Crest stopped. Use /crest on to resume.");
        return;
    } else if (value != "status") {
        const auto mode = std::find_if(display_modes.begin(), display_modes.end(), [value](auto entry) {
            return entry.first == value;
        });
        if (mode == display_modes.end()) {
            reply("Commands are /crest on, off, toggle, save, stop, status, parens-tags, parens, tags, suffix, or concealment-off.");
            return;
        }
        if (storage_ready && settings.set_text("concealment_display", mode->second)) changed();
    }
    if (!nameplate_api_available()) {
        reply("This Hailstone build needs the native nameplate SDK to draw Crest plates.");
        return;
    }
    std::string status = settings.boolean("enabled") && !stopped ? "Nameplates enabled. " : "Nameplates disabled. ";
    status += "Concealment display is ";
    status += settings.text("concealment_display");
    reply(status + ".");
}

void put(HailstoneOut* out, const char* key, int type, std::int64_t n, const char* text = nullptr) {
    if (key) host->out_key(host->host, out, key);
    const HailstoneValue value{type, n, 0, text};
    host->out_value(host->host, out, &value);
}

void put_text(HailstoneOut* out, const char* key, const std::string& value) {
    put(out, key, HAILSTONE_STRING, 0, value.c_str());
}

int result(HailstoneOut* out, bool success) {
    put(out, nullptr, HAILSTONE_BOOL, success);
    return 1;
}

const char* string_argument(const HailstoneArgs* args, std::size_t index) {
    HailstoneValue value{};
    if (!host->arg(host->host, args, index, &value) || value.kind != HAILSTONE_STRING) return nullptr;
    return value.s;
}

int ui_settings(void*, const HailstoneArgs*, HailstoneOut* out) {
    host->out_begin_table(host->host, out);
    put(out, "open", HAILSTONE_BOOL, settings_open);
    put(out, "enabled", HAILSTONE_BOOL, store.data.settings.boolean("enabled") && !stopped);
    put(out, "revision", HAILSTONE_INT, static_cast<std::int64_t>(revision));
    put(out, "has_character", HAILSTONE_BOOL, storage_ready && !store.file.empty());
    put(out, "available", HAILSTONE_BOOL, nameplate_api_available());
    put(out, "server_data", HAILSTONE_BOOL, server_data);
    if (!store.error.empty()) put_text(out, "storage_error", store.error);
    host->out_end(host->host, out);
    return 1;
}

int ui_config(void*, const HailstoneArgs*, HailstoneOut* out) {
    put_text(out, nullptr, store.data.settings.export_text());
    return 1;
}

int ui_profiles(void*, const HailstoneArgs*, HailstoneOut* out) {
    host->out_begin_table(host->host, out);
    for (const auto& [name, profile] : store.data.profiles) {
        // The UI bridge treats integer-shaped table keys as Lua numbers.
        const auto key = ':' + name;
        put_text(out, key.c_str(), profile.export_text());
    }
    host->out_end(host->host, out);
    return 1;
}

int ui_replace(void*, const HailstoneArgs* args, HailstoneOut* out) {
    const auto* text = string_argument(args, 0);
    if (!storage_ready || !text) return result(out, false);
    auto replacement = crest::Settings::import(text);
    if (!replacement) return result(out, false);
    stopped = false;
    store.data.settings = std::move(*replacement);
    changed();
    return result(out, true);
}

int ui_save(void*, const HailstoneArgs*, HailstoneOut* out) {
    return result(out, storage_ready && store.save());
}

int ui_save_profile(void*, const HailstoneArgs* args, HailstoneOut* out) {
    const auto* name = string_argument(args, 0);
    const auto* text = string_argument(args, 1);
    auto& profiles = store.data.profiles;
    if (!storage_ready || store.file.empty() || !name || !text || !crest::valid_profile_name(name) ||
        (!profiles.contains(name) && profiles.size() >= crest::max_profiles)) return result(out, false);
    auto profile = crest::Settings::import(text);
    if (!profile) return result(out, false);
    profiles.insert_or_assign(name, std::move(*profile));
    changed(false);
    return result(out, true);
}

int ui_delete_profile(void*, const HailstoneArgs* args, HailstoneOut* out) {
    const auto* name = string_argument(args, 0);
    if (!storage_ready || store.file.empty() || !name || !store.data.profiles.erase(name)) return result(out, false);
    changed(false);
    return result(out, true);
}

int ui_personal(void*, const HailstoneArgs*, HailstoneOut* out) {
    const auto& player = gameplay.player();
    if (!player.in_zone || !storage_ready) return 0;
    host->out_begin_table(host->host, out);
    put(out, "in_zone", HAILSTONE_BOOL, player.in_zone);
    put(out, "game_state", HAILSTONE_INT, GAMESTATE_INGAME);
    put(out, "cur_hp", HAILSTONE_INT, player.hp);
    put(out, "max_hp", HAILSTONE_INT, player.max_hp);
    put(out, "cur_mana", HAILSTONE_INT, player.mana);
    put(out, "max_mana", HAILSTONE_INT, player.max_mana);
    put(out, "cur_endurance", HAILSTONE_INT, player.endurance);
    put(out, "max_endurance", HAILSTONE_INT, player.max_endurance);
    put_text(out, "cast_name", player.cast_name);
    host->out_end(host->host, out);
    return 1;
}

int ui_set_setting(void*, const HailstoneArgs* args, HailstoneOut* out) {
    const auto* key = string_argument(args, 0);
    HailstoneValue value{};
    if (!key || !host->arg(host->host, args, 1, &value) || value.kind != HAILSTONE_BOOL ||
        std::string_view(key) != "open") return result(out, false);
    settings_open = value.i != 0;
    return result(out, true);
}

void refresh_ui() {
    if (!HAILSTONE_HOST_HAS(host, caps) || !(host->caps(host->host) & HAILSTONE_CAP_UI)) {
        ui_registered = false;
        settings_open = false;
        return;
    }
    if (ui_registered) return;
    if (HAILSTONE_HOST_HAS(host, add_ui_function) && HAILSTONE_HOST_HAS(host, arg) &&
        HAILSTONE_HOST_HAS(host, out_begin_table) && HAILSTONE_HOST_HAS(host, out_end) &&
        HAILSTONE_HOST_HAS(host, out_key) && HAILSTONE_HOST_HAS(host, out_value)) {
        using Binding = std::pair<const char*, HailstoneUiFn>;
        static constexpr Binding functions[]{
            {"settings", ui_settings}, {"get_config", ui_config}, {"get_profiles", ui_profiles},
            {"replace_settings", ui_replace}, {"save_settings", ui_save}, {"save_profile", ui_save_profile},
            {"delete_profile", ui_delete_profile}, {"personal", ui_personal}, {"set_setting", ui_set_setting},
        };
        for (const auto& [name, method] : functions) host->add_ui_function(host->host, name, method, nullptr);
    }
    if (HAILSTONE_HOST_HAS(host, add_option))
        host->add_option(host->host, "enabled", "Enable Crest nameplates", 0, 1,
                         [](void*) -> int { return store.data.settings.boolean("enabled") && !stopped; },
                         [](void*, int value) { set_enabled(value != 0); }, nullptr);
    if (HAILSTONE_HOST_HAS(host, add_menu)) host->add_menu(host->host, "Crest", "/crest");
    ui_registered = true;
}

} // namespace

HAILSTONE_API void InitializePlugin() {
    if (!nameplate_api_available() && HAILSTONE_HOST_HAS(host, log))
        host->log(host->host, 2, "Crest requires a Hailstone build with the merged native nameplate SDK.");
    refresh_character(Clock::now());
    refresh_registration();
    if (HAILSTONE_HOST_HAS(host, add_command)) AddCommand("/crest", command, nullptr);
    refresh_ui();
}

HAILSTONE_API void ShutdownPlugin() {
    if (registered && HAILSTONE_HOST_HAS(host, nameplate_unregister)) host->nameplate_unregister(host->host);
    if (store.changed && !store.save() && HAILSTONE_HOST_HAS(host, log)) host->log(host->host, 2, store.error.c_str());
    registered = packets_allowed = settings_open = ui_registered = stopped = false;
    store.open({});
    next_character_poll = {};
    ++revision;
    reset_world();
}

HAILSTONE_API void OnPulse() {
    const auto now = Clock::now();
    refresh_character(now);
    refresh_registration();
    refresh_ui();
    if (store.changed && now >= save_at && !store.save()) save_at = now + 1s;
    if (!nameplate_api_available() || now < next_gameplay_poll) return;
    next_gameplay_poll = now + 100ms;
    if (host->caps(host->host) & HAILSTONE_CAP_READ)
        gameplay.refresh(host, store.data.settings, now_ms(), packets_allowed);
    else if (gameplay.player().in_zone) gameplay.reset();
    sync_indicator_scope();
}

HAILSTONE_API void OnBeginZone() { reset_world(); }
HAILSTONE_API void OnRemoveSpawn(std::uint32_t id) { forget_actor(id); }
HAILSTONE_API int OnPacket(int stream, int direction, HailstonePacket* packet) {
    if (!packets_allowed || stream != HAILSTONE_STREAM_ZONE || direction != HAILSTONE_IN || !packet || !packet->data) return 0;
    const auto bytes = std::as_bytes(std::span{packet->data, packet->size});
    if (packet->opcode == 0x7d1c || packet->opcode == 0x7d1d) {
        if (HAILSTONE_HOST_HAS(host, get_player) && !gameplay.refresh_identity(host)) {
            sync_indicator_scope();
            return 0;
        }
        sync_indicator_scope();
        if (indicators.receive(packet->opcode, bytes, now_ms())) server_data = true;
    } else if (packet->opcode == 0x7280 && bytes.size() >= 4) forget_actor(crest::wire::le32(bytes, 0));
    else if (packet->opcode == 0x3791 || packet->opcode == 0x5f42) {
        gameplay.receive_points(host, packet->opcode, bytes);
        sync_indicator_scope();
    }
    return 0;
}
