// SPDX-License-Identifier: MIT
#include "gameplay.hpp"
#include "settings.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {

constexpr std::uint16_t mana = 0x3791;
constexpr std::uint16_t endurance = 0x5f42;

void require(bool condition, const char* error) {
    if (!condition) throw std::runtime_error(error);
}

struct Fixture {
    PluginHostApi api{};
    HailstonePlayer player{};
    std::array<HailstoneSpawn, 5> spawns{};
    std::uint32_t xtarget = 3;

    Fixture() {
        player.size = sizeof(player);
        player.spawn_id = 1;
        player.game_state = GAMESTATE_INGAME;
        player.in_zone = 1;
        player.in_group = 1;
        player.zone_id = 10;
        player.target_id = 3;
        player.xtarget_slots = 1;
        player.level = 60;
        player.cur_hp = 15'000;
        player.max_hp = 20'000;
        player.cur_mana = 100;
        player.max_mana = 250;
        player.cur_endurance = 10;
        player.max_endurance = 50;
        player.casting_spell_id = 200;
        std::memcpy(player.name, "Player", 7);
        for (std::uint32_t id = 1; id < spawns.size(); ++id) {
            auto& spawn = spawns[id];
            spawn.size = sizeof(spawn);
            spawn.id = id;
            spawn.level = id == 1 ? 60 : 61;
            spawn.incarnation = id + 100;
            spawn.con_color = 15;
            spawn.in_group = id <= 2;
            std::memcpy(spawn.name, "Actor", 6);
        }

        api.size = sizeof(api);
        api.host = this;
        api.get_player = [](void* context, HailstonePlayer* out) {
            *out = static_cast<Fixture*>(context)->player;
            return 1;
        };
        api.get_spawn = [](void* context, std::uint32_t id, HailstoneSpawn* out) {
            const auto& actors = static_cast<Fixture*>(context)->spawns;
            if (id == 0 || id >= actors.size()) return 0;
            *out = actors[id];
            return 1;
        };
        api.get_xtarget = [](void* context, std::uint32_t index, HailstoneXTarget* out) {
            if (index != 0) return 0;
            out->spawn_id = static_cast<Fixture*>(context)->xtarget;
            return 1;
        };
        api.get_spell = [](void*, std::uint32_t id, HailstoneSpell* out) {
            const std::string_view text = id == 101 ? "Spirit" : id == 102 ? "Poison" : id == 103 ? "Sleep" :
                                          id == 200 ? "Clarity" : "";
            if (text.empty()) return 0;
            std::memcpy(out->name, text.data(), text.size());
            return 1;
        };
        api.get_buff_data_bulk = [](void*, unsigned char* out, std::int32_t capacity) -> std::int32_t {
            std::array<HS_BulkBuffData, 2> buffs{};
            buffs[0].ID = 101;
            buffs[0].spelltype = 1;
            buffs[1].ID = 102;
            if (capacity < static_cast<std::int32_t>(sizeof(buffs))) return 0;
            std::memcpy(out, buffs.data(), sizeof(buffs));
            return static_cast<std::int32_t>(sizeof(buffs));
        };
        api.get_target_buff_data_bulk = [](void*, unsigned char* out, std::int32_t capacity) -> std::int32_t {
            const HS_BulkListHeader header{2, 2};
            const std::array<HS_BulkTargetBuffFixedRecord, 2> buffs{{{103, 8, 0, 20, 0}, {101, 9, 1, 4, 0}}};
            const auto size = static_cast<std::int32_t>(sizeof(header) + sizeof(buffs));
            if (capacity < size) return 0;
            std::memcpy(out, &header, sizeof(header));
            std::memcpy(out + sizeof(header), buffs.data(), sizeof(buffs));
            return size;
        };
        api.parse = [](void*, const char* expression, char* out, std::size_t capacity) {
            const auto expr = std::string_view(expression);
            std::string_view result = "NULL";
            if (expr == "${Me.CombatState}|${Me.Combat}") result = "ACTIVE|TRUE";
            if (expr == "${Group.GroupSize}") result = "2";
            if (expr == "${Group.Member[0].Spawn.ID}") result = "1";
            if (expr == "${Group.Member[1].Spawn.ID}") result = "2";
            if (expr == "${Spell[102].SpellIcon}|${Spawn[1].BuffDuration[2].TotalSeconds}") result = "112|24";
            if (expr == "${Spell[103].SpellIcon}|${Spawn[3].BuffDuration[1].TotalSeconds}") result = "113|42";
            if (expr == "${Spell[101].SpellIcon}|${Spawn[3].BuffDuration[2].TotalSeconds}") result = "111|NULL";
            if (capacity) {
                const auto count = std::min(result.size(), capacity - 1);
                std::memcpy(out, result.data(), count);
                out[count] = '\0';
            }
            return static_cast<int>(result.size());
        };
    }

