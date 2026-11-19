// SPDX-License-Identifier: MIT
#include "gameplay.hpp"
#include "settings.hpp"
#include "wire_bytes.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <string_view>

namespace crest {
namespace {

constexpr std::uint32_t max_slots = 64;
constexpr auto no_spell = 0xffffffffu;

struct BuffRecord {
    std::uint32_t spell_id;
    bool detrimental;
};

template<std::size_t Size>
std::string_view counted(const char (&text)[Size]) {
    return {text, static_cast<std::size_t>(std::find(text, text + Size, '\0') - text)};
}

std::string_view macro(const PluginHostApi* api, const char* expression, std::span<char> buffer) {
    if (!HAILSTONE_HOST_HAS(api, parse)) return {};
    const int length = api->parse(api->host, expression, buffer.data(), buffer.size());
    if (length < 0 || static_cast<std::size_t>(length) >= buffer.size()) return {};
    return {buffer.data(), static_cast<std::size_t>(length)};
}

std::optional<std::uint32_t> number(std::string_view text) {
    if (text.empty()) return std::nullopt;
    std::uint32_t result{};
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) return std::nullopt;
    return result;
}

bool due(std::optional<std::uint64_t> last, std::uint64_t now, std::uint64_t interval) {
    return !last || now < *last || now - *last >= interval;
}

bool read_spawn(const PluginHostApi* api, std::uint32_t id, HailstoneSpawn& spawn) {
    spawn = {};
    spawn.size = sizeof(spawn);
    return id && HAILSTONE_HOST_HAS(api, get_spawn) && api->get_spawn(api->host, id, &spawn) && spawn.id == id;
}

ResourceActor living_actor(const PluginHostApi* api, std::uint32_t id) {
    HailstoneSpawn spawn{};
    if (!read_spawn(api, id, spawn) || spawn.is_corpse || counted(spawn.name).empty()) return {};
    return {id, spawn.incarnation};
}

std::uint32_t xtarget_id(const PluginHostApi* api, std::uint32_t index) {
    HailstoneXTarget slot{};
    slot.size = sizeof(slot);
    if (!HAILSTONE_HOST_HAS(api, get_xtarget) || !api->get_xtarget(api->host, index, &slot)) return 0;
    return slot.spawn_id;
}

bool read_spell(const PluginHostApi* api, std::uint32_t id, HailstoneSpell& spell) {
    spell = {};
    spell.size = sizeof(spell);
    return id && id != no_spell && HAILSTONE_HOST_HAS(api, get_spell) && api->get_spell(api->host, id, &spell);
}

std::vector<BuffRecord> self_buffs(const PluginHostApi* api) {
    std::vector<BuffRecord> result;
    if (!HAILSTONE_HOST_HAS(api, get_buff_data_bulk)) return result;
    std::array<unsigned char, max_slots * HAILSTONE_BUFF_RECORD_SIZE> buffer{};
    const auto count = api->get_buff_data_bulk(api->host, buffer.data(), static_cast<std::int32_t>(buffer.size()));
    if (count <= 0 || static_cast<std::size_t>(count) > buffer.size() || count % HAILSTONE_BUFF_RECORD_SIZE != 0) return result;
    const auto bytes = std::as_bytes(std::span(buffer).first(static_cast<std::size_t>(count)));
    for (std::size_t offset = 0; offset < bytes.size(); offset += HAILSTONE_BUFF_RECORD_SIZE) {
        const auto spell_id = wire::le32(bytes, offset + offsetof(HS_BulkBuffData, ID));
        if (spell_id && spell_id != no_spell)
            result.push_back({spell_id, wire::le32(bytes, offset + offsetof(HS_BulkBuffData, spelltype)) == 0});
    }
    return result;
}

std::vector<BuffRecord> target_buffs(const PluginHostApi* api) {
    if (!HAILSTONE_HOST_HAS(api, get_target_buff_data_bulk)) return {};
    constexpr std::size_t max_caster_name_bytes = 64;
    std::array<unsigned char, HS_BULK_LIST_HEADER_SIZE + max_slots * (HAILSTONE_TARGET_BUFF_RECORD_SIZE + max_caster_name_bytes)> buffer{};
    const auto length = api->get_target_buff_data_bulk(api->host, buffer.data(), static_cast<std::int32_t>(buffer.size()));
    if (length < HS_BULK_LIST_HEADER_SIZE || static_cast<std::size_t>(length) > buffer.size()) return {};
    const auto bytes = std::as_bytes(std::span(buffer).first(static_cast<std::size_t>(length)));
    const auto count = wire::le32(bytes, offsetof(HS_BulkListHeader, count));
    const auto total = wire::le32(bytes, offsetof(HS_BulkListHeader, total));
    if (count > total || count > max_slots) return {};
    std::vector<BuffRecord> result;
    result.reserve(count);
    std::size_t offset = HS_BULK_LIST_HEADER_SIZE;
    for (std::uint32_t index = 0; index < count; ++index) {
        if (bytes.size() - offset < HAILSTONE_TARGET_BUFF_RECORD_SIZE) return {};
        const auto name_length = wire::le32(bytes, offset + offsetof(HS_BulkTargetBuffFixedRecord, casterNameLength));
        if (name_length > bytes.size() - offset - HAILSTONE_TARGET_BUFF_RECORD_SIZE) return {};
        const auto spell_id = wire::le32(bytes, offset + offsetof(HS_BulkTargetBuffFixedRecord, spellID));
        if (spell_id && spell_id != no_spell)
            result.push_back({spell_id, wire::le32(bytes, offset + offsetof(HS_BulkTargetBuffFixedRecord, spellType)) == 0});
        offset += HAILSTONE_TARGET_BUFF_RECORD_SIZE + name_length;
    }
    if (offset != bytes.size()) return {};
    return result;
}

char lower_ascii(char value) { return value >= 'A' && value <= 'Z' ? static_cast<char>(value + ('a' - 'A')) : value; }

bool listed(std::string_view name, std::string_view words) {
    constexpr std::string_view whitespace = " \t\n\r\f\v";
    while (!words.empty()) {
        const auto comma = words.find(',');
        auto word = words.substr(0, comma);
        const auto start = word.find_first_not_of(whitespace);
        if (start != std::string_view::npos) {
            word = word.substr(start, word.find_last_not_of(whitespace) - start + 1);
            if (std::search(name.begin(), name.end(), word.begin(), word.end(), [](char a, char b) {
                return lower_ascii(a) == lower_ascii(b);
            }) != name.end()) return true;
        }
        if (comma == std::string_view::npos) break;
        words.remove_prefix(comma + 1);
    }
    return false;
}

std::string clipped_label(std::string_view text) {
    std::size_t length = std::min<std::size_t>(20, text.size());
    if (length < text.size())
        while (length && (static_cast<unsigned char>(text[length]) & 0xc0u) == 0x80u) --length;
    std::string result(text.substr(0, length));
    for (char& byte : result)
        if (static_cast<unsigned char>(byte) < 32 || byte == 127) byte = ' ';
    return result;
}

std::vector<AuraObservation> make_auras(const PluginHostApi* api, std::uint32_t id, const Settings& settings,
                                        std::span<const BuffRecord> records) {
    const auto requested = settings.number("max_auras");
    const auto limit = static_cast<std::size_t>(std::clamp(std::isfinite(requested) ? requested : 3.0, 1.0, 5.0));
    const auto mode = settings.text("aura_mode");
    const auto names = settings.text("aura_names");
    std::vector<AuraObservation> output;
    output.reserve(limit);
    for (std::size_t index = 0; index < records.size(); ++index) {
        const auto& record = records[index];
        if (mode != "all" && mode != "whitelist" && !record.detrimental) continue;
        HailstoneSpell spell{};
        if (!read_spell(api, record.spell_id, spell)) continue;
        const auto name = counted(spell.name);
        if (mode == "whitelist" && !listed(name, names)) continue;
        auto& aura = output.emplace_back();
        aura.label = clipped_label(name);
        aura.detrimental = record.detrimental;
        char expression[128], result[128];
        std::snprintf(expression, sizeof(expression), "${Spell[%u].SpellIcon}|${Spawn[%u].BuffDuration[%zu].TotalSeconds}",
                      record.spell_id, id, index + 1);
        const auto value = macro(api, expression, result);
        const auto separator = value.find('|');
        if (separator != std::string_view::npos) {
            const auto icon = number(value.substr(0, separator));
            if (icon && *icon <= 65535) aura.spell_icon = icon;
            const auto duration = number(value.substr(separator + 1));
            if (duration && *duration) aura.label += " " + std::to_string(*duration) + "s";
        }
        if (output.size() == limit) break;
    }
    return output;
}

std::uint32_t consider_rgb(std::uint32_t color) {
    switch (color) {
    case 6: return 0x999999;
    case 2: return 0x58d07d;
    case 18: return 0x6ecfe4;
    case 4: return 0x6599ef;
    case 10: return 0xeeeeee;
    case 15: return 0xf1d85a;
    case 13: return 0xf16b67;
    default: return 0;
    }
}

} // namespace

