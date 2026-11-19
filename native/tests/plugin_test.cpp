// SPDX-License-Identifier: MIT
#include "hailstone_plugin.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
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
    std::uint32_t caps = HAILSTONE_CAP_READ | HAILSTONE_CAP_UI | HAILSTONE_CAP_EXTEND |
                         HAILSTONE_CAP_COMMANDS | HAILSTONE_CAP_PACKETS;
    std::vector<Submission> submitted;

    FakeHost() {
        api.abi = HAILSTONE_PLUGIN_ABI;
        api.size = sizeof(api);
        api.host = this;
        api.caps = [](void* data) { return static_cast<FakeHost*>(data)->caps; };
        api.log = [](void* data, int level, const char*) {
            if (level == 2) ++static_cast<FakeHost*>(data)->warnings;
        };
        api.add_command = [](void* data, const char* name, HailstoneCommandFn fn, void* user) {
            auto& self = *static_cast<FakeHost*>(data);
            require(std::string_view(name) == "/crestnative" && fn, "native command registration");
            self.command = fn;
            self.command_data = user;
            return 1;
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

    void expect_labels(const std::vector<std::string>& labels) const {
        require(submitted.size() == 1 && submitted.front().flags == HAILSTONE_NAMEPLATE_CLICKABLE &&
                submitted.front().labels == labels, "visible labels match the configured display");
        require(measurements == labels.size(), "each submitted label used the host font metrics");
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

} // namespace

int main() {
    try {
        require(hailstone_plugin_abi() == HAILSTONE_PLUGIN_ABI, "plugin reports its public SDK ABI");
        older_host_is_safe();
        extended_candidate_stride();
        replacement_commands_and_packets();
        std::cout << "Crest native plugin smoke test passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Crest native plugin smoke test: " << error.what() << '\n';
        return 1;
    }
}
