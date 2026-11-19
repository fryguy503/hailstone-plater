// SPDX-License-Identifier: MIT
#include "hailstone_plugin.h"
#include "settings.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

extern "C" std::uint32_t hailstone_plugin_abi();
extern "C" void hailstone_plugin_set_host(const PluginHostApi* api);
extern "C" void InitializePlugin();
extern "C" void ShutdownPlugin();
extern "C" void OnPulse();
extern "C" void OnBeginZone();
extern "C" int OnPacket(int stream, int direction, HailstonePacket* packet);

struct HailstoneArgs { std::vector<HailstoneValue> values; };
struct HailstoneOut {
    std::string key;
    std::map<std::string, HailstoneValue> fields;
    std::map<std::string, std::string> texts;
};

namespace {

void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

struct Submission {
    std::uint32_t flags;
    std::vector<std::string> labels;
};

struct FakeHost {
    static constexpr std::uint64_t frame_id = 17;
    PluginHostApi api{};
    HailstoneNameplateFn provider = nullptr;
    HailstoneCommandFn command = nullptr;
    void* provider_data = nullptr;
    void* command_data = nullptr;
    unsigned registrations = 0;
    unsigned unregistrations = 0;
    unsigned measurements = 0;
    unsigned warnings = 0;
    unsigned option_registrations = 0, menu_registrations = 0;
    HailstoneOptionGetFn option_get = nullptr;
    HailstoneOptionSetFn option_set = nullptr;
    std::uint32_t caps = HAILSTONE_CAP_READ | HAILSTONE_CAP_UI | HAILSTONE_CAP_EXTEND |
                         HAILSTONE_CAP_COMMANDS | HAILSTONE_CAP_PACKETS;
    std::vector<std::string> chat;
    std::map<std::string, HailstoneUiFn> ui_functions;
    std::vector<Submission> submitted;

    FakeHost() {
        api.abi = HAILSTONE_PLUGIN_ABI;
        api.size = sizeof(api);
        api.host = this;
        api.caps = [](void* data) { return static_cast<FakeHost*>(data)->caps; };
        api.log = [](void* data, int level, const char*) {
            if (level == 2) ++static_cast<FakeHost*>(data)->warnings;
        };
        api.write_chat = [](void* data, const char* text, std::uint32_t) {
            static_cast<FakeHost*>(data)->chat.emplace_back(text);
        };
        api.add_command = [](void* data, const char* name, HailstoneCommandFn fn, void* user) {
            auto& self = *static_cast<FakeHost*>(data);
            require(std::string_view(name) == "/crest" && fn, "native command registration");
            self.command = fn;
            self.command_data = user;
            return 1;
        };
        api.add_option = [](void* data, const char* name, const char*, int min, int max,
                            HailstoneOptionGetFn get, HailstoneOptionSetFn set, void*) {
            auto& self = *static_cast<FakeHost*>(data);
            require(std::string_view(name) == "enabled" && min == 0 && max == 1 && get && set,
                    "the native options panel exposes an enable switch");
            ++self.option_registrations;
            self.option_get = get;
            self.option_set = set;
            return 1;
        };
        api.add_menu = [](void* data, const char* label, const char* action) {
            require(std::string_view(label) == "Crest" && std::string_view(action) == "/crest",
                    "the Plugins menu opens Crest settings");
            ++static_cast<FakeHost*>(data)->menu_registrations;
            return 1;
        };
        api.add_ui_function = [](void* data, const char* name, HailstoneUiFn function, void*) {
            static_cast<FakeHost*>(data)->ui_functions[name] = function;
            return 1;
        };
        api.arg = [](void*, const HailstoneArgs* args, std::size_t i, HailstoneValue* value) {
            if (i >= args->values.size()) return 0;
            *value = args->values[i];
            return 1;
        };
        api.out_begin_table = [](void*, HailstoneOut*) {};
        api.out_end = [](void*, HailstoneOut*) {};
        api.out_key = [](void*, HailstoneOut* out, const char* key) { out->key = key; };
        api.out_value = [](void*, HailstoneOut* out, const HailstoneValue* value) {
            out->fields[out->key] = *value;
            if (value->kind == HAILSTONE_STRING && value->s) out->texts[out->key] = value->s;
        };
        api.nameplate_register = [](void* data, HailstoneNameplateFn fn, void* user) {
            auto& self = *static_cast<FakeHost*>(data);
            require(fn && !self.provider, "exactly one active native provider");
            ++self.registrations;
            self.provider = fn;
            self.provider_data = user;
            return 1;
        };
        api.nameplate_unregister = [](void* data) {
            auto& self = *static_cast<FakeHost*>(data);
            require(self.provider != nullptr, "unregister requires a provider");
            ++self.unregistrations;
            self.provider = nullptr;
            self.provider_data = nullptr;
            return 1;
        };
        api.nameplate_measure_text = [](void* data, const char* text, std::uint32_t length, float font_size,
                                       std::int32_t font, HailstoneNameplateTextMetrics* result) {
            auto& self = *static_cast<FakeHost*>(data);
            require(text && length && font_size >= 4 && font == 3 && result && result->size == sizeof(*result),
                    "valid borrowed text measurement");
            ++self.measurements;
            const float width = static_cast<float>(length) * font_size / 2;
            *result = {sizeof(*result), width, font_size, 0, 0, width, font_size};
            return 1;
        };
        api.nameplate_spell_icon = [](void*, std::uint32_t, HailstoneNameplateImage*) { return 0; };
        api.nameplate_submit = [](void* data, const HailstoneNameplateModel* model) {
            auto& self = *static_cast<FakeHost*>(data);
            require(model && model->size == sizeof(*model) && model->frame == frame_id &&
                    model->candidate_index == self.submitted.size(), "model identifies the current frame and candidate");
            Submission result{model->flags, {}};
            if (!(model->flags & HAILSTONE_NAMEPLATE_HIDE)) {
                require(model->commands && model->command_count && model->bounds.width > 0 && model->bounds.height > 0 &&
                        model->command_size == sizeof(HailstoneNameplateCommand),
                        "a visible model submits real geometry");
                const auto* records = static_cast<const std::byte*>(model->commands);
                for (std::uint32_t index = 0; index < model->command_count; ++index) {
                    HailstoneNameplateCommand primitive;
                    std::memcpy(&primitive, records + static_cast<std::size_t>(index) * model->command_size,
                                sizeof(primitive));
                    if (primitive.kind == HAILSTONE_NAMEPLATE_TEXT) {
                        require(primitive.text.data && primitive.text.bytes > 0, "valid borrowed command label");
                        if (primitive.color != 0)
                            result.labels.emplace_back(primitive.text.data, primitive.text.bytes);
                    }
                }
            }
            self.submitted.push_back(std::move(result));
            return 1;
        };
    }