    HailstoneNameplateCandidate plate(std::uint32_t id) const {
        HailstoneNameplateCandidate candidate{};
        candidate.spawn_id = id;
        candidate.incarnation = spawns[id].incarnation;
        candidate.level = spawns[id].level;
        candidate.flags = id == player.spawn_id ? HAILSTONE_NAMEPLATE_SELF : HAILSTONE_NAMEPLATE_NPC;
        return candidate;
    }
};

std::array<std::byte, 10> points(std::uint8_t value, std::uint8_t id) {
    return {std::byte{value}, {}, {}, {}, std::byte{100}, {}, {}, {}, std::byte{id}, {}};
}

void test_gameplay() {
    Fixture fixture;
    crest::Gameplay game;
    crest::Settings settings;
    require(settings.set("show_auras", true), "enable auras");
    require(settings.set_text("aura_mode", "detrimental"), "set aura filter");
    game.refresh(&fixture.api, settings, 0, true);
    const auto& player = game.player();
    require(player.in_zone && player.in_combat && player.target_id == 3 && player.hp == 15'000 &&
            player.max_hp == 20'000 && player.mana == 100 && player.max_endurance == 50 && player.cast_name == "Clarity",
            "personal points, combat or casting observation");

    const auto self = game.actor(&fixture.api, fixture.plate(1), true);
    require(!self.consider_color && self.auras.size() == 1 && self.auras[0].label == "Poison 24s" &&
            self.auras[0].spell_icon == 112u && self.auras[0].detrimental, "self filtered aura");
    const auto target = game.actor(&fixture.api, fixture.plate(3), true);
    require(target.consider_color == 0xf1d85au && target.auras.size() == 1 && target.auras[0].label == "Sleep 42s" &&
            target.auras[0].spell_icon == 113u, "target buff order, countdown or consider RGB");

    require(game.receive_points(&fixture.api, mana, points(0, 2)), "group points rejected");
    require(game.receive_points(&fixture.api, endurance, points(25, 3)), "extended target points rejected");
    require(!game.receive_points(&fixture.api, mana, points(10, 1)), "self overwritten by supplemental packet");
    require(!game.receive_points(&fixture.api, mana, points(90, 4)), "untracked actor points accepted");
    require(game.actor(&fixture.api, fixture.plate(2), false).resources.mana_percent == 0, "known zero lost");
    require(game.actor(&fixture.api, fixture.plate(3), false).resources.endurance_percent == 25, "target points lost");

    require(settings.set_text("aura_mode", "whitelist") && settings.set_text("aura_names", " poison , SPIRIT "),
            "set literal case-insensitive aura whitelist");
    game.refresh(&fixture.api, settings, 500, true);
    require(game.actor(&fixture.api, fixture.plate(2), false).resources.mana_percent == 0, "group scope prune lost known zero");
    const auto white = game.actor(&fixture.api, fixture.plate(3), false).auras;
    require(white.size() == 1 && white[0].label == "Spirit" && white[0].spell_icon == 111u && !white[0].detrimental,
            "literal whitelist or a permanent buff without duration");

    fixture.xtarget = 0;
    require(game.receive_points(&fixture.api, mana, points(35, 2)), "current group update rejected");
    require(!game.actor(&fixture.api, fixture.plate(3), false).resources.endurance_percent,
            "packet receive retained a departed extended target before the next pulse");
    fixture.player.target_id = 4;
    game.refresh(&fixture.api, settings, 750, true);
    const auto old_target = game.actor(&fixture.api, fixture.plate(3), false);
    require(old_target.auras.empty() && !old_target.resources.endurance_percent, "stale old target aura or resource");
    ++fixture.spawns[2].incarnation;
    require(!game.actor(&fixture.api, fixture.plate(2), false).resources.mana_percent, "resource leaked to recycled actor");
    require(game.receive_points(&fixture.api, mana, points(42, 2)), "new group incarnation could not establish points");
    game.refresh(&fixture.api, settings, 751, false);
    require(!game.actor(&fixture.api, fixture.plate(2), false).resources.mana_percent, "packet permission loss retained points");
}

} // namespace

int main() {
    try {
        test_gameplay();
        std::cout << "Crest gameplay tests passed\n";
        return 0;
    } catch (const std::exception& failure) {
        std::cerr << "Crest gameplay test failed: " << failure.what() << '\n';
        return 1;
    }
}
