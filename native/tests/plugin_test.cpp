// SPDX-License-Identifier: MIT
#include "hailstone_plugin.h"

#include <array>
#include <cstddef>
#include <cstdint>
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
    std::vector<Submission> submitted;

    FakeHost() {
        api.abi = HAILSTONE_PLUGIN_ABI;
        api.size = sizeof(api);
        api.host = this;
        api.caps = [](void*) -> std::uint32_t {
            return HAILSTONE_CAP_READ | HAILSTONE_CAP_UI | HAILSTONE_CAP_EXTEND |
                   HAILSTONE_CAP_COMMANDS | HAILSTONE_CAP_PACKETS;
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
                require(model->commands && model->command_count && model->bounds.width > 0 && model->bounds.height > 0,
                        "a visible model submits real geometry");
                for (std::uint32_t index = 0; index < model->command_count; ++index) {
                    const auto& primitive = model->commands[index];
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

    void render(const HailstoneNameplateCandidate& actor) {
        require(provider != nullptr, "canvas provider was registered");
        measurements = 0;
        submitted.clear();
        const HailstoneNameplateFrame frame{sizeof(HailstoneNameplateFrame), 1, frame_id, 50, 16, 14, 3,
                                           {0, 0, 800, 600}, &actor};
        provider(provider_data, &frame);
    }

    void expect_labels(const std::vector<std::string>& labels) const {
        require(submitted.size() == 1 && submitted.front().flags == HAILSTONE_NAMEPLATE_CLICKABLE &&
                submitted.front().labels == labels, "visible labels match the configured display");
        require(measurements == labels.size(), "each submitted label used the host font metrics");
    }
};

void older_host_is_safe() {
    FakeHost fake;
    fake.api.size = offsetof(PluginHostApi, nameplate_register);
    hailstone_plugin_set_host(&fake.api);
    InitializePlugin();
    OnPulse();
    require(fake.command && !fake.provider && fake.registrations == 0, "older ABI 3 keeps the stock canvas");
    ShutdownPlugin();
    require(fake.unregistrations == 0, "older ABI 3 had nothing to unregister");
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
        replacement_commands_and_packets();
        std::cout << "Crest native plugin smoke test passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Crest native plugin smoke test: " << error.what() << '\n';
        return 1;
    }
}
