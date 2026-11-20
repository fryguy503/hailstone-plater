// SPDX-License-Identifier: MIT
#include "presentation_detail.hpp"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <numbers>
#include <utility>

namespace crest::detail {
namespace {

constexpr std::size_t max_commands = 512, max_vertices = 65536, max_badges = 8;

struct Point { float x, y; };
struct TextMetric { float width, height, x0, y0, x1, y1; };
struct Ink { float left, top, right, bottom; };
struct BadgeLayout {
    const Badge* data;
    float width, font, icon_width;
    float x = 0, y = 0;
};

std::uint32_t tone(std::uint32_t rgb, float factor)
{
    std::uint32_t result = 0;
    for (const unsigned shift : {0u, 8u, 16u}) {
        const auto channel = static_cast<unsigned>(std::min(255.0f, std::floor(((rgb >> shift) & 255u) * factor + 0.5f)));
        result |= channel << shift;
    }
    return result;
}

float clamped(float value, float low, float high, float fallback)
{
    return std::isfinite(value) ? std::clamp(value, low, high) : fallback;
}

class Geometry {
public:
    Geometry(const PlateStyle& style, const HailstoneNameplateFrame& frame, const HailstoneNameplateCandidate& actor,
             const CanvasMetrics& metrics)
        : style_(style), metrics_(metrics), font_(frame.font)
    {
        unit_ = std::clamp(style.scale * clamped(actor.native_scale, 0.1f, 4.0f, 1), 0.1f, 4.0f);
        fs_ = std::clamp(clamped(frame.font_size, 4, 256, 14) * unit_ *
                         clamped(style.font_scale, 0.5f, 2, 1), 4.0f, 256.0f);
        tag_font_ = std::max(4.0f, fs_ * 0.8f);
        bar_font_ = std::max(4.0f, fs_ * 0.72f);
        width_ = std::clamp(style.width, 24.0f, 600.0f) * unit_;
        gap_ = 2 * unit_;
        if (style.frame_style == "runic") ornament_ = 4 * unit_;
        else if (style.frame_style == "bevel" || style.frame_style == "glass") ornament_ = 3 * unit_;
        row_gap_ = gap_ + ornament_ * 2;
        cast_width_ = style.cast_width > 0 ? std::clamp(style.cast_width, 24.0f, 600.0f) * unit_ : width_;
        body_width_ = style.cast ? std::max(width_, cast_width_) : width_;
        target_ = (actor.flags & HAILSTONE_NAMEPLATE_TARGET) && style.show_marker && style.target_style != "none";
        if (target_ && style.target_style == "chevrons" && (style.health || style.resource || style.cast)) {
            // Clear the widest bar, including its frame art and stroke fringe.
            const auto bar_width = style.health || style.resource ? body_width_ : cast_width_;
            auto frame_fringe = 1 + ornament_;
            if (style.frame_style == "runic") frame_fringe = 1 + 15 * unit_;
            else if (style.frame_style == "glass") frame_fringe += unit_ / 2;
            else if (style.frame_style == "bevel") frame_fringe += 0.75f * unit_;
            else if (style.frame_style == "minimal") frame_fringe = 1 + std::max(0.25f, 2 * unit_);
            const auto stroke_fringe = 1 + unit_;
            chevron_tip_ = bar_width / 2 + std::max(5 * unit_, frame_fringe + stroke_fringe + gap_);
            chevron_right_ = chevron_tip_ + 12 * unit_ + stroke_fringe;
        }
    }

    std::optional<PreparedPlate> build();

private:
    const PlateStyle& style_;
    const CanvasMetrics& metrics_;
    int32_t font_;
    float unit_, fs_, tag_font_, bar_font_, width_, gap_, ornament_ = 0, row_gap_, cast_width_, body_width_;
    float chevron_tip_ = 0, chevron_right_ = 0, reserved_ = 0, name_width_ = 0;
    bool target_ = false, failed_ = false, inside_ = false;
    std::string name_;
    std::unordered_map<float, std::unordered_map<std::string, TextMetric>> measurements_;
    PreparedPlate plate_;
    static constexpr auto infinity = std::numeric_limits<float>::infinity();
    Ink ink_{infinity, infinity, -infinity, -infinity};

