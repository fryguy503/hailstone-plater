// SPDX-License-Identifier: MIT
#include "settings.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <fstream>
#include <locale>
#include <random>
#include <sstream>
#include <unordered_set>

namespace crest {
namespace {

enum class Kind { BOOL, NUMBER, COLOR, STRING, ENUM };
struct Definition {
    std::string_view key;
    Kind kind;
    std::string_view initial;
    double minimum, maximum;
    std::string_view choices;
};
constexpr Definition schema[] = {
#include "settings_schema.inc"
};

const Definition* definition(std::string_view key) {
    static const auto index = [] {
        std::unordered_map<std::string_view, const Definition*> result;
        result.reserve(std::size(schema));
        for (const auto& item : schema) result.emplace(item.key, &item);
        return result;
    }();
    const auto it = index.find(key);
    return it == index.end() ? nullptr : it->second;
}

std::string_view take(std::string_view& input, char delimiter) {
    const auto split = input.find(delimiter);
    const auto piece = input.substr(0, split);
    input.remove_prefix(split == input.npos ? input.size() : split + 1);
    return piece;
}

template<typename T>
std::optional<T> parse_number(std::string_view text) {
    if (text.empty()) return {};
    if (text.front() == '+') text.remove_prefix(1);
    if (text.empty()) return {};
    T result{};
    const auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), result);
    if (ec != std::errc{} || ptr != text.data() + text.size()) return {};
    return result;
}

std::string format_number(double number) {
    std::array<char, 64> text{};
    const auto result = std::to_chars(text.data(), text.data() + text.size(), number);
    return std::string(text.data(), result.ptr);
}

bool control(unsigned char c) { return c < 32 || c == 127; }

std::string clean_text(std::string_view text, std::size_t limit) {
    std::string cleaned(text.substr(0, limit));
    for (char& c : cleaned) if (control(static_cast<unsigned char>(c))) c = ' ';
    return cleaned;
}

bool matches_choice(std::string_view choices, std::string_view value) {
    while (!choices.empty()) if (take(choices, '|') == value) return true;
    return false;
}

std::optional<Rule> parse_rule(std::string_view line) {
    std::array<std::string_view, 7> fields{};
    for (auto& field : fields) field = take(line, '|');
    if (!line.empty()) return {};
    const auto name = hex_decode(fields[0]);
    const auto valid_flag = [](std::string_view flag) { return flag == "0" || flag == "1"; };
    if (!name || name->empty() || !valid_flag(fields[1]) || !valid_flag(fields[3]) || !valid_flag(fields[4])) return {};
    auto color = parse_profile_number(fields[5]), scale = parse_profile_number(fields[6]);
    if (!color || !scale) return {};
    Rule rule;
    rule.name = clean_text(*name, 80);
    if (fields[2] == "npc" || fields[2] == "player" || fields[2] == "pet") rule.kind = fields[2];
    rule.exact = fields[1] == "1";
    rule.enabled = fields[3] == "1";
    rule.hide = fields[4] == "1";
    rule.color = static_cast<std::uint32_t>(std::clamp(*color, 0.0, 16777215.0));
    rule.scale = std::clamp(*scale, 0.5, 2.0);
    return rule;
}

constexpr std::size_t max_store_bytes = (max_profiles + 1) * (max_profile_bytes + 128);

std::filesystem::path storage_path(const std::filesystem::path& path) {
#ifdef _WIN32
    // Leave room for the temporary file on clients without longPathAware.
    if (path.is_absolute() && path.native().size() >= 224) {
        const auto normalized = path.lexically_normal().make_preferred().native();
        if (normalized.starts_with(L"\\\\?\\")) return normalized;
        if (normalized.starts_with(L"\\\\")) return L"\\\\?\\UNC\\" + normalized.substr(2);
        return L"\\\\?\\" + normalized;
    }
#endif
    return path;
}

std::optional<std::string> read_bounded(const std::filesystem::path& path, std::size_t max_size) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return {};
    std::string bytes(max_size + 1, '\0');
    file.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (file.bad() || static_cast<std::size_t>(file.gcount()) > max_size) return {};
    bytes.resize(static_cast<std::size_t>(file.gcount()));
    return bytes;
}

