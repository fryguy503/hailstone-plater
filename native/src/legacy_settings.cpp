// SPDX-License-Identifier: MIT
#include "settings.hpp"

#include <utility>

namespace crest {
namespace {

struct Pickled {
    enum class Kind { Scalar, Text, Table };
    Kind kind = Kind::Scalar;
    std::string value;
    std::map<std::string, Pickled, std::less<>> members;

    const Pickled* field(std::string_view key) const {
        const auto found = members.find(key);
        return found == members.end() ? nullptr : &found->second;
    }

    std::string_view scalar(std::string_view key, std::string_view fallback = {}) const {
        const auto* node = field(key);
        return node && node->kind != Kind::Table ? node->value : fallback;
    }
};

// Read only the table and scalar syntax written by mq.pickle. Profiles cannot
// run Lua code, and both nesting and the number of records are bounded.
class PickleReader {
public:
    explicit PickleReader(std::string_view bytes) : input_(bytes) {}

    std::optional<Pickled> read() {
        if (!consume("return")) return {};
        auto table = value(0);
        space();
        if (!table || table->kind != Pickled::Kind::Table || !input_.empty()) return {};
        return table;
    }

private:
    std::string_view input_;
    std::size_t nodes_ = 0;

    static bool digit(char c) { return c >= '0' && c <= '9'; }

    void space() {
        const auto next = input_.find_first_not_of(" \t\r\n");
        input_.remove_prefix(next == input_.npos ? input_.size() : next);
    }

    bool consume(std::string_view text) {
        space();
        if (!input_.starts_with(text)) return false;
        input_.remove_prefix(text.size());
        return true;
    }

    std::optional<std::string> quoted() {
        if (!consume("\"")) return {};
        std::string result;
        while (!input_.empty()) {
            auto c = input_.front();
            input_.remove_prefix(1);
            if (c == '"') return result;
            if (c != '\\') {
                result.push_back(c);
                continue;
            }
            if (input_.empty()) return {};
            c = input_.front();
            input_.remove_prefix(1);
            if (digit(c)) {
                unsigned byte = static_cast<unsigned>(c - '0');
                for (int n = 1; n < 3 && !input_.empty() && digit(input_.front()); ++n) {
                    byte = byte * 10 + static_cast<unsigned>(input_.front() - '0');
                    input_.remove_prefix(1);
                }
                if (byte > 255) return {};
                result.push_back(static_cast<char>(byte));
            } else if (c == 'x' && input_.size() >= 2) {
                const auto byte = hex_decode(input_.substr(0, 2));
                if (!byte) return {};
                result += *byte;
                input_.remove_prefix(2);
            } else if (c == 'n' || c == '\n') result.push_back('\n');
            else if (c == 'r') result.push_back('\r');
            else if (c == 't') result.push_back('\t');
            else if (c == '\\' || c == '"') result.push_back(c);
            else return {};
        }
        return {};
    }

    std::optional<Pickled> value(unsigned depth) {
        if (++nodes_ > 16384 || depth > 6) return {};
        Pickled result;
        if (consume("{")) {
            result.kind = Pickled::Kind::Table;
            while (!consume("}")) {
                if (!consume("[")) return {};
                auto key = quoted();
                if (!key) {
                    std::size_t digits = 0;
                    while (digits < input_.size() && digit(input_[digits])) ++digits;
                    if (!digits) return {};
                    key = std::string(input_.substr(0, digits));
                    input_.remove_prefix(digits);
                }
                if (!consume("]") || !consume("=")) return {};
                auto child = value(depth + 1);
                if (!child || !result.members.emplace(std::move(*key), std::move(*child)).second) return {};
                if (!consume(",")) {
                    if (!consume("}")) return {};
                    break;
                }
            }
            return result;
        }
        space();
        if (input_.starts_with('"')) {
            auto text = quoted();
            if (!text) return {};
            result.kind = Pickled::Kind::Text;
            result.value = std::move(*text);
            return result;
        }
        const auto length = input_.find_first_of(" \t\r\n,}");
        auto token = input_.substr(0, length);
        if (token.empty()) return {};
        if (token != "true" && token != "false" && token != "nil" && !parse_profile_number(token)) return {};
        result.value = token;
        input_.remove_prefix(token.size());
        return result;
    }
};

Settings convert(const Pickled& table) {
    Settings settings;
    for (const auto& [key, entry] : table.members)
        if (key != "rules" && entry.kind != Pickled::Kind::Table)
            settings.set_text(key, entry.value);
    const auto* rules = table.field("rules");
    if (!rules) return settings;
    auto text = settings.export_text();
    for (std::size_t index = 1; index <= max_rules; ++index) {
        const auto* rule = rules->field(std::to_string(index));
        if (!rule || rule->scalar("name").empty()) continue;
        const auto flag = [&](std::string_view key, bool fallback = false) {
            return rule->scalar(key, fallback ? "true" : "false") == "true" ? "1" : "0";
        };
        auto kind = rule->scalar("kind");
        if (kind != "npc" && kind != "player" && kind != "pet") kind = "any";
        std::string record = "\nrule|" + hex_encode(rule->scalar("name")) + "|" + flag("exact") + "|";
        record += kind;
        record += std::string("|") + flag("enabled", true) + "|" + flag("hide") + "|";
        record += rule->scalar("color", "15053908");
        record += '|';
        record += rule->scalar("scale", "1");
        if (Settings::import("CREST1" + record)) text += record;
    }
    return Settings::import(text).value_or(std::move(settings));
}

} // namespace

std::optional<SettingsData> import_legacy_settings(std::string_view text) {
    auto table = PickleReader(text).read();
    if (!table) return {};
    const auto* settings = table->field("settings");
    if (!settings || settings->kind != Pickled::Kind::Table) return {};
    SettingsData result;
    result.settings = convert(*settings);
    if (const auto* profiles = table->field("profiles")) {
        for (const auto& [key, node] : profiles->members) {
            if (result.profiles.size() >= max_profiles) break;
            if (valid_profile_name(key) && node.kind == Pickled::Kind::Table)
                result.profiles.emplace(key, convert(node));
        }
    }
    return result;
}

} // namespace crest