    TextMetric measured(std::string_view text, float size);
    float extent(std::string_view text, float size);
    std::pair<float, float> text_height(std::string_view text, float size);
    std::string fit(std::string_view text, float size, float width, std::string_view prefix = {}, std::string_view suffix = {});
    void emit(HailstoneNameplateCommand command, const Ink& ink);
    void rect(float x0, float y0, float x1, float y1, std::uint32_t color,
              float rounding = 0, float thickness = 0, float opacity = 1);
    void line(Point a, Point b, std::uint32_t color, float thickness, float opacity = 1);
    void triangle(Point a, Point b, Point c, std::uint32_t color);
    void circle(Point center, float radius, std::uint32_t color, float thickness = 0);
    void label(std::string_view text, float x, float y, float size, float available, std::string_view align,
               std::uint32_t color, bool shadow, std::size_t prefix = 0, std::uint32_t prefix_color = 0);
    void polygon(std::initializer_list<Point> points, std::uint32_t color, float thickness = 0);
    void diamond(Point center, float radius, std::uint32_t color);
    void vector_icon(std::string_view kind, Point center, float radius, std::uint32_t color);
    bool image_icon(std::uint32_t spell, Point center, float radius);
    void fill(float x0, float y0, float x1, float y1, std::uint32_t color, float rounding);
    void target_trim(float x0, float y0, float x1, float y1, float rounding);
    void draw_bar(const std::optional<Bar>&, float y, float height, float width, std::string_view align,
                  bool primary, bool is_health);
};

TextMetric Geometry::measured(std::string_view text, float size)
{
    if (text.empty()) return {0, size, 0, 0, 0, size};
    auto& cache = measurements_[size];
    auto key = std::string(text);
    if (auto found = cache.find(key); found != cache.end()) return found->second;
    const auto m = metrics_.measure(text, size, font_);
    if (m && std::isfinite(m->width) && std::isfinite(m->height) && m->width >= 0 && m->width <= 131072 &&
        m->height >= 0 && m->height <= 4096 && std::isfinite(m->ink_x0) && std::isfinite(m->ink_y0) &&
        std::isfinite(m->ink_x1) && std::isfinite(m->ink_y1) && std::abs(m->ink_x0) <= 131072 &&
        std::abs(m->ink_y0) <= 131072 && std::abs(m->ink_x1) <= 131072 && std::abs(m->ink_y1) <= 131072 &&
        m->ink_x1 >= m->ink_x0 && m->ink_y1 >= m->ink_y0) {
        return cache.emplace(std::move(key), TextMetric{m->width, m->height, m->ink_x0, m->ink_y0, m->ink_x1, m->ink_y1}).first->second;
    }
    failed_ = true;
    return {0, size, 0, 0, 0, size};
}

float Geometry::extent(std::string_view text, float size)
{
    const auto m = measured(text, size);
    return std::max(m.width, m.x1) - std::min(0.0f, m.x0);
}

std::pair<float, float> Geometry::text_height(std::string_view text, float size)
{
    const auto m = measured(text, size);
    const auto top = std::min(0.0f, m.y0);
    return {std::max({size, m.height, m.y1}) - top, top};
}

std::string Geometry::fit(std::string_view text, float size, float available, std::string_view prefix, std::string_view suffix)
{
    if (text.empty() || available <= 0) return {};
    // Measuring a badge then subtracting its positioned padding can round the
    // available space down by a few float ULPs.
    constexpr float pixel_tolerance = 0.001f;
    std::string full(prefix);
    full.append(text).append(suffix);
    if (extent(full, size) <= available + pixel_tolerance) return full;
    std::string ellipsis(prefix);
    ellipsis.append("...").append(suffix);
    if (extent(ellipsis, size) > available + pixel_tolerance) return {};
    const auto offsets = utf8_offsets(text);
    std::size_t low = 1, high = offsets.size();
    while (low < high) {
        const auto middle = low + (high - low) / 2;
        std::string candidate(prefix);
        candidate.append(text.substr(0, offsets[middle])).append("...").append(suffix);
        if (extent(candidate, size) <= available + pixel_tolerance) {
            ellipsis = std::move(candidate);
            low = middle + 1;
        } else high = middle;
    }
    return ellipsis;
}

void Geometry::emit(HailstoneNameplateCommand c, const Ink& ink)
{
    if (c.alpha <= 0) return;
    if (plate_.commands.size() >= max_commands) {
        failed_ = true;
        return;
    }
    plate_.commands.push_back(c);
    ink_.left = std::min(ink_.left, ink.left);
    ink_.top = std::min(ink_.top, ink.top);
    ink_.right = std::max(ink_.right, ink.right);
    ink_.bottom = std::max(ink_.bottom, ink.bottom);
    switch (c.kind) {
    case HAILSTONE_NAMEPLATE_RECT: plate_.vertex_cost += c.rounding > 0 ? 512 : 24; break;
    case HAILSTONE_NAMEPLATE_RECT_FILLED:
    case HAILSTONE_NAMEPLATE_GRADIENT: plate_.vertex_cost += c.rounding > 0 ? 256 : 4; break;
    case HAILSTONE_NAMEPLATE_CIRCLE:
    case HAILSTONE_NAMEPLATE_CIRCLE_FILLED: plate_.vertex_cost += 4 * (c.segments + 1); break;
    case HAILSTONE_NAMEPLATE_TEXT: plate_.vertex_cost += 4 * (c.text.bytes + 1); break;
    case HAILSTONE_NAMEPLATE_IMAGE: plate_.vertex_cost += 4; break;
    default: plate_.vertex_cost += 12; break;
    }
}

void Geometry::rect(float x0, float y0, float x1, float y1, std::uint32_t color, float rounding, float thickness, float opacity)
{
    if (x1 <= x0 || y1 <= y0) return;
    HailstoneNameplateCommand c{};
    c.kind = thickness ? HAILSTONE_NAMEPLATE_RECT : HAILSTONE_NAMEPLATE_RECT_FILLED;
    c.color = color;
    c.alpha = style_.alpha * opacity;
    c.x0 = x0; c.y0 = y0; c.x1 = x1; c.y1 = y1;
    c.rounding = rounding;
    c.thickness = thickness ? std::clamp(thickness, 0.01f, 64.0f) : 0.0f;
    const auto fringe = 1 + c.thickness / 2;
    emit(c, {x0 - fringe, y0 - fringe, x1 + fringe, y1 + fringe});
}

void Geometry::line(Point a, Point b, std::uint32_t color, float thickness, float opacity)
{
    HailstoneNameplateCommand c{};
    c.kind = HAILSTONE_NAMEPLATE_LINE;
    c.color = color;
    c.alpha = style_.alpha * opacity;
    c.thickness = std::clamp(thickness, 0.01f, 64.0f);
    c.x0 = a.x; c.y0 = a.y; c.x1 = b.x; c.y1 = b.y;
    const auto fringe = 1 + c.thickness / 2;
    emit(c, {std::min(a.x, b.x) - fringe, std::min(a.y, b.y) - fringe,
             std::max(a.x, b.x) + fringe, std::max(a.y, b.y) + fringe});
}

void Geometry::triangle(Point a, Point b, Point c, std::uint32_t color)
{
    HailstoneNameplateCommand command{};
    command.kind = HAILSTONE_NAMEPLATE_TRIANGLE_FILLED;
    command.color = color;
    command.alpha = style_.alpha;
    command.x0 = a.x; command.y0 = a.y;
    command.x1 = b.x; command.y1 = b.y;
    command.x2 = c.x; command.y2 = c.y;
    emit(command, {std::min({a.x, b.x, c.x}) - 1, std::min({a.y, b.y, c.y}) - 1,
                   std::max({a.x, b.x, c.x}) + 1, std::max({a.y, b.y, c.y}) + 1});
}

void Geometry::circle(Point center, float radius, std::uint32_t color, float thickness)
{
    HailstoneNameplateCommand c{};
    c.kind = thickness ? HAILSTONE_NAMEPLATE_CIRCLE : HAILSTONE_NAMEPLATE_CIRCLE_FILLED;
    c.color = color;
    c.alpha = style_.alpha;
    c.x = center.x; c.y = center.y;
    c.radius = radius;
    c.segments = 32;
    c.thickness = thickness ? std::clamp(thickness, 0.01f, 64.0f) : 0.0f;
    const auto outer = radius + 1 + c.thickness / 2;
    emit(c, {center.x - outer, center.y - outer, center.x + outer, center.y + outer});
}

void Geometry::label(std::string_view text, float x, float y, float size, float available, std::string_view align,
                     std::uint32_t color, bool shadow, std::size_t prefix, std::uint32_t prefix_color)
{
    auto fitted = fit(text, size, available);
    if (fitted.empty()) return;
    const auto m = measured(fitted, size);
    const auto width = std::max(m.width, m.x1) - std::min(0.0f, m.x0);
    const auto shift = align == "left" ? 0.0f : align == "right" ? available - width : (available - width) / 2;
    x += shift - std::min(0.0f, m.x0);
    const auto print = [&](std::string_view piece, const TextMetric& ink, float tx, float ty, std::uint32_t rgb, float opacity) {
        HailstoneNameplateCommand c{};
        c.kind = HAILSTONE_NAMEPLATE_TEXT;
        c.color = rgb;
        c.alpha = style_.alpha * opacity;
        c.x = tx; c.y = ty;
        c.font_size = size;
        c.font = font_;
        const auto& owned = plate_.text_storage.emplace_back(piece);
        c.text = {owned.data(), static_cast<std::uint32_t>(owned.size())};
        emit(c, {tx + ink.x0, ty + ink.y0, tx + ink.x1, ty + ink.y1});
    };
    if (shadow) print(fitted, m, x + unit_, y + unit_, 0, 0.82f);
    if (prefix > 0 && prefix_color != color) {
        if (fitted.size() <= prefix) print(fitted, m, x, y, prefix_color, 1);
        else {
            const std::string_view name(fitted);
            const auto head = name.substr(0, prefix), rest = name.substr(prefix);
            const auto head_metric = measured(head, size);
            print(head, head_metric, x, y, prefix_color, 1);
            print(rest, measured(rest, size), x + head_metric.width, y, color, 1);
        }
    } else print(fitted, m, x, y, color, 1);
}

void Geometry::polygon(std::initializer_list<Point> points, std::uint32_t color, float thickness)
{
    const auto* p = points.begin();
    if (thickness) {
        for (std::size_t i = 0; i < points.size(); ++i) line(p[i], p[(i + 1) % points.size()], color, thickness);
    } else {
        for (std::size_t i = 1; i + 1 < points.size(); ++i) triangle(p[0], p[i], p[i + 1], color);
    }
}

void Geometry::diamond(Point center, float r, std::uint32_t color)
{
    const auto [x, y] = center;
    polygon({{x - r, y}, {x, y - r}, {x + r, y}, {x, y + r}}, color);
}

void Geometry::vector_icon(std::string_view kind, Point center, float r, std::uint32_t color)
{
    const auto [x, y] = center;
    const auto stroke = std::max(0.5f, 0.18f * r);
    const auto p = [&](float hx, float hy) -> Point { return {x + hx * r, y + hy * r}; };
    if (kind == "eye") {
        auto top = p(-1, 0), bottom = top;
        for (int n = 1; n <= 8; ++n) {
            const auto t = n / 8.0f;
            const auto bulge = 2.4f * t * (1 - t);
            const auto next_top = p(2 * t - 1, -bulge), next_bottom = p(2 * t - 1, bulge);
            line(top, next_top, color, stroke);
            line(bottom, next_bottom, color, stroke);
            top = next_top; bottom = next_bottom;
        }
        circle(center, r * 0.3f, color);
    } else if (kind == "peace") {
        triangle(p(0, 0.2f), p(-0.7f, -1), p(0.15f, -0.3f), color);
        triangle(p(-0.3f, 0.3f), p(-1, 0.6f), p(-0.6f, -0.2f), color);
        circle(p(0.15f, 0.1f), r * 0.45f, color);
        circle(p(0.55f, -0.2f), r * 0.25f, color);
        triangle(p(0.7f, -0.3f), p(1, -0.15f), p(0.7f, 0), color);
        line(p(0.6f, 0.15f), p(0.9f, 0.8f), color, stroke);
    } else if (kind == "sleep") {
        line(p(-0.8f, -0.8f), p(0.8f, -0.8f), color, stroke);
        line(p(0.8f, -0.8f), p(-0.8f, 0.8f), color, stroke);
        line(p(-0.8f, 0.8f), p(0.8f, 0.8f), color, stroke);
    } else if (kind == "star") {
        std::array<Point, 10> edges;
        for (std::size_t n = 0; n < edges.size(); ++n) {
            const auto angle = n * std::numbers::pi_v<float> / 5 - std::numbers::pi_v<float> / 2;
            const auto edge = n % 2 ? 0.43f : 1.0f;
            edges[n] = p(std::cos(angle) * edge, std::sin(angle) * edge);
        }
        for (std::size_t n = 0; n < edges.size(); ++n) triangle(center, edges[n], edges[(n + 1) % edges.size()], color);
    } else if (kind == "sun") {
        circle(center, r * 0.5f, color);
        for (int n = 0; n < 8; ++n) {
            const auto angle = n * std::numbers::pi_v<float> / 4;
            const auto dx = std::cos(angle), dy = std::sin(angle);
            line(p(dx * 0.7f, dy * 0.7f), p(dx, dy), color, stroke);
        }
    } else if (kind == "moon") {
        circle(center, r * 0.9f, color);
        circle(p(0.4f, -0.25f), r * 0.75f, style_.background);
    } else if (kind == "diamond") diamond(center, r, color);
    else if (kind == "triangle") triangle(p(0, -1), p(1, 0.8f), p(-1, 0.8f), color);
    else if (kind == "cross") {
        rect(x - r * 0.25f, y - r, x + r * 0.25f, y + r, color);
        rect(x - r, y - r * 0.25f, x + r, y + r * 0.25f, color);
    } else if (kind == "skull") {
        circle(p(0, -0.2f), r * 0.8f, color);
        rect(x - r * 0.5f, y, x + r * 0.5f, y + r * 0.9f, color);
        circle(p(-0.3f, -0.2f), r * 0.2f, style_.background);
        circle(p(0.3f, -0.2f), r * 0.2f, style_.background);
        line(p(0, 0.5f), p(0, 0.9f), style_.background, stroke);
    } else if (kind == "sword") {
        polygon({p(-0.15f, 0.2f), p(0.75f, -0.85f), p(0.85f, -0.75f), p(0.1f, 0.4f)}, color);
        line(p(-0.65f, 0.75f), p(-0.1f, 0.2f), color, stroke * 2);
        line(p(-0.5f, 0), p(0.4f, 0.7f), color, stroke * 1.5f);
    } else circle(center, r * 0.8f, color, stroke);
}

bool Geometry::image_icon(std::uint32_t spell, Point center, float r)
{
    if (!metrics_.icon || spell > 65535) return false;
    const auto image = metrics_.icon(spell);
    if (!image || !image->texture || !std::isfinite(image->u0) || !std::isfinite(image->v0) ||
        !std::isfinite(image->u1) || !std::isfinite(image->v1) || image->u0 < 0 || image->v0 < 0 ||
        image->u1 > 1 || image->v1 > 1 || image->u1 < image->u0 || image->v1 < image->v0) return false;
    HailstoneNameplateCommand c{};
    c.kind = HAILSTONE_NAMEPLATE_IMAGE;
    c.color = 0xffffff;
    c.alpha = style_.alpha;
    c.texture = image->texture;
    c.u0 = image->u0; c.v0 = image->v0; c.u1 = image->u1; c.v1 = image->v1;
    c.x0 = center.x - r; c.y0 = center.y - r; c.x1 = center.x + r; c.y1 = center.y + r;
    emit(c, {c.x0, c.y0, c.x1, c.y1});
    return true;
}

void Geometry::fill(float x0, float y0, float x1, float y1, std::uint32_t color, float rounding)
{
    if (x1 <= x0 || y1 <= y0) return;
    if (style_.texture == "gradient" || style_.texture == "gloss") {
        HailstoneNameplateCommand c{};
        c.kind = HAILSTONE_NAMEPLATE_GRADIENT;
        c.x0 = x0; c.y0 = y0; c.x1 = x1; c.y1 = y1;
        c.alpha = c.alpha2 = style_.alpha;
        c.color = tone(color, 1.15f); c.color2 = tone(color, 0.75f);
        c.rounding = rounding;
        emit(c, {x0 - 1, y0 - 1, x1 + 1, y1 + 1});
    } else rect(x0, y0, x1, y1, color, rounding);
    if (style_.texture == "gloss") {
        const auto shine = (y1 - y0) * 0.42f;
        rect(x0, y0, x1, y0 + shine, 0xffffff, std::min(rounding, shine / 2), 0, 0.23f);
        line({x0, y0 + shine}, {x1, y0 + shine}, 0xffffff, std::min(unit_, y1 - y0), 0.18f);
    }
}

void Geometry::target_trim(float x0, float y0, float x1, float y1, float rounding)
{
    const auto border = style_.border;
    if (style_.target_style == "arrow") {
        rect(x0 - unit_, y0 - unit_, x1 + unit_, y1 + unit_, border, rounding, unit_);
    } else if (style_.target_style == "chevrons") {
        const auto middle = (y0 + y1) / 2;
        for (const auto side : {-1, 1}) {
            for (const auto distance : {0, 7}) {
                const auto tip = side * (chevron_tip_ + distance * unit_);
                const auto tail = tip + side * 5 * unit_;
                line({tail, middle - 5 * unit_}, {tip, middle}, border, 2 * unit_);
                line({tip, middle}, {tail, middle + 5 * unit_}, border, 2 * unit_);
            }
        }
    } else if (style_.target_style == "brackets") {
        for (const auto side : {-1, 1}) {
            for (const auto row : {-1, 1}) {
                const auto x = (side < 0 ? x0 : x1) + side * 3 * unit_;
                const auto y = (row < 0 ? y0 : y1) + row * 3 * unit_;
                line({x, y - row * 5 * unit_}, {x, y}, border, 1.5f * unit_);
                line({x, y}, {x - side * 7 * unit_, y}, border, 1.5f * unit_);
            }
        }
    } else if (style_.target_style == "glow") {
        for (int ring = 6; ring >= 1; --ring) {
            const auto distance = ring * unit_;
            rect(x0 - distance, y0 - distance, x1 + distance, y1 + distance, border,
                 rounding + distance, unit_, 0.32f - ring * 0.035f);
        }
    }
}

void Geometry::draw_bar(const std::optional<Bar>& value, float y0, float height, float width, std::string_view align,
                        bool primary, bool is_health)
{
    if (!value) return;
    const auto x0 = -width / 2, x1 = width / 2, y1 = y0 + height;
    const auto edge = std::min({style_.border_width * unit_, (height - unit_) / 2, (width - unit_) / 2});
    const auto rounding = std::min(style_.rounding * unit_, height / 2);
    const auto border = style_.border, bg = style_.background;
    const auto out = ornament_;
    if (style_.frame_style == "bevel") {
        rect(x0 - out, y0 - out, x1 + out, y1 + out, tone(border, 0.45f));
        line({x0 - out, y0 - out}, {x1 + out, y0 - out}, tone(border, 1.85f), 1.5f * unit_);
        line({x0 - out, y0 - out}, {x0 - out, y1 + out}, tone(border, 1.4f), 1.5f * unit_);
        line({x0 - out, y1 + out}, {x1 + out, y1 + out}, 0, 1.5f * unit_);
        line({x1 + out, y0 - out}, {x1 + out, y1 + out}, 0, 1.5f * unit_);
    } else if (style_.frame_style == "glass") {
        const auto radius = std::max(rounding, height * 0.35f);
        rect(x0 - out, y0 - out, x1 + out, y1 + out, 0x060b13, radius, 0, 0.88f);
        rect(x0 - out, y0 - out, x1 + out, y1 + out, 0xb4d1e8, radius, unit_, 0.32f);
    } else if (style_.frame_style == "runic") {
        const auto middle = (y0 + y1) / 2;
        const auto cut = std::min(height / 2 + out, 7 * unit_);
        const std::initializer_list<Point> points{{x0 - out + cut, y0 - out}, {x1 + out - cut, y0 - out},
            {x1 + out, middle}, {x1 + out - cut, y1 + out}, {x0 - out + cut, y1 + out}, {x0 - out, middle}};
        polygon(points, tone(border, 0.45f));
        polygon(points, border, 1.4f * unit_);
        if (primary) {
            for (const auto side : {-1, 1}) {
                const Point center{(side < 0 ? x0 : x1) + side * 10 * unit_, middle};
                diamond(center, 5 * unit_, border);
                circle(center, 1.6f * unit_, bg);
            }
        }
    }
    rect(x0, y0, x1, y1, bg, rounding, 0, style_.frame_style == "minimal" ? 0.45f : 1.0f);
    const auto ix = x0 + edge, iy = y0 + edge, iw = width - 2 * edge, ih = height - 2 * edge;
    const auto right = ix + iw * static_cast<float>(value->value / 100);
    if (style_.texture == "segments") {
        const auto count = std::clamp(static_cast<int>(std::floor(iw / std::max(2.0f, 3 * unit_))), 1, 10);
        const auto segment = iw / count;
        const auto segment_gap = std::min(1.5f * unit_, 0.25f * segment);
        for (int i = 0; i < count; ++i) {
            const auto left = ix + i * segment;
            fill(left, iy, std::min(right, left + segment - (i + 1 < count ? segment_gap : 0)), iy + ih, value->color, 0);
        }
    } else fill(ix, iy, right, iy + ih, value->color, std::max(0.0f, rounding - edge));
    if (style_.frame_style == "minimal") line({x0, y1}, {x1, y1}, border, std::max(0.5f, edge));
    else if (edge > 0) rect(x0 + edge / 2, y0 + edge / 2, x1 - edge / 2, y1 - edge / 2, border, rounding, edge);
    if (primary && target_) target_trim(x0, y0, x1, y1, rounding);

    auto text_x = x0 + gap_, text_width = width - 2 * gap_;
    if (is_health && reserved_ > 0) {
        text_x = align == "left" ? x0 + gap_ : x1 - reserved_;
        text_width = reserved_ - gap_;
    }
    if (!value->text.empty()) {
        const auto [th, above] = text_height(value->text, bar_font_);
        label(value->text, text_x, y0 + (height - th) / 2 - above, bar_font_, text_width, align, style_.text_color, true);
    }
    if (inside_ && is_health) {
        const auto nx = align == "left" ? x0 + reserved_ + gap_ : x0 + gap_;
        const auto [nh, above] = text_height(name_, fs_);
        label(name_, nx, y0 + (height - nh) / 2 - above, fs_, name_width_, style_.text_align, style_.name_color, true);
    }
}

std::optional<PreparedPlate> Geometry::build()
{
    auto subtitle = style_.subtitle;
    const auto& level = style_.level;
    const bool level_below = !level.empty() && style_.health && style_.level_position == "below";
    std::size_t level_prefix = 0;
    if (!level.empty() && !level_below) {
        if (subtitle.empty()) subtitle = level;
        else subtitle = level + " | " + subtitle;
        level_prefix = level.size();
    }
    name_ = style_.name;
    const bool show_name = style_.show_name && !name_.empty();
    inside_ = show_name && style_.name_position == "inside" && style_.health;
    if (inside_ && !style_.health->text.empty()) {
        reserved_ = std::min(width_ / 2, extent(style_.health->text, bar_font_) + gap_ * 2);
    }
    name_width_ = inside_ ? std::max(0.0f, width_ - reserved_ - gap_ * 2) : width_;
    const Badge* fallback = nullptr;
    if (show_name && (!style_.name_prefix.empty() || !style_.name_suffix.empty())) {
        auto decorated = fit(name_, fs_, name_width_, style_.name_prefix, style_.name_suffix);
        if (!decorated.empty()) name_ = std::move(decorated);
        else if (style_.name_fallback_badge) fallback = &*style_.name_fallback_badge;
    }

    const bool side = style_.badge_position == "left" || style_.badge_position == "right";
    const auto badge_scale = std::clamp(style_.badge_scale, 0.5f, 2.5f);
    const auto badge_gap = gap_ * badge_scale;
    const auto badge_limit = side ? std::min(width_, 140 * unit_) : width_;
    const auto badge_size = std::min(std::max(14 * unit_, fs_ * 0.8f) * badge_scale,
                                     std::max(unit_, badge_limit - 2 * badge_gap));
    const auto badge_font = std::clamp(tag_font_ * badge_scale, 4.0f, 256.0f);
    std::array<BadgeLayout, max_badges> badges{};
    std::size_t badge_count = 0;
    float column_width = 0;
    const auto add_badge = [&](const Badge& data) {
        if (badge_count >= badges.size()) return;
        const auto icon_width = data.text_only ? 0.0f : badge_size;
        const auto available = std::max(0.0f, badge_limit - icon_width - badge_gap * (data.text_only ? 2 : 3));
        const auto measured_width = extent(data.text, badge_font);
        const auto size = data.text_only && measured_width > available
            ? std::max(4.0f, badge_font * available / measured_width) : badge_font;
        const auto label_width = std::min(size == badge_font ? measured_width : extent(data.text, size), available);
        const auto padding = badge_gap * (data.text.empty() || data.text_only ? 2 : 3);
        const auto bw = std::min(badge_limit, icon_width + padding + label_width);
        badges[badge_count++] = {&data, bw, size, icon_width};
        column_width = std::max(column_width, bw);
    };
    if (fallback) add_badge(*fallback);
    for (const auto& badge : style_.badges) add_badge(badge);
    auto badge_layout = std::span(badges).first(badge_count);
    if (!style_.health && !style_.resource && !style_.cast && !show_name && subtitle.empty() && badge_layout.empty()) {
        return hidden_plate();
    }

    const auto marker = target_ && style_.target_style == "arrow" ? 10 * unit_ : 0.0f;
    auto y = marker;
    const auto rows = [&](float at) {
        auto row = at;
        float x = 0;
        for (auto& badge : badge_layout) {
            if (x > 0 && x + badge.width > width_) { x = 0; row += badge_size + badge_gap; }
            badge.x = x - width_ / 2;
            badge.y = row;
            x += badge.width + badge_gap;
        }
        return badge_layout.empty() ? at : row + badge_size + badge_gap;
    };
    if (style_.badge_position == "above") y = rows(y);
    const auto body_top = y;
    const auto text_row = [&](std::string_view text, float font) {
        const auto [height, top] = text_height(text, font);
        const auto at = y - top;
        y += height + unit_ + row_gap_;
        return at;
    };
    auto name_y = y;
    auto guild_y = y;
    const auto show_guild = show_name && !style_.guild.empty();
    if (show_name && style_.name_position != "below" && !inside_) {
        name_y = text_row(name_, fs_);
        if (show_guild) guild_y = text_row(style_.guild, tag_font_);
    }
    auto subtitle_y = y;
    if (!subtitle.empty()) subtitle_y = text_row(subtitle, tag_font_);
    const auto bar_height = [&](const std::optional<Bar>& value, float height) {
        if (!value) return 0.0f;
        auto h = std::clamp(height, 1.0f, 80.0f) * unit_;
        if (!value->text.empty()) h = std::max(h, text_height(value->text, bar_font_).first + gap_ + unit_);
        return h;
    };
    auto hh = bar_height(style_.health, style_.height);
    const auto mh = bar_height(style_.resource, style_.mana_height), ch = bar_height(style_.cast, style_.cast_height);
    if (inside_) hh = std::max(hh, text_height(name_, fs_).first + gap_ + unit_);
    auto hy = y;
    y += hh;
    if (inside_ && show_guild) {
        y += row_gap_;
        guild_y = text_row(style_.guild, tag_font_);
    }
    auto level_y = y;
    if (level_below) {
        const auto [height, top] = text_height(level, tag_font_);
        level_y = y + row_gap_ - top;
        y += row_gap_ + height + unit_;
    }
    if (style_.health && style_.resource) y += row_gap_;
    auto my = y;
    y += mh;
    if (style_.cast && (style_.health || style_.resource)) y += style_.cast_gap * unit_ + ornament_ * 2;
    auto cy = y;
    y += ch;
    if (show_name && style_.name_position == "below") {
        if (style_.health || style_.resource || style_.cast) y += row_gap_;
        name_y = text_row(name_, fs_);
        if (show_guild) guild_y = text_row(style_.guild, tag_font_);
        y -= row_gap_;
    }
    if (side && !badge_layout.empty()) {
        const auto column_height = badge_layout.size() * (badge_size + badge_gap) - badge_gap;
        const auto body_height = y - body_top;
        const auto shift = std::max(0.0f, (column_height - body_height) / 2);
        name_y += shift; guild_y += shift; subtitle_y += shift; level_y += shift; hy += shift; my += shift; cy += shift;
        const auto top = body_top + std::max(0.0f, (body_height - column_height) / 2);
        auto decoration = style_.frame_style == "runic" ? 17 * unit_ : ornament_ + 2 * unit_;
        if (target_ && style_.target_style == "glow") decoration = std::max(decoration, 9 * unit_);
        auto side_edge = body_width_ / 2 + decoration;
        if (chevron_right_ > 0) {
            float fringe = 0;
            if (style_.frame_style != "minimal") {
                fringe = 1;
                if (style_.frame_style == "bevel" || style_.frame_style == "runic") fringe += unit_ / 2;
            }
            side_edge = std::max(side_edge, chevron_right_ + fringe);
        }
        for (std::size_t i = 0; i < badge_layout.size(); ++i) {
            auto& b = badge_layout[i];
            b.x = style_.badge_position == "left" ? -side_edge - badge_gap - column_width : side_edge + badge_gap;
            b.y = top + i * (badge_size + badge_gap);
        }
    } else if (style_.badge_position == "below" && !badge_layout.empty()) rows(y + row_gap_);

    if (marker > 0) triangle({-3.5f * unit_, unit_}, {3.5f * unit_, unit_}, {0, 7.3f * unit_}, style_.border);
    for (const auto& b : badge_layout) {
        const auto& data = *b.data;
        const auto bx = b.x, by = b.y;
        if (style_.frame_style != "minimal") rect(bx, by, bx + b.width, by + badge_size, style_.background, 2 * unit_);
        if (style_.frame_style == "bevel" || style_.frame_style == "runic") {
            rect(bx, by, bx + b.width, by + badge_size, style_.border, unit_, unit_);
        }
        const Point center{bx + badge_gap + badge_size / 2, by + badge_size / 2};
        if (b.icon_width > 0 && !(data.spell_icon && image_icon(*data.spell_icon, center, badge_size * 0.45f))) {
            vector_icon(data.icon, center, badge_size * 0.35f, data.color);
        }
        const auto text_x = bx + badge_gap + (b.icon_width > 0 ? b.icon_width + badge_gap : 0);
        label(data.text, text_x, by + (badge_size - b.font) / 2, b.font,
              std::max(0.0f, bx + b.width - badge_gap - text_x), "left", data.color, false);
    }
    draw_bar(style_.health, hy, hh, width_, style_.health_text_align, true, true);
    draw_bar(style_.resource, my, mh, width_, style_.health_text_align, !style_.health, false);
    draw_bar(style_.cast, cy, ch, cast_width_, style_.cast_text_align, !style_.health && !style_.resource, false);
    if (show_name && !inside_) label(name_, -width_ / 2, name_y, fs_, width_, style_.text_align, style_.name_color, true);
    if (show_guild) label(style_.guild, -width_ / 2, guild_y, tag_font_, width_, style_.text_align, style_.text_color, true);
    label(subtitle, -width_ / 2, subtitle_y, tag_font_, width_, style_.text_align, style_.text_color, true,
          level_prefix, style_.level_color);
    if (level_below) label(level, -width_ / 2, level_y, tag_font_, width_, style_.text_align, style_.level_color, true);

    if (failed_ || plate_.vertex_cost > max_vertices) return std::nullopt;
    if (plate_.commands.empty()) return hidden_plate();
    const auto width = ink_.right - ink_.left, height = ink_.bottom - ink_.top;
    if (width < 0.01f || width > 4096 || height < 0.01f || height > 4096) return std::nullopt;
    const auto dy = -ink_.bottom - style_.offset_y * unit_;
    for (auto& c : plate_.commands) {
        if (c.kind == HAILSTONE_NAMEPLATE_TEXT || c.kind == HAILSTONE_NAMEPLATE_CIRCLE ||
            c.kind == HAILSTONE_NAMEPLATE_CIRCLE_FILLED) c.y += dy;
        else { c.y0 += dy; c.y1 += dy; }
        if (c.kind == HAILSTONE_NAMEPLATE_TRIANGLE_FILLED) c.y2 += dy;
    }
    plate_.bounds = {ink_.left, ink_.top + dy, width, height};
    plate_.stack = style_.stack;
    plate_.stack_gap = style_.stack_gap * unit_;
    plate_.max_stack = style_.max_stack * unit_;
    plate_.through_walls = style_.through_walls;
    plate_.clickable = style_.clickable;
    return std::move(plate_);
}

} // namespace

std::optional<PreparedPlate> render(const PlateStyle& style, const HailstoneNameplateFrame& frame,
                                    const HailstoneNameplateCandidate& actor, const CanvasMetrics& metrics)
{
    if (!metrics.measure || frame.font < 0 || frame.font > 6) return std::nullopt;
    return Geometry(style, frame, actor, metrics).build();
}

} // namespace crest::detail
