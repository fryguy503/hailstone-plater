// SPDX-License-Identifier: MIT
#include "stacking.hpp"

#include <array>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <source_location>
#include <string_view>
#include <vector>

namespace {

constexpr crest::StackBounds viewport{0, 0, 800, 600};

void check(bool condition, std::string_view message,
           std::source_location location = std::source_location::current()) {
    if (condition) return;
    std::cerr << location.file_name() << ':' << location.line() << ": " << message << '\n';
    std::exit(EXIT_FAILURE);
}

crest::StackPlate plate(std::uint32_t id, double depth, double y = 400) {
    return {.bounds = {-40, -20, 80, 20}, .x = 400, .y = y, .depth = depth, .id = id};
}

void target_and_depth_priority() {
    std::array records{plate(9, 20), plate(5, 10), plate(2, 10), plate(99, 50)};
    records[3].target = true;
    crest::stack_plates(records, viewport);
    check(records[3].offset_y == 0, "the target stays pinned even at a farther depth");
    check(records[2].offset_y == -24 && records[1].offset_y == -48 && records[0].offset_y == -72,
          "movable plates resolve by nearest depth and then lower actor id");
    check(!records[0].hidden && !records[1].hidden && !records[2].hidden && !records[3].hidden,
          "all four plates fit");

    std::array reversed{records[3], records[2], records[1], records[0]};
    crest::stack_plates(reversed, viewport);
    for (std::size_t i = 0; i < records.size(); ++i) {
        check(records[i].offset_y == reversed[records.size() - 1 - i].offset_y,
              "input enumeration does not change placement across distinct actors");
    }

    std::array ties{plate(2, 0), plate(2, 0)};
    crest::stack_plates(ties, viewport);
    check(ties[0].offset_y == 0 && ties[1].offset_y == -24, "exact ties respect input order");
}

void connected_intervals_and_limits() {
    std::array records{plate(1, 1), plate(2, 2, 375), plate(3, 3)};
    records[0].stack = records[1].stack = false;
    crest::stack_plates(records, viewport);
    check(records[0].offset_y == 0 && records[1].offset_y == 0, "opt-out plates never move");
    check(!records[2].hidden && records[2].offset_y == -49, "placement crosses connected obstructions");
    records[2].max_offset = 24;
    crest::stack_plates(records, viewport);
    check(records[2].hidden, "placement exceeding the allowed displacement is hidden");
    records[2].max_offset = 200;
    records[2].y = 10;
    crest::stack_plates(records, viewport);
    check(records[2].hidden, "placement crossing the viewport top is hidden");

    std::array touching{plate(1, 1, 396), plate(2, 2, 444), plate(3, 3, 420)};
    touching[0].stack = touching[1].stack = false;
    crest::stack_plates(touching, viewport);
    check(!touching[2].hidden && touching[2].offset_y == 0, "exactly touching intervals permit the requested gap");
    touching[1].y -= 1;
    crest::stack_plates(touching, viewport);
    check(!touching[2].hidden && touching[2].offset_y == -48, "one pixel of overlap connects the intervals");
}

void bounds_caps_and_frame_reset() {
    std::vector<crest::StackPlate> records;
    for (std::uint32_t id = 1; id <= 300; ++id) {
        auto item = plate(id, id);
        item.x = 100 * id;
        records.push_back(item);
    }
    crest::stack_plates(records, {0, 0, 50000, 1000});
    for (std::size_t i = 0; i < records.size(); ++i) {
        check(records[i].hidden == (i >= 256), "only the first 256 movable priorities are placed");
    }

    auto& last = records.back();
    last.stack = false;
    last.offset_y = -500;
    crest::stack_plates(std::span(&last, 1), viewport);
    check(!last.hidden && last.offset_y == 0, "the next frame resets output before pinning the plate");
    last.stack = true;
    crest::stack_plates(std::span(&last, 1), viewport);
    check(last.hidden, "a movable plate outside the viewport is hidden");

    std::vector oversized(4097, plate(1, 1));
    for (auto& item : oversized) item.stack = false;
    crest::stack_plates(oversized, viewport);
    check(!oversized[4095].hidden && oversized[4096].hidden, "total records are capped even for pinned plates");

    std::array malformed{plate(1, 1), plate(2, 2), plate(3, 3), plate(4, 4)};
    malformed[0].x = std::numeric_limits<double>::quiet_NaN();
    malformed[1].bounds.width = -1;
    malformed[2].hide = malformed[2].target = true;
    malformed[3].bounds.width = std::numeric_limits<double>::infinity();
    crest::stack_plates(malformed, viewport);
    for (const auto& item : malformed) check(item.hidden, "invalid or explicitly hidden records emit no plate");
}

void deadline_preserves_placed_plates() {
    std::array records{plate(4, 20), plate(2, 2), plate(1, 1), plate(3, 10)};
    records[2].target = true;
    unsigned checks = 0;
    crest::stack_plates(records, viewport, [&] { return ++checks > 1; });
    check(checks == 2, "the deadline check stops after expiring");
    check(records[0].hidden && records[3].hidden, "remaining movable records are hidden");
    check(!records[1].hidden && records[1].offset_y == -24, "already placed nearest plate remains");
    check(!records[2].hidden && records[2].offset_y == 0, "target remains pinned");
}

} // namespace

int main() {
    target_and_depth_priority();
    connected_intervals_and_limits();
    bounds_caps_and_frame_reset();
    deadline_preserves_placed_plates();
    std::cout << "Crest native stacking tests passed\n";
}