    void run_command(const char* argument) const {
        require(command != nullptr, "command was registered");
        command(command_data, argument);
    }

    void render_frame(const void* actors, std::uint32_t count, std::uint32_t stride) {
        require(provider != nullptr, "canvas provider was registered");
        measurements = 0;
        submitted.clear();
        const HailstoneNameplateFrame frame{sizeof(HailstoneNameplateFrame), count, stride, 0, frame_id,
                                           50, 16, 14, 3, {0, 0, 800, 600}, actors};
        provider(provider_data, &frame);
    }

    void render(const HailstoneNameplateCandidate& actor) {
        render_frame(&actor, 1, sizeof(actor));
    }

    void expect_labels(std::vector<std::string> labels) const {
        auto observed = submitted.empty() ? std::vector<std::string>{} : submitted.front().labels;
        std::sort(labels.begin(), labels.end());
        std::sort(observed.begin(), observed.end());
        require(submitted.size() == 1 && submitted.front().flags == HAILSTONE_NAMEPLATE_CLICKABLE &&
                observed == labels, "visible labels match the configured display");
        require(measurements >= labels.size(), "submitted labels used the host font metrics");
    }
};

void older_host_is_safe() {
    for (const auto host_size : {offsetof(PluginHostApi, nameplate_register),
                                 offsetof(PluginHostApi, nameplate_spell_icon)}) {
        FakeHost fake;
        fake.api.size = static_cast<std::uint32_t>(host_size);
        hailstone_plugin_set_host(&fake.api);
        InitializePlugin();
        OnPulse();
        OnPulse();
        require(fake.command && !fake.provider && fake.registrations == 0, "older ABI 3 keeps the stock canvas");
        require(fake.warnings == 1, "missing canvas SDK only warns once per load");
        ShutdownPlugin();
        require(fake.unregistrations == 0, "older ABI 3 had nothing to unregister");
        hailstone_plugin_set_host(nullptr);
    }
}

void extended_candidate_stride() {
    FakeHost fake;
    hailstone_plugin_set_host(&fake.api);
    InitializePlugin();

    struct FutureCandidate {
        HailstoneNameplateCandidate known;
        std::array<std::uint64_t, 4> future;
    };
    std::array<FutureCandidate, 2> actors{};
    actors[0].known.flags = HAILSTONE_NAMEPLATE_NPC | HAILSTONE_NAMEPLATE_TARGET;
    actors[0].known.spawn_id = 42;
    actors[0].known.incarnation = 17;
    actors[0].known.caption = {"First", 5};
    actors[0].known.x = 200;
    actors[0].known.y = 400;
    actors[0].known.depth = 10;
    actors[0].known.native_scale = 1;
    actors[0].future.fill(UINT64_MAX);
    actors[1].known = actors[0].known;
    actors[1].known.flags = HAILSTONE_NAMEPLATE_NPC;
    actors[1].known.spawn_id = 43;
    actors[1].known.incarnation = 18;
    actors[1].known.caption = {"Second", 6};
    actors[1].known.x = 600;
    fake.render_frame(actors.data(), static_cast<std::uint32_t>(actors.size()), sizeof(FutureCandidate));
    require(fake.submitted.size() == 2 && fake.submitted[0].labels == std::vector<std::string>{"First"} &&
            fake.submitted[1].labels == std::vector<std::string>{"Second"}, "step over extended records by the host stride");
    fake.render_frame(actors.data(), static_cast<std::uint32_t>(actors.size()), 1);
    require(fake.submitted.empty() && fake.measurements == 0, "missing candidate fields leave the stock canvas alone");

    ShutdownPlugin();
    hailstone_plugin_set_host(nullptr);
}

void replacement_commands_and_packets() {
    FakeHost fake;
    hailstone_plugin_set_host(&fake.api);
    InitializePlugin();
    OnPulse();
    require(fake.registrations == 1, "pulse does not register a duplicate provider");

    HailstoneNameplateCandidate actor{};
    actor.flags = HAILSTONE_NAMEPLATE_NPC | HAILSTONE_NAMEPLATE_TARGET | HAILSTONE_NAMEPLATE_HP_KNOWN |
                  HAILSTONE_NAMEPLATE_CONCEALED | HAILSTONE_NAMEPLATE_INVISIBLE | HAILSTONE_NAMEPLATE_INVISIBLE_UNDEAD;
    actor.spawn_id = 42;
    actor.incarnation = 17;
    actor.caption = {"Crypt Guardian", 14};
    actor.x = 400;
    actor.y = 400;
    actor.depth = 10;
    actor.native_scale = 1;
    actor.distance = 10;
    actor.hp_percent = 80;
    fake.render(actor);
    fake.expect_labels({"80%", "(Crypt Guardian)", "INV+IVU"});

    fake.run_command("parens");
    fake.render(actor);
    fake.expect_labels({"80%", "(Crypt Guardian)"});
    actor.flags &= ~(HAILSTONE_NAMEPLATE_CONCEALED | HAILSTONE_NAMEPLATE_INVISIBLE | HAILSTONE_NAMEPLATE_INVISIBLE_UNDEAD);
    fake.render(actor);
    fake.expect_labels({"80%", "Crypt Guardian"});

    fake.run_command("parens-tags");
    // Complete concealment snapshot for spawn 42, type IVU. The packet is only observed.
    std::array<std::uint8_t, 20> bytes{0x31, 0x43, 0x50, 0x4e, 1, 1, 1, 0, 1, 0, 0, 0,
                                      0, 0, 1, 0, 42, 0, 2, 0};
    const auto original = bytes;
    HailstonePacket packet{0x7d1d, bytes.data(), bytes.size()};
    require(OnPacket(HAILSTONE_STREAM_ZONE, HAILSTONE_OUT, &packet) == 0, "outbound packet passes through");
    fake.render(actor);
    fake.expect_labels({"80%", "Crypt Guardian"});
    require(OnPacket(HAILSTONE_STREAM_ZONE, HAILSTONE_IN, &packet) == 0 && bytes == original &&
            packet.opcode == 0x7d1d && packet.size == bytes.size() && packet.data == bytes.data(),
            "inbound observation leaves the packet untouched");
    fake.render(actor);
    fake.expect_labels({"80%", "(Crypt Guardian)", "IVU"});

    // A full same-ID replacement does not have to emit a separate despawn.
    ++actor.incarnation;
    fake.render(actor);
    fake.expect_labels({"80%", "Crypt Guardian"});
    bytes[8] = 2;
    OnPacket(HAILSTONE_STREAM_ZONE, HAILSTONE_IN, &packet);
    fake.render(actor);
    fake.expect_labels({"80%", "(Crypt Guardian)", "IVU"});

    fake.caps &= ~HAILSTONE_CAP_PACKETS;
    OnPulse();
    fake.render(actor);
    fake.expect_labels({"80%", "Crypt Guardian"});
    fake.caps |= HAILSTONE_CAP_PACKETS;
    OnPulse();
    fake.render(actor);
    fake.expect_labels({"80%", "Crypt Guardian"});

    OnBeginZone();
    OnPacket(HAILSTONE_STREAM_ZONE, HAILSTONE_IN, &packet);
    fake.render(actor);
    fake.expect_labels({"80%", "(Crypt Guardian)", "IVU"});
    OnBeginZone();
    fake.render(actor);
    fake.expect_labels({"80%", "Crypt Guardian"});

    fake.run_command("off");
    fake.render(actor);
    require(fake.submitted.empty() && fake.measurements == 0, "disabled callback leaves stock nameplates alone");
    OnPulse();
    require(fake.unregistrations == 1 && !fake.provider, "disabled provider releases registration");
    fake.run_command("on");
    OnPulse();
    require(fake.registrations == 2 && fake.provider, "re-enable registers the provider again");
    ShutdownPlugin();
    require(fake.unregistrations == 2 && !fake.provider, "plugin shutdown releases the provider");
    hailstone_plugin_set_host(nullptr);
}

void controls_are_visible_and_share_state() {
    FakeHost fake;
    hailstone_plugin_set_host(&fake.api);
    InitializePlugin();
    OnPulse();
    require(fake.option_registrations == 1 && fake.menu_registrations == 1,
            "Crest appears in Plugins options and the Plugins menu once");
    const auto settings = [&] {
        HailstoneOut out;
        require(fake.ui_functions.at("settings")(nullptr, nullptr, &out) == 1, "the settings UI can read state");
        return out.fields;
    };
    const auto set = [&](const char* key, int kind, std::int64_t value) {
        const HailstoneArgs args{{{HAILSTONE_STRING, 0, 0, key}, {kind, value, 0, nullptr}}};
        HailstoneOut out;
        fake.ui_functions.at("set_setting")(nullptr, &args, &out);
        return out.fields.at("").i != 0;
    };
    const auto config = [&] {
        HailstoneOut out;
        fake.ui_functions.at("get_config")(nullptr, nullptr, &out);
        return crest::Settings::import(out.texts.at(""));
    };
    const auto replace = [&](const char* text) {
        const HailstoneArgs args{{{HAILSTONE_STRING, 0, 0, text}}};
        HailstoneOut out;
        fake.ui_functions.at("replace_settings")(nullptr, &args, &out);
        return out.fields.at("").i != 0;
    };
    require(!settings().at("open").i, "the settings window starts closed");
    fake.run_command("");
    require(settings().at("open").i && settings().at("available").i && !fake.chat.empty(),
            "bare /crest opens the window and reports status in chat");
    const auto original_revision = settings().at("revision").i;
    require(replace("CREST1\nconcealment_display=Name suffix\nwidth=260\ncon_name_color=true\nlevel_position=below"),
            "valid full settings are accepted");
    require(!replace("CREST1\nconcealment_display=Invalid"), "invalid settings are rejected as a unit");
    fake.run_command("status");
    const auto updated = config();
    require(updated && updated->number("width") == 260 && updated->boolean("con_name_color") &&
            updated->text("level_position") == "below" && settings().at("revision").i > original_revision &&
            fake.chat.back().find("Name suffix") != std::string::npos, "the whole panel and slash commands share settings");
    require(set("open", HAILSTONE_BOOL, 0), "settings UI can close the window");
    require(!settings().at("open").i, "closing the settings window clears native open state");
    fake.option_set(nullptr, 0);
    OnPulse();
    require(!settings().at("enabled").i && !fake.provider, "the Plugins toggle releases the provider");
    require(replace("CREST1\nenabled=true"), "settings window can re-enable the renderer");
    OnPulse();
    require(fake.option_get(nullptr) == 1 && fake.provider, "the settings window and Plugins toggle share state");
    fake.run_command("help");
    require(fake.chat.back().find("/crest on") != std::string::npos, "command help goes to chat");
    ShutdownPlugin();
    hailstone_plugin_set_host(nullptr);
}

} // namespace

int main() {
    try {
        require(hailstone_plugin_abi() == HAILSTONE_PLUGIN_ABI, "plugin reports its public SDK ABI");
        older_host_is_safe();
        extended_candidate_stride();
        replacement_commands_and_packets();
        controls_are_visible_and_share_state();
        std::cout << "Crest native plugin smoke test passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Crest native plugin smoke test: " << error.what() << '\n';
        return 1;
    }
}
