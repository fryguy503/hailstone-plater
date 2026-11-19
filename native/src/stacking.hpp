// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>
#include <functional>
#include <limits>
#include <span>

namespace crest {

struct StackBounds {
    double x = 0;
    double y = 0;
    double width = 0;
    double height = 0;
};

struct StackPlate {
    StackBounds bounds;
    double x = 0;
    double y = 0;
    double depth = std::numeric_limits<double>::infinity();
    std::uint32_t id = 0;
    bool target = false;
    bool stack = true;
    bool hide = false;
    double gap = 4;
    double max_offset = 96;
    double offset_y = 0;
    bool hidden = false;
};

// Preserve input order. Targets and opt-out plates stay at their anchors.
// Once exhausted returns true, remaining movable plates are hidden for this frame.
void stack_plates(std::span<StackPlate> plates, StackBounds viewport,
                  const std::function<bool()>& exhausted = {});

} // namespace crest
