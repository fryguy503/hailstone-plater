// SPDX-License-Identifier: MIT
#include "settings.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {

void require(bool passed, const char* message) {
    if (!passed) throw std::runtime_error(message);
}

void imports_existing_profile() {
    const auto settings = crest::Settings::import(
        "CREST1\r\nwidth=340\ncon_name_color=true\nlevel_position=below\naura_names=6d657a\n"
        "rule|477561726469616e|0|npc|1|0|16744448|1.2");
    require(settings && settings->number("width") == 340 && settings->number("height") == 12 &&
            settings->boolean("con_name_color") && settings->text("level_position") == "below" &&
            settings->text("aura_names") == "mez" && settings->rules().size() == 1 &&
            settings->rules().front().name == "Guardian", "Lua profile fields and missing defaults are restored");
    require(crest::Settings::import(settings->export_text())->export_text() == settings->export_text(),
            "native profile export round trips");
    require(!crest::Settings::import("CREST1\nwidth=200\nwidth=240") &&
            !crest::Settings::import("CREST1\nwidth=nan") &&
            !crest::Settings::import("CREST1\nlevel_position=middle"), "invalid input cannot partially apply");
}

void migrates_and_replaces_character_store() {
    namespace fs = std::filesystem;
    struct Scratch {
        fs::path path;
        bool owned = false;
        ~Scratch() {
            if (!owned) return;
            std::error_code ec;
#ifdef _WIN32
            const auto absolute = fs::absolute(path, ec).make_preferred().native();
            if (!ec && absolute.size() > 2 && absolute[1] == L':') {
                fs::remove_all(fs::path(L"\\\\?\\" + absolute), ec);
                return;
            }
#endif
            fs::remove_all(path, ec);
        }
    } scratch{fs::temp_directory_path() / ("crest-store-test-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))};
    scratch.owned = fs::create_directory(scratch.path);
    require(scratch.owned, "create a private test directory");
    const auto file = scratch.path / "crest-settings.txt";
    const auto legacy = scratch.path / "saved.lua";
    std::ofstream(legacy) << R"(return {
      ["settings"] = { ["width"] = 260, ["level_position"] = "below",
        ["rules"] = { [1] = { ["name"] = "A \"Quoted\" Mob", ["enabled"] = true, ["scale"] = 1.25 } }
      },
      ["profiles"] = { ["Raid profile"] = { ["enabled"] = false, ["through_walls"] = true } },
      ["version"] = 1
    })";
    crest::SettingsStore store;
    store.open(file, legacy);
    require(store.changed && store.error.empty() && store.data.settings.number("width") == 260 &&
            store.data.profiles.at("Raid profile").boolean("through_walls") &&
            store.data.settings.rules().size() == 1 &&
            store.data.settings.rules()[0].name == "A \"Quoted\" Mob", "known mq.pickle format migrates as data");
    require(store.save() && fs::exists(legacy), "migration keeps the source and saves the character store");
    store.data.settings.set("enabled", false);
    store.changed = true;
    require(store.save(), "a subsequent save atomically replaces the character store");
    store.open(file, legacy);
    require(store.error.empty() && !store.changed && !store.data.settings.boolean("enabled") &&
            store.data.profiles.size() == 1, "the new store takes precedence");
    require(!crest::import_legacy_settings("return { [\"settings\"] = os.execute(\"bad\") }"),
            "Lua expressions are refused without execution");

#ifdef _WIN32
    const auto deep_file = scratch.path / std::string(100, 'a') / std::string(100, 'b') / "crest-settings.txt";
    require(deep_file.native().size() > 260, "exercise the Windows path ceiling");
    store.open(deep_file);
    store.data.settings.set("enabled", false);
    store.changed = true;
    require(store.save(), "long paths support the atomic character save");
    store.open(deep_file);
    require(store.error.empty() && !store.data.settings.boolean("enabled"), "long character path can be reopened");
#endif
}

} // namespace

int main() {
    try {
        imports_existing_profile();
        migrates_and_replaces_character_store();
        std::cout << "Crest native configuration tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