std::optional<SettingsData> read_store(std::string_view bytes) {
    if (take(bytes, '\n') != "CRESTSTORE1") return {};
    SettingsData data;
    bool found_settings = false;
    while (!bytes.empty()) {
        auto header = take(bytes, '\n');
        const auto kind = take(header, ' ');
        const auto name = kind == "profile" ? hex_decode(take(header, ' ')) : std::optional<std::string>{};
        const auto length = parse_number<std::size_t>(header);
        if (!length || *length > max_profile_bytes || *length >= bytes.size() || bytes[*length] != '\n') return {};
        auto settings = Settings::import(bytes.substr(0, *length));
        if (!settings) return {};
        bytes.remove_prefix(*length + 1);
        if (kind == "current" && !found_settings) {
            data.settings = std::move(*settings);
            found_settings = true;
        } else if (kind == "profile" && name && valid_profile_name(*name) && data.profiles.size() < max_profiles) {
            if (!data.profiles.emplace(*name, std::move(*settings)).second) return {};
        } else return {};
    }
    if (!found_settings) return {};
    return data;
}

} // namespace

std::optional<double> parse_profile_number(std::string_view text) {
    // Older Apple libc++ toolchains provide only integral from_chars.
    std::istringstream input{std::string(text)};
    input.imbue(std::locale::classic());
    double value{};
    if (!(input >> std::noskipws >> value) || !input.eof() || !std::isfinite(value)) return {};
    return value;
}

std::string hex_encode(std::string_view text) {
    constexpr std::string_view alphabet = "0123456789abcdef";
    std::string result;
    result.reserve(text.size() * 2);
    for (unsigned char byte : text) {
        result.push_back(alphabet[byte >> 4]);
        result.push_back(alphabet[byte & 15]);
    }
    return result;
}

std::optional<std::string> hex_decode(std::string_view text) {
    if (text.size() % 2 != 0) return {};
    std::string result;
    result.reserve(text.size() / 2);
    for (std::size_t i = 0; i < text.size(); i += 2) {
        unsigned value{};
        const auto [end, ec] = std::from_chars(text.data() + i, text.data() + i + 2, value, 16);
        if (ec != std::errc{} || end != text.data() + i + 2) return {};
        result.push_back(static_cast<char>(value));
    }
    return result;
}

bool valid_profile_name(std::string_view name) {
    return !name.empty() && name.size() <= 32 &&
           std::none_of(name.begin(), name.end(), [](unsigned char c) { return control(c); });
}

Settings::Settings() {
    values_.reserve(std::size(schema));
    for (const auto& field : schema) {
        if (field.kind == Kind::BOOL) values_.emplace(field.key, field.initial == "true");
        else if (field.kind == Kind::NUMBER || field.kind == Kind::COLOR)
            values_.emplace(field.key, parse_profile_number(field.initial).value());
        else values_.emplace(field.key, std::string(field.initial));
    }
}

bool Settings::boolean(std::string_view key) const {
    const auto it = values_.find(key);
    const auto* value = it == values_.end() ? nullptr : std::get_if<bool>(&it->second);
    return value && *value;
}

double Settings::number(std::string_view key) const {
    const auto it = values_.find(key);
    const auto* value = it == values_.end() ? nullptr : std::get_if<double>(&it->second);
    return value ? *value : 0;
}

std::string_view Settings::text(std::string_view key) const {
    const auto it = values_.find(key);
    const auto* value = it == values_.end() ? nullptr : std::get_if<std::string>(&it->second);
    return value ? std::string_view(*value) : std::string_view{};
}

bool Settings::set(std::string_view key, bool value) {
    const auto it = values_.find(key);
    if (it == values_.end() || !std::holds_alternative<bool>(it->second)) return false;
    it->second = value;
    return true;
}

bool Settings::set_text(std::string_view key, std::string_view value) {
    const auto* field = definition(key);
    if (!field) return false;
    switch (field->kind) {
    case Kind::BOOL:
        return (value == "true" || value == "false") && set(key, value == "true");
    case Kind::NUMBER:
    case Kind::COLOR: {
        const auto number = parse_profile_number(value);
        if (!number) return false;
        const double clipped = std::clamp(*number, field->minimum, field->maximum);
        values_.at(key) = field->kind == Kind::COLOR ? std::floor(clipped) : clipped;
        return true;
    }
    case Kind::STRING:
        values_.at(key) = clean_text(value, static_cast<std::size_t>(field->maximum));
        return true;
    case Kind::ENUM:
        if (!matches_choice(field->choices, value)) return false;
        values_.at(key) = std::string(value);
        return true;
    }
    return false;
}