bool Gameplay::sync_player(const PluginHostApi* api, HailstonePlayer& sample) {
    sample = {};
    sample.size = sizeof(sample);
    if (!HAILSTONE_HOST_HAS(api, get_player) || !api->get_player(api->host, &sample) ||
        !sample.in_zone || sample.game_state != GAMESTATE_INGAME || !sample.spawn_id || counted(sample.name).empty()) {
        if (player_.in_zone) reset();
        return false;
    }
    if (player_.self_id != sample.spawn_id || character_ != counted(sample.name) || zone_id_ != sample.zone_id ||
        zone_instance_ != sample.zone_instance || grouped_ != (sample.in_group != 0) || player_.in_raid != (sample.in_raid != 0)) {
        reset();
        character_ = counted(sample.name);
        zone_id_ = sample.zone_id;
        zone_instance_ = sample.zone_instance;
        grouped_ = sample.in_group != 0;
    }
    if (player_.level != sample.level) consider_cache_.fill(0);
    if (player_.target_id != sample.target_id) auras_at_.reset();
    player_.in_zone = true;
    player_.in_raid = sample.in_raid != 0;
    player_.self_id = sample.spawn_id;
    player_.target_id = sample.target_id;
    player_.level = sample.level;
    player_.hp = sample.cur_hp;
    player_.max_hp = sample.max_hp;
    player_.mana = sample.cur_mana;
    player_.max_mana = sample.max_mana;
    player_.endurance = sample.cur_endurance;
    player_.max_endurance = sample.max_endurance;
    if (casting_spell_ != sample.casting_spell_id) {
        casting_spell_ = sample.casting_spell_id;
        HailstoneSpell spell{};
        player_.cast_name = read_spell(api, casting_spell_, spell) ? counted(spell.name) : std::string_view{};
    }
    return true;
}

