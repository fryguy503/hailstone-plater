// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

namespace crest {

inline constexpr std::size_t max_profile_bytes = 32768;
inline constexpr std::size_t max_profiles = 16;
inline constexpr std::size_t max_rules = 64;

struct Rule {
    std::string name, kind = "any";
    bool exact = false, enabled = true, hide = false;
    std::uint32_t color = 0xe5b454;
    double scale = 1;
};

class Settings {
public:
    Settings();
    bool boolean(std::string_view key) const;
    double number(std::string_view key) const;
    std::string_view text(std::string_view key) const;
    const std::vector<Rule>& rules() const { return rules_; }
    bool set(std::string_view key, bool value);
    bool set_text(std::string_view key, std::string_view value);
    static std::optional<Settings> import(std::string_view text);
    std::string export_text() const;

private:
    using Value = std::variant<bool, double, std::string>;
    std::unordered_map<std::string_view, Value> values_;
    std::vector<Rule> rules_;
};

using Profiles = std::map<std::string, Settings, std::less<>>;

struct SettingsData {
    Settings settings;
    Profiles profiles;
};

class SettingsStore {
public:
    SettingsData data;
    std::filesystem::path file;
    std::string error;
    bool changed = false;

    void open(std::filesystem::path path, const std::filesystem::path& legacy = {});
    bool save();
};

std::string hex_encode(std::string_view text);
std::optional<std::string> hex_decode(std::string_view text);
std::optional<double> parse_profile_number(std::string_view text);
bool valid_profile_name(std::string_view name);
std::optional<SettingsData> import_legacy_settings(std::string_view text);

} // namespace crest
