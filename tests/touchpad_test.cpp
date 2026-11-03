// The touch controls of the iOS build (host/touchpad.cpp) without a screen: fingers at the places of the buttons and sticks press them,
// and the picture is drawn for phones and tablets held sideways. Build and run with:
//   clang++ -std=c++20 -O2 tests/touchpad_test.cpp host/touchpad.cpp host/quickmenu.cpp -o build/touchpadtest && build/touchpadtest
// An argument writes the picture of a 2556 x 1179 window (an iPhone held sideways) as a PPM file over grey, to look at.

#include "../host/touchpad.hpp"

#include <cstdio>
#include <cstdlib>

namespace {

int g_failed = 0;

void check(bool ok, const char *what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_failed;
}

// what one finger at x, y (window pixels) reads
touchpad::Reading tap(float x, float y, int w, int h) {
    touchpad::finger_down(1, x / static_cast<float>(w), y / static_cast<float>(h));
    const touchpad::Reading r = touchpad::read();
    touchpad::finger_up(1);
    return r;
}

void test_size(int w, int h) {
    std::printf("-- %d x %d\n", w, h);
    touchpad::set_window(w, h);
    const float u = std::min(h / 10.0f, w / 17.0f), mx = 0.6f * u + 0.03f * w;
    const float fx = w - mx - 2.4f * u, fy = 4.6f * u, off = 1.25f * u;
    check(tap(fx, fy + off, w, h).buttons == touchpad::kCross, "cross");
    check(tap(fx + off, fy, w, h).buttons == touchpad::kCircle, "circle");
    check(tap(fx, fy - off, w, h).buttons == touchpad::kTriangle, "triangle");
    check(tap(fx - off, fy, w, h).buttons == touchpad::kSquare, "square");
    check(tap(w / 2.0f + 2.1f * u, 0.8f * u, w, h).buttons == touchpad::kStart, "start");
    check(tap(w / 2.0f - 2.1f * u, 0.8f * u, w, h).buttons == touchpad::kSelect, "select");
    check(tap(mx + 1.3f * u, 2.1f * u, w, h).buttons == touchpad::kL1, "L1");
    check(tap(w - mx - 1.3f * u, 0.9f * u, w, h).buttons == touchpad::kR2, "R2");
    check(tap(w / 2.0f, h / 2.0f, w, h).buttons == 0, "nothing in the middle");
    // the D-pad: up, and down-right on the diagonal
    const float dx = mx + 2.4f * u, dy = 4.6f * u, dr = 1.7f * u;
    check(tap(dx, dy - 0.8f * dr, w, h).buttons == touchpad::kUp, "D-pad up");
    check(tap(dx + 0.6f * dr, dy + 0.6f * dr, w, h).buttons == (touchpad::kDown | touchpad::kRight), "D-pad down-right");
    // the left stick: a finger that starts on it and moves far right reads full right, and the stick keeps it off the buttons
    const float sx = mx + 4.2f * u, sy = 8.0f * u;
    touchpad::finger_down(7, sx / w, sy / h);
    touchpad::finger_move(7, (sx + 3 * u) / w, sy / h);
    touchpad::Reading r = touchpad::read();
    check(r.lx == 255 && r.ly == 0x80 && r.buttons == 0, "left stick right");
    // a second finger on Cross at the same time
    touchpad::finger_down(8, fx / w, (fy + off) / h);
    r = touchpad::read();
    check(r.lx == 255 && r.buttons == touchpad::kCross, "stick and cross together");
    // the thumb slides from Cross to Circle
    touchpad::finger_move(8, (fx + off) / w, fy / h);
    check(touchpad::read().buttons == touchpad::kCircle, "slide from cross to circle");
    touchpad::release_all();
    r = touchpad::read();
    check(r.buttons == 0 && r.lx == 0x80, "all let go");
    // the right stick, up
    const float rsx = w - mx - 4.2f * u;
    touchpad::finger_down(9, rsx / w, sy / h);
    touchpad::finger_move(9, rsx / w, (sy - 1.5f * u) / h);
    r = touchpad::read();
    check(r.ry == 0 && r.rx == 0x80, "right stick up");
    touchpad::finger_up(9);
    // MENU: a tap on it is taken once; a finger that slides off it is not a tap
    touchpad::finger_down(3, 0.5f, 0.8f * u / h);
    touchpad::finger_up(3);
    check(touchpad::take_menu_tap() && !touchpad::take_menu_tap(), "menu tap, once");
    touchpad::finger_down(3, 0.5f, 0.8f * u / h);
    touchpad::finger_move(3, 0.5f, 0.5f);
    touchpad::finger_up(3);
    check(!touchpad::take_menu_tap(), "menu slid off: no tap");
    const quickmenu::Picture &p = touchpad::picture();
    std::size_t drawn = 0;
    for (std::uint32_t px : p.rgba) drawn += (px >> 24) != 0;
    check(p.width == w && p.height == h && drawn > p.rgba.size() / 50 && drawn < p.rgba.size() / 2, "picture: the window's size, mostly clear");
}

}  // namespace

int main(int argc, char **argv) {
    test_size(2556, 1179);  // iPhone 15 Pro, sideways
    test_size(2732, 2048);  // iPad Pro 12.9", sideways
    test_size(1334, 750);   // iPhone SE, sideways
    if (argc > 1) {
        touchpad::set_window(2556, 1179);
        const quickmenu::Picture &p = touchpad::picture();
        if (std::FILE *f = std::fopen(argv[1], "wb")) {
            std::fprintf(f, "P6\n%d %d\n255\n", p.width, p.height);
            for (std::uint32_t px : p.rgba) {
                const unsigned a = px >> 24;
                for (int ch = 0; ch < 3; ++ch) std::fputc(static_cast<int>((((px >> (8 * ch)) & 255) * a + 60 * (255 - a)) / 255), f);
            }
            std::fclose(f);
        }
    }
    std::printf("%s\n", g_failed ? "FAILED" : "all passed");
    return g_failed ? 1 : 0;
}
