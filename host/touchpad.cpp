#include "touchpad.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <mutex>
#include <string>
#include <vector>

namespace touchpad {
namespace {

enum class Kind { button, stick, dpad, menu };
enum class Mark { none, triangle, circle, cross, square };

// A control: a circle (cx, cy, r) for the sticks, the D-pad and the round buttons, a box (x, y, w, h) for the others. All in window pixels.
struct Control {
    Kind kind;
    std::uint16_t mask = 0;  // the button's bit (button), 0 for the left stick and 1 for the right (stick)
    bool round = true;
    float cx = 0, cy = 0, r = 0;
    float x = 0, y = 0, w = 0, h = 0;
    Mark mark = Mark::none;
    const char *label = "";
};

struct Finger {
    bool on = false;
    std::int64_t id = 0;
    int control = -1;   // the control it holds (a stick holds its finger until it is let go), -1 none
    float x = 0, y = 0;  // window pixels
};

std::mutex g_mutex;  // the window thread feeds and reads it; kept simple in case a host reads from another thread
int g_width = 0, g_height = 0;
float g_unit = 1;
std::vector<Control> g_controls;
std::array<Finger, 10> g_fingers{};
bool g_menu_tap = false;
quickmenu::Picture g_picture;
int g_picture_w = -1, g_picture_h = -1;

void lay_out() {
    g_controls.clear();
    const float w = static_cast<float>(g_width), h = static_cast<float>(g_height);
    const float u = std::max(1.0f, std::min(h / 10.0f, w / 17.0f));
    g_unit = u;
    const float mx = 0.6f * u + 0.03f * w;  // the notch and the round corners of a phone held sideways
    const auto circle = [&](Kind k, std::uint16_t mask, float cx, float cy, float r, Mark mark, const char *label) {
        Control c{k, mask};
        c.cx = cx, c.cy = cy, c.r = r, c.mark = mark, c.label = label;
        g_controls.push_back(c);
    };
    const auto box = [&](Kind k, std::uint16_t mask, float x, float y, float bw, float bh, const char *label) {
        Control c{k, mask};
        c.round = false;
        c.x = x, c.y = y, c.w = bw, c.h = bh, c.label = label;
        c.cx = x + bw / 2, c.cy = y + bh / 2, c.r = std::max(bw, bh) / 2;
        g_controls.push_back(c);
    };
    // shoulders, two on each side, at the top corners
    box(Kind::button, kL2, mx, 0.4f * u, 2.6f * u, 1.0f * u, "L2");
    box(Kind::button, kL1, mx, 1.6f * u, 2.6f * u, 1.0f * u, "L1");
    box(Kind::button, kR2, w - mx - 2.6f * u, 0.4f * u, 2.6f * u, 1.0f * u, "R2");
    box(Kind::button, kR1, w - mx - 2.6f * u, 1.6f * u, 2.6f * u, 1.0f * u, "R1");
    // Select, MENU, Start in the middle of the top edge
    box(Kind::button, kSelect, w / 2 - 3.0f * u, 0.4f * u, 1.8f * u, 0.8f * u, "SELECT");
    box(Kind::menu, 0, w / 2 - 0.9f * u, 0.4f * u, 1.8f * u, 0.8f * u, "MENU");
    box(Kind::button, kStart, w / 2 + 1.2f * u, 0.4f * u, 1.8f * u, 0.8f * u, "START");
    // left: the D-pad above, the stick below and inside, L3 beside it
    circle(Kind::dpad, 0, mx + 2.4f * u, 4.6f * u, 1.7f * u, Mark::none, "");
    circle(Kind::stick, 0, mx + 4.2f * u, 8.0f * u, 1.5f * u, Mark::none, "");
    circle(Kind::button, kL3, mx + 1.0f * u, 8.8f * u, 0.6f * u, Mark::none, "L3");
    // right: the face buttons above, the stick below and inside, R3 beside it
    const float fx = w - mx - 2.4f * u, fy = 4.6f * u, off = 1.25f * u, fr = 0.72f * u;
    circle(Kind::button, kTriangle, fx, fy - off, fr, Mark::triangle, "");
    circle(Kind::button, kCircle, fx + off, fy, fr, Mark::circle, "");
    circle(Kind::button, kCross, fx, fy + off, fr, Mark::cross, "");
    circle(Kind::button, kSquare, fx - off, fy, fr, Mark::square, "");
    circle(Kind::stick, 1, w - mx - 4.2f * u, 8.0f * u, 1.5f * u, Mark::none, "");
    circle(Kind::button, kR3, w - mx - 1.0f * u, 8.8f * u, 0.6f * u, Mark::none, "R3");
}

// how far (in window pixels, < 0 inside) the point is from the control's edge
float distance(const Control &c, float px, float py) {
    if (c.round) return std::hypot(px - c.cx, py - c.cy) - c.r;
    const float dx = std::max({c.x - px, 0.0f, px - (c.x + c.w)}), dy = std::max({c.y - py, 0.0f, py - (c.y + c.h)});
    if (dx > 0 || dy > 0) return std::hypot(dx, dy);
    return -std::min({px - c.x, c.x + c.w - px, py - c.y, c.y + c.h - py});
}

// The control a finger at px, py presses: the nearest one it is on, or near (a bit of slack, since a thumb does not hit exactly).
int hit(float px, float py, bool sticks) {
    int best = -1;
    float best_d = 0;
    for (int i = 0; i < static_cast<int>(g_controls.size()); ++i) {
        const Control &c = g_controls[i];
        if (!sticks && c.kind == Kind::stick) continue;
        const float slack = c.kind == Kind::stick ? 0.6f * c.r : c.kind == Kind::dpad ? 0.3f * c.r : 0.3f * g_unit;
        const float d = distance(c, px, py);
        if (d > slack) continue;
        if (best < 0 || d < best_d) best = i, best_d = d;
    }
    return best;
}

Finger *find(std::int64_t id) {
    for (Finger &f : g_fingers)
        if (f.on && f.id == id) return &f;
    return nullptr;
}

std::uint8_t axis_value(float v) { return static_cast<std::uint8_t>(std::clamp(std::lround((v + 1.0f) * 127.5f), 0L, 255L)); }  // -1 0, 0 0x80, 1 0xFF

// ---- drawing: straight alpha, red in the low byte ----

constexpr std::uint32_t rgba(int r, int g, int b, int a) {
    return static_cast<std::uint32_t>(r) | static_cast<std::uint32_t>(g) << 8 | static_cast<std::uint32_t>(b) << 16 | static_cast<std::uint32_t>(a) << 24;
}
constexpr std::uint32_t kFill = rgba(255, 255, 255, 46), kEdge = rgba(255, 255, 255, 120), kLabel = rgba(255, 255, 255, 200),
                        kKnob = rgba(255, 255, 255, 90), kGreen = rgba(80, 220, 170, 210), kRed = rgba(255, 110, 120, 210),
                        kBlue = rgba(130, 170, 255, 210), kPink = rgba(240, 140, 220, 210);

// `color` over the pixel, `cover` of it (0 to 1)
void blend(std::uint32_t &dst, std::uint32_t color, float cover) {
    const float sa = static_cast<float>(color >> 24) / 255.0f * std::clamp(cover, 0.0f, 1.0f);
    if (sa <= 0) return;
    const float da = static_cast<float>(dst >> 24) / 255.0f, oa = sa + da * (1 - sa);
    std::uint32_t out = 0;
    for (int ch = 0; ch < 3; ++ch) {
        const float s = static_cast<float>((color >> (8 * ch)) & 255), d = static_cast<float>((dst >> (8 * ch)) & 255);
        const float v = (s * sa + d * da * (1 - sa)) / oa;
        out |= static_cast<std::uint32_t>(std::clamp(std::lround(v), 0L, 255L)) << (8 * ch);
    }
    dst = out | static_cast<std::uint32_t>(std::clamp(std::lround(oa * 255.0f), 0L, 255L)) << 24;
}

// Fills every pixel of the box x0..x1, y0..y1 by a signed distance (< 0 inside), with a pixel of soft edge.
template <class Sdf> void paint(quickmenu::Picture &p, float x0, float y0, float x1, float y1, std::uint32_t color, Sdf sdf) {
    const int ix0 = std::max(0, static_cast<int>(std::floor(x0)) - 1), iy0 = std::max(0, static_cast<int>(std::floor(y0)) - 1);
    const int ix1 = std::min(p.width, static_cast<int>(std::ceil(x1)) + 1), iy1 = std::min(p.height, static_cast<int>(std::ceil(y1)) + 1);
    for (int y = iy0; y < iy1; ++y)
        for (int x = ix0; x < ix1; ++x) {
            const float d = sdf(static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f);
            if (d < 0.5f) blend(p.rgba[static_cast<std::size_t>(y) * p.width + x], color, 0.5f - d);
        }
}

void disc(quickmenu::Picture &p, float cx, float cy, float r, std::uint32_t color) {
    paint(p, cx - r, cy - r, cx + r, cy + r, color, [&](float x, float y) { return std::hypot(x - cx, y - cy) - r; });
}
void ring(quickmenu::Picture &p, float cx, float cy, float r, float t, std::uint32_t color) {
    paint(p, cx - r - t, cy - r - t, cx + r + t, cy + r + t, color, [&](float x, float y) { return std::abs(std::hypot(x - cx, y - cy) - r) - t / 2; });
}
float box_sdf(float x, float y, float bx, float by, float bw, float bh, float rad) {
    const float hx = bw / 2 - rad, hy = bh / 2 - rad;
    const float qx = std::abs(x - (bx + bw / 2)) - hx, qy = std::abs(y - (by + bh / 2)) - hy;
    return std::hypot(std::max(qx, 0.0f), std::max(qy, 0.0f)) + std::min(std::max(qx, qy), 0.0f) - rad;
}
void rounded(quickmenu::Picture &p, float bx, float by, float bw, float bh, float rad, std::uint32_t fill, std::uint32_t edge, float t) {
    paint(p, bx, by, bx + bw, by + bh, fill, [&](float x, float y) { return box_sdf(x, y, bx, by, bw, bh, rad); });
    paint(p, bx - t, by - t, bx + bw + t, by + bh + t, edge, [&](float x, float y) { return std::abs(box_sdf(x, y, bx, by, bw, bh, rad)) - t / 2; });
}
float segment_sdf(float x, float y, float ax, float ay, float bx, float by) {
    const float px = x - ax, py = y - ay, dx = bx - ax, dy = by - ay;
    const float k = std::clamp((px * dx + py * dy) / std::max(dx * dx + dy * dy, 1e-6f), 0.0f, 1.0f);
    return std::hypot(px - k * dx, py - k * dy);
}
void line(quickmenu::Picture &p, float ax, float ay, float bx, float by, float t, std::uint32_t color) {
    paint(p, std::min(ax, bx) - t, std::min(ay, by) - t, std::max(ax, bx) + t, std::max(ay, by) + t, color,
          [&](float x, float y) { return segment_sdf(x, y, ax, ay, bx, by) - t / 2; });
}
void label(quickmenu::Picture &p, float cx, float cy, const char *text, int scale) {
    const std::string t = text;
    if (t.empty()) return;
    quickmenu::Picture glyphs;
    glyphs.width = quickmenu::text_width(t, scale);
    glyphs.height = 8 * scale;
    glyphs.rgba.assign(static_cast<std::size_t>(glyphs.width) * glyphs.height, 0);
    quickmenu::draw_text(glyphs, 0, 0, scale, t, kLabel);
    const int x0 = static_cast<int>(std::lround(cx)) - glyphs.width / 2, y0 = static_cast<int>(std::lround(cy)) - glyphs.height / 2;
    for (int y = 0; y < glyphs.height; ++y)
        for (int x = 0; x < glyphs.width; ++x) {
            const std::uint32_t c = glyphs.rgba[static_cast<std::size_t>(y) * glyphs.width + x];
            const int px = x0 + x, py = y0 + y;
            if (c && px >= 0 && py >= 0 && px < p.width && py < p.height) blend(p.rgba[static_cast<std::size_t>(py) * p.width + px], c, 1);
        }
}

void draw_all(quickmenu::Picture &p) {
    p.width = g_width;
    p.height = g_height;
    p.x = p.y = 0;
    p.rgba.assign(static_cast<std::size_t>(std::max(g_width, 0)) * std::max(g_height, 0), 0);
    const float u = g_unit, t = std::max(1.5f, 0.05f * u);
    const int scale = std::max(1, static_cast<int>(std::lround(u * 0.045f)));
    for (const Control &c : g_controls) {
        if (!c.round) {
            rounded(p, c.x, c.y, c.w, c.h, std::min(c.h / 2, 0.3f * u), kFill, kEdge, t);
            label(p, c.cx, c.cy, c.label, c.kind == Kind::menu || c.w < 2 * u ? std::max(1, scale - 1) : scale);
            continue;
        }
        switch (c.kind) {
        case Kind::stick:
            disc(p, c.cx, c.cy, c.r, kFill);
            ring(p, c.cx, c.cy, c.r, t, kEdge);
            disc(p, c.cx, c.cy, c.r * 0.45f, kKnob);
            break;
        case Kind::dpad: {
            const float arm = c.r * 0.62f, len = c.r;
            rounded(p, c.cx - arm / 2, c.cy - len, arm, 2 * len, 0.15f * u, kFill, kEdge, t);
            rounded(p, c.cx - len, c.cy - arm / 2, 2 * len, arm, 0.15f * u, kFill, kEdge, t);
            const float a = 0.22f * c.r, at = len - 0.3f * c.r;  // small arrows near the ends
            for (int dir = 0; dir < 4; ++dir) {
                const float ux = dir == 1 ? 1.0f : dir == 3 ? -1.0f : 0.0f, uy = dir == 0 ? -1.0f : dir == 2 ? 1.0f : 0.0f;
                const float tipx = c.cx + ux * (at + a * 0.5f), tipy = c.cy + uy * (at + a * 0.5f);
                const float bx = c.cx + ux * (at - a * 0.5f), by = c.cy + uy * (at - a * 0.5f);
                line(p, tipx, tipy, bx - uy * a, by + ux * a, t * 1.4f, kLabel);
                line(p, tipx, tipy, bx + uy * a, by - ux * a, t * 1.4f, kLabel);
            }
            break;
        }
        default: {
            disc(p, c.cx, c.cy, c.r, kFill);
            ring(p, c.cx, c.cy, c.r, t, kEdge);
            const float s = c.r * 0.45f, mt = std::max(2.0f, 0.09f * u);
            switch (c.mark) {
            case Mark::triangle: {
                const float ax = c.cx, ay = c.cy - s, bx = c.cx - s * 0.95f, by = c.cy + s * 0.65f, cx2 = c.cx + s * 0.95f, cy2 = by;
                line(p, ax, ay, bx, by, mt, kGreen);
                line(p, bx, by, cx2, cy2, mt, kGreen);
                line(p, cx2, cy2, ax, ay, mt, kGreen);
                break;
            }
            case Mark::circle: ring(p, c.cx, c.cy, s * 0.85f, mt, kRed); break;
            case Mark::cross:
                line(p, c.cx - s * 0.8f, c.cy - s * 0.8f, c.cx + s * 0.8f, c.cy + s * 0.8f, mt, kBlue);
                line(p, c.cx - s * 0.8f, c.cy + s * 0.8f, c.cx + s * 0.8f, c.cy - s * 0.8f, mt, kBlue);
                break;
            case Mark::square:
                rounded(p, c.cx - s * 0.75f, c.cy - s * 0.75f, s * 1.5f, s * 1.5f, 0.0f, 0, kPink, mt);
                break;
            case Mark::none: label(p, c.cx, c.cy, c.label, std::max(1, scale - 1)); break;
            }
        }
        }
    }
}

}  // namespace

void set_window(int width, int height) {
    std::lock_guard lock(g_mutex);
    if (width == g_width && height == g_height) return;
    g_width = width;
    g_height = height;
    lay_out();
    for (Finger &f : g_fingers) f = {};
}

void finger_down(std::int64_t id, float x, float y) {
    std::lock_guard lock(g_mutex);
    Finger *f = find(id);
    if (!f) {
        for (Finger &free : g_fingers)
            if (!free.on) {
                f = &free;
                break;
            }
    }
    if (!f) return;
    f->on = true;
    f->id = id;
    f->x = x * static_cast<float>(g_width);
    f->y = y * static_cast<float>(g_height);
    f->control = hit(f->x, f->y, true);
}

void finger_move(std::int64_t id, float x, float y) {
    std::lock_guard lock(g_mutex);
    Finger *f = find(id);
    if (!f) return;
    f->x = x * static_cast<float>(g_width);
    f->y = y * static_cast<float>(g_height);
    // a stick keeps its finger wherever it goes; on the buttons a thumb can slide from one to the next
    if (f->control >= 0 && g_controls[f->control].kind == Kind::stick) return;
    if (f->control >= 0 && g_controls[f->control].kind == Kind::menu) return;
    f->control = hit(f->x, f->y, false);
}

void finger_up(std::int64_t id) {
    std::lock_guard lock(g_mutex);
    Finger *f = find(id);
    if (!f) return;
    if (f->control >= 0 && g_controls[f->control].kind == Kind::menu && distance(g_controls[f->control], f->x, f->y) <= 0.3f * g_unit) g_menu_tap = true;
    *f = {};
}

void release_all() {
    std::lock_guard lock(g_mutex);
    for (Finger &f : g_fingers) f = {};
}

Reading read() {
    std::lock_guard lock(g_mutex);
    Reading r;
    for (const Finger &f : g_fingers) {
        if (!f.on || f.control < 0) continue;
        const Control &c = g_controls[f.control];
        switch (c.kind) {
        case Kind::button: r.buttons |= c.mask; break;
        case Kind::menu: break;
        case Kind::stick: {
            float dx = (f.x - c.cx) / c.r, dy = (f.y - c.cy) / c.r;
            const float len = std::hypot(dx, dy);
            if (len < 0.12f) break;  // a resting thumb is the middle
            if (len > 1) dx /= len, dy /= len;
            (c.mask ? r.rx : r.lx) = axis_value(dx);
            (c.mask ? r.ry : r.ly) = axis_value(dy);
            break;
        }
        case Kind::dpad: {
            const float dx = f.x - c.cx, dy = f.y - c.cy;
            if (std::hypot(dx, dy) < 0.25f * c.r) break;
            // eight ways: a direction is pressed when the finger is within 67.5 degrees of it, so the diagonals press two
            const float a = std::atan2(dy, dx);  // 0 right, pi/2 down
            const float k = 3.14159265f * 0.375f;
            const auto near = [&](float dir) { return std::abs(std::remainder(a - dir, 2 * 3.14159265f)) < k; };
            if (near(0)) r.buttons |= kRight;
            if (near(3.14159265f)) r.buttons |= kLeft;
            if (near(3.14159265f / 2)) r.buttons |= kDown;
            if (near(-3.14159265f / 2)) r.buttons |= kUp;
            break;
        }
        }
    }
    return r;
}

bool take_menu_tap() {
    std::lock_guard lock(g_mutex);
    const bool tap = g_menu_tap;
    g_menu_tap = false;
    return tap;
}

const quickmenu::Picture &picture() {
    std::lock_guard lock(g_mutex);
    if (g_picture_w != g_width || g_picture_h != g_height) {
        draw_all(g_picture);
        g_picture_w = g_width;
        g_picture_h = g_height;
    }
    return g_picture;
}

}  // namespace touchpad