std::optional<Settings> Settings::import(std::string_view input) {
    if (input.size() > max_profile_bytes) return {};
    auto header = take(input, '\n');
    if (header.ends_with('\r')) header.remove_suffix(1);
    if (header != "CREST1") return {};
    Settings result;
    std::unordered_set<std::string_view> seen;
    std::size_t lines = 1;
    while (!input.empty()) {
        if (++lines > 512) return {};
        auto line = take(input, '\n');
        if (line.ends_with('\r')) line.remove_suffix(1);
        if (line.empty()) continue;
        if (line.starts_with("rule|")) {
            auto rule = parse_rule(line.substr(5));
            if (!rule || result.rules_.size() >= max_rules) return {};
            result.rules_.push_back(std::move(*rule));
        } else {
            if (line.find('=') == line.npos) return {};
            auto key = take(line, '=');
            const auto* field = definition(key);
            if (!field || !seen.insert(field->key).second) return {};
            if (field->kind == Kind::STRING) {
                const auto decoded = hex_decode(line);
                if (!decoded || !result.set_text(field->key, *decoded)) return {};
            } else if (!result.set_text(field->key, line)) return {};
        }
    }
    return result;
}

std::string Settings::export_text() const {
    std::string result = "CREST1";
    for (const auto& field : schema) {
        result += '\n';
        result += field.key;
        result += '=';
        if (field.kind == Kind::BOOL) result += boolean(field.key) ? "true" : "false";
        else if (field.kind == Kind::NUMBER || field.kind == Kind::COLOR) result += format_number(number(field.key));
        else if (field.kind == Kind::STRING) result += hex_encode(text(field.key));
        else result += text(field.key);
    }
    for (const auto& rule : rules_) {
        result += "\nrule|" + hex_encode(rule.name) + (rule.exact ? "|1|" : "|0|") + rule.kind;
        result += rule.enabled ? "|1" : "|0";
        result += rule.hide ? "|1|" : "|0|";
        result += std::to_string(rule.color) + "|" + format_number(rule.scale);
    }
    return result;
}

void SettingsStore::open(std::filesystem::path path, const std::filesystem::path& legacy) {
    file = std::move(path);
    data = {};
    error.clear();
    changed = false;
    if (file.empty()) return;
    std::error_code ec;
    const auto current_file = storage_path(file);
    const auto legacy_file = storage_path(legacy);
    if (std::filesystem::exists(current_file, ec) || ec) {
        const auto bytes = read_bounded(current_file, max_store_bytes);
        if (auto loaded = bytes ? read_store(*bytes) : std::nullopt) data = std::move(*loaded);
        else error = "Saved Crest settings could not be read.";
    } else if (!legacy_file.empty() && std::filesystem::exists(legacy_file, ec) && !ec) {
        constexpr std::size_t max_legacy_bytes = 1u << 20;
        const auto bytes = read_bounded(legacy_file, max_legacy_bytes);
        if (auto loaded = bytes ? import_legacy_settings(*bytes) : std::nullopt) {
            data = std::move(*loaded);
            changed = true;
        } else error = "Older Lua Crest settings could not be imported.";
    }
}

bool SettingsStore::save() {
    if (file.empty()) return false;
    if (!changed) return error.empty();
    const auto current_file = storage_path(file);
    auto temporary = current_file;
    std::random_device nonce;
    temporary += ".tmp." + std::to_string(nonce()) + "." + std::to_string(nonce());
    const auto write = [](std::ofstream& out, std::string_view header, const Settings& settings) {
        const auto text = settings.export_text();
        out << header << ' ' << text.size() << '\n' << text << '\n';
    };
    std::error_code ec;
    std::filesystem::create_directories(current_file.parent_path(), ec);
    if (!ec) {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        out << "CRESTSTORE1\n";
        write(out, "current", data.settings);
        for (const auto& [name, settings] : data.profiles) write(out, "profile " + hex_encode(name), settings);
        out.close();
        if (out) std::filesystem::rename(temporary, current_file, ec);
        else ec = std::make_error_code(std::errc::io_error);
    }
    if (ec) {
        std::error_code cleanup_error;
        std::filesystem::remove(temporary, cleanup_error);
        error = "Crest settings could not be saved.";
        return false;
    }
    error.clear();
    changed = false;
    return true;
}

} // namespace crest