void Gameplay::refresh(const PluginHostApi* api, const Settings& settings, std::uint64_t now_ms, bool packets_allowed) {
    HailstonePlayer sample{};
    if (!sync_player(api, sample)) return;
    if (due(combat_at_, now_ms, 100)) {
        char result[64];
        const auto value = macro(api, "${Me.CombatState}|${Me.Combat}", result);
        const auto separator = value.find('|');
        const auto state = value.substr(0, separator);
        const auto attack = separator != std::string_view::npos ? value.substr(separator + 1) : std::string_view{};
        player_.in_combat = state == "COMBAT" || attack == "TRUE" || attack == "true";
        combat_at_ = now_ms;
    }
    if (!packets_allowed) {
        resources_.reset();
        resources_at_.reset();
    } else if (due(resources_at_, now_ms, 250)) {
        refresh_scope(api, sample);
        resources_at_ = now_ms;
    }
    if (!settings.boolean("show_auras")) {
        if (auras_at_) auras_ = {};
        auras_at_.reset();
    } else if (due(auras_at_, now_ms, 500)) {
        refresh_auras(api, settings);
        auras_at_ = now_ms;
    }
}

ResourceActor Gameplay::refresh_scope(const PluginHostApi* api, const HailstonePlayer& sample, std::uint32_t incoming_id) {
    std::array<ResourceActor, max_slots * 2> tracked{};
    std::size_t count = 0;
    ResourceActor incoming{};
    const auto add = [&](std::uint32_t id) {
        if (id == 0 || id == sample.spawn_id) return;
        const auto actor = living_actor(api, id);
        if (!actor.incarnation) return;
        tracked[count++] = actor;
        if (id == incoming_id) incoming = actor;
    };
    if (sample.in_group) {
        char result[32];
        const auto slots = std::min(max_slots, number(macro(api, "${Group.GroupSize}", result)).value_or(0));
        for (std::uint32_t index = 0; index < slots; ++index) {
            char expression[64];
            std::snprintf(expression, sizeof(expression), "${Group.Member[%u].Spawn.ID}", index);
            add(number(macro(api, expression, result)).value_or(0));
        }
    }
    for (std::uint32_t index = 0; index < std::min(max_slots, sample.xtarget_slots); ++index) add(xtarget_id(api, index));
    resources_.prune(std::span(tracked).first(count));
    return incoming;
}

