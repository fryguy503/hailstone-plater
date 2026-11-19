// SPDX-License-Identifier: MIT
#include "resource_observations.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <stdexcept>
#include <vector>

namespace {

constexpr std::uint16_t mana = 0x3791;
constexpr std::uint16_t endurance = 0x5f42;
using crest::ResourceActor;
using crest::ResourceObservations;

void require(bool value, const char* failure) {
    if (!value) throw std::runtime_error(failure);
}

std::array<std::byte, 10> update(std::uint32_t current, std::uint32_t maximum, std::uint16_t id) {
    std::array<std::byte, 10> packet{};
    for (std::size_t i = 0; i < 4; ++i) {
        packet[i] = static_cast<std::byte>((current >> (i * 8)) & 0xffu);
        packet[4 + i] = static_cast<std::byte>((maximum >> (i * 8)) & 0xffu);
    }
    packet[8] = static_cast<std::byte>(id & 0xffu);
    packet[9] = static_cast<std::byte>(id >> 8);
    return packet;
}

void tracked_updates_and_incarnation() {
    ResourceObservations readings;
    const ResourceActor actor{0xf123, 11};
    const ResourceActor reuse{actor.id, 12};
    const auto packet = update(25, 100, 0xf123);
    require(crest::points_actor_id(mana, packet) == actor.id, "could not read the full actor id");
    require(!readings.receive(mana, packet, {1, 11}), "packet attached to the wrong actor");
    require(!readings.receive(mana, packet, {actor.id, 0}), "packet without an established incarnation");
    require(readings.receive(mana, packet, actor), "valid mana update rejected");
    require(readings.receive(endurance, update(0, 100, 0xf123), actor), "known zero rejected");
    require(readings.values(actor).mana_percent == 25 && readings.values(actor).endurance_percent == 0,
            "independent resource pools or known zero lost");
    require(!readings.values(reuse).mana_percent && !readings.values(reuse).endurance_percent, "stale reuse");
    require(readings.receive(mana, packet, reuse), "new incarnation update rejected");
    require(!readings.values(actor).mana_percent && !readings.values(reuse).endurance_percent, "previous pools transferred");
    const std::array tracked{actor};
    readings.prune(tracked);
    require(!readings.values(reuse).mana_percent, "untracked incarnation left behind");
}

void invalid_observations_clear_only_the_matching_pool() {
    ResourceObservations readings;
    const ResourceActor actor{11, 7};
    const auto valid = update(50, 100, 11);
    require(readings.receive(endurance, valid, actor), "valid endurance update rejected");
    for (const auto& invalid : {update(1, 0, 11), update(0x7fffffffu, 0x80000000u, 11), update(101, 100, 11)}) {
        require(readings.receive(mana, valid, actor), "valid mana update rejected");
        require(!readings.receive(mana, invalid, actor), "invalid resource accepted");
        require(!readings.values(actor).mana_percent && readings.values(actor).endurance_percent == 50,
                "invalid mana either retained old value or cleared endurance");
    }
    require(readings.receive(mana, valid, actor), "valid mana update rejected");
    require(!crest::points_actor_id(0, valid) && !crest::points_actor_id(mana, update(0, 100, 0)),
            "unrelated update or zero id accepted");
    for (std::size_t size = 0; size < valid.size(); ++size)
        require(!readings.receive(mana, std::span(valid).first(size), actor), "truncated update accepted");
    require(readings.values(actor).mana_percent == 50, "truncation with no full id cleared the pool");
    std::vector oversized(valid.begin(), valid.end());
    oversized.push_back(std::byte{});
    require(!readings.receive(mana, oversized, actor) && !readings.values(actor).mana_percent,
            "oversized update with a complete id failed to invalidate the pool");
    readings.remove(actor.id);
    require(!readings.values(actor).endurance_percent, "removed actor left cached resources");
}

void bounded_entries_and_reset() {
    ResourceObservations readings;
    for (std::uint16_t id = 1; id <= 128; ++id)
        require(readings.receive(mana, update(1, 4, id), {id, id}), "valid capacity entry rejected");
    require(!readings.receive(mana, update(1, 4, 129), {129, 129}), "entry cap overrun");
    require(readings.receive(endurance, update(2, 4, 128), {128, 128}), "existing entry rejected at capacity");
    require(readings.values({128, 128}).mana_percent == 25 && readings.values({128, 128}).endurance_percent == 50,
            "cap changed existing observations");
    readings.reset();
    require(!readings.values({128, 128}).mana_percent && readings.receive(mana, update(1, 4, 129), {129, 129}),
            "reset did not clear the actor cache");
}

} // namespace

int main() {
    try {
        tracked_updates_and_incarnation();
        invalid_observations_clear_only_the_matching_pool();
        bounded_entries_and_reset();
        std::cout << "Crest resource observation tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Crest resource observation test failed: " << error.what() << '\n';
        return 1;
    }
}
