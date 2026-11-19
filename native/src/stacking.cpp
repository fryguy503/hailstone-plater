// SPDX-License-Identifier: MIT
#include "stacking.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <tuple>
#include <vector>

namespace crest {
namespace {

constexpr std::size_t max_records = 4096;
constexpr std::size_t max_movable = 256;
constexpr double max_viewport = 131072;

struct Interval {
    double low;
    double high;
};

double bounded(double value, double low, double high, double fallback) {
    return std::isfinite(value) ? std::clamp(value, low, high) : fallback;
}

bool valid_bounds(const StackBounds& bounds) {
    return std::isfinite(bounds.x) && std::isfinite(bounds.y) && std::isfinite(bounds.width) &&
           std::isfinite(bounds.height) && bounds.width >= 0 && bounds.height >= 0 &&
           std::isfinite(bounds.x + bounds.width) && std::isfinite(bounds.y + bounds.height);
}

StackBounds projected(const StackPlate& plate) {
    return {plate.x + plate.bounds.x, plate.y + plate.bounds.y, plate.bounds.width, plate.bounds.height};
}

bool outside(const StackBounds& plate, const StackBounds& viewport) {
    return plate.x + plate.width < viewport.x || plate.x > viewport.x + viewport.width ||
           plate.y + plate.height < viewport.y || plate.y > viewport.y + viewport.height;
}

double placed_top(const StackBounds& plate, double gap, const std::vector<StackBounds>& blockers,
                  std::vector<Interval>& intervals) {
    intervals.clear();
    for (const auto& blocker : blockers) {
        if (plate.x < blocker.x + blocker.width + gap && blocker.x < plate.x + plate.width + gap) {
            const double low = blocker.y - gap - plate.height;
            // A forbidden interval starting at or below the current top cannot force an upward move.
            if (low < plate.y) intervals.push_back({low, blocker.y + blocker.height + gap});
        }
    }
    if (intervals.empty()) return plate.y;
    std::sort(intervals.begin(), intervals.end(), [](const auto& a, const auto& b) { return a.low < b.low; });

    // Bounds touching at the requested gap are valid, so intervals are open.
    auto merged = intervals.front();
    for (const auto& next : std::span(intervals).subspan(1)) {
        if (next.low < merged.high) {
            merged.high = std::max(merged.high, next.high);
        } else {
            if (plate.y > merged.low && plate.y < merged.high) return merged.low;
            merged = next;
        }
    }
    return plate.y > merged.low && plate.y < merged.high ? merged.low : plate.y;
}

} // namespace

void stack_plates(std::span<StackPlate> plates, StackBounds viewport, const std::function<bool()>& exhausted) {
    viewport.x = bounded(viewport.x, -max_viewport, max_viewport, 0);
    viewport.y = bounded(viewport.y, -max_viewport, max_viewport, 0);
    viewport.width = bounded(viewport.width, 0, max_viewport, max_viewport);
    viewport.height = bounded(viewport.height, 0, max_viewport, max_viewport);

    const auto count = std::min(plates.size(), max_records);
    std::vector<std::size_t> order;
    std::vector<StackBounds> blockers;
    order.reserve(count);
    blockers.reserve(count);
    for (std::size_t index = 0; index < plates.size(); ++index) {
        auto& plate = plates[index];
        plate.offset_y = 0;
        const auto bounds = projected(plate);
        plate.hidden = index >= count || plate.hide || !valid_bounds(plate.bounds) || !valid_bounds(bounds);
        if (plate.hidden) continue;
        if (plate.target || !plate.stack) blockers.push_back(bounds);
        else order.push_back(index);
    }

    const auto priority = [&](std::size_t index) {
        const auto& plate = plates[index];
        const auto depth = std::isfinite(plate.depth) ? plate.depth : std::numeric_limits<double>::infinity();
        return std::tuple(depth, plate.id, index);
    };
    std::sort(order.begin(), order.end(), [&](auto a, auto b) { return priority(a) < priority(b); });

    std::vector<Interval> intervals;
    intervals.reserve(std::min(count, blockers.size() + max_movable));
    std::size_t next = 0;
    const auto movable_count = std::min(order.size(), max_movable);
    for (; next < movable_count; ++next) {
        if (exhausted && exhausted()) break;
        auto& plate = plates[order[next]];
        auto bounds = projected(plate);
        if (outside(bounds, viewport)) {
            plate.hidden = true;
            continue;
        }

        const auto gap = bounded(plate.gap, 0, 80, 4);
        const auto limit = bounded(plate.max_offset, 0, 800, 96);
        const auto top = placed_top(bounds, gap, blockers, intervals);
        plate.offset_y = top - bounds.y;
        bounds.y = top;
        plate.hidden = -plate.offset_y > limit || bounds.y < viewport.y;
        if (!plate.hidden) blockers.push_back(bounds);
    }
    for (; next < order.size(); ++next) plates[order[next]].hidden = true;
}

} // namespace crest