bool Gameplay::receive_points(const PluginHostApi* api, std::uint16_t opcode, std::span<const std::byte> packet) {
    const auto id = points_actor_id(opcode, packet);
    if (!id) return false;
    HailstonePlayer sample{};
    if (!sync_player(api, sample) || *id == sample.spawn_id) return false;
    const auto actor = refresh_scope(api, sample, *id);
    return actor.incarnation && resources_.receive(opcode, packet, actor);
}

void Gameplay::refresh_auras(const PluginHostApi* api, const Settings& settings) {
    auras_ = {};
    auras_[0].actor = living_actor(api, player_.self_id);
    if (auras_[0].actor.incarnation)
        auras_[0].auras = make_auras(api, player_.self_id, settings, self_buffs(api));
    if (player_.target_id == player_.self_id) return;
    auras_[1].actor = living_actor(api, player_.target_id);
    if (auras_[1].actor.incarnation)
        auras_[1].auras = make_auras(api, player_.target_id, settings, target_buffs(api));
}

std::optional<std::uint32_t> Gameplay::consider(const PluginHostApi* api, const HailstoneNameplateCandidate& actor) {
    if (actor.flags & (HAILSTONE_NAMEPLATE_SELF | HAILSTONE_NAMEPLATE_CORPSE)) return std::nullopt;
    const bool cacheable = actor.level > 0 && actor.level <= 255 && player_.level > 0;
    const auto index = (actor.flags & HAILSTONE_NAMEPLATE_NPC ? 0u : 256u) + actor.level;
    if (cacheable && consider_cache_[index]) return consider_cache_[index];
    HailstoneSpawn spawn{};
    if (!read_spawn(api, actor.spawn_id, spawn) || spawn.incarnation != actor.incarnation) return std::nullopt;
    const auto color = consider_rgb(spawn.con_color);
    if (!color) return std::nullopt;
    if (cacheable) consider_cache_[index] = color;
    return color;
}

ActorGameplay Gameplay::actor(const PluginHostApi* api, const HailstoneNameplateCandidate& actor, bool need_consider) {
    ActorGameplay result;
    if (need_consider) result.consider_color = consider(api, actor);
    if ((actor.flags & HAILSTONE_NAMEPLATE_CORPSE) || !actor.incarnation) return result;
    const ResourceActor identity{actor.spawn_id, actor.incarnation};
    result.resources = resources_.values(identity);
    if (actor.spawn_id == player_.self_id && auras_[0].actor == identity) result.auras = auras_[0].auras;
    if (actor.spawn_id == player_.target_id && auras_[1].actor == identity) result.auras = auras_[1].auras;
    return result;
}

void Gameplay::remove(std::uint32_t id) {
    resources_.remove(id);
    for (auto& set : auras_)
        if (set.actor.id == id) set = {};
}

bool Gameplay::refresh_identity(const PluginHostApi* api) {
    HailstonePlayer sample{};
    return sync_player(api, sample);
}

void Gameplay::reset() {
    const auto next_revision = scope_revision_ + 1;
    *this = {};
    scope_revision_ = next_revision;
}

} // namespace crest
