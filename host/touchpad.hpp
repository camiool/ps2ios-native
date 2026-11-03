#pragma once

// A PS2 controller on a touch screen (the iOS build, docs/ios.md): two sticks, the D-pad, the four face buttons, L1 L2 R1 R2, L3 R3,
// Select, Start and a MENU button that opens the quick menu. host/window.cpp feeds it SDL's finger events and ORs what it reads into
// the first controller port, next to the keyboard and a real controller. It is drawn as one picture the size of the window (mostly
// clear), laid over the game's picture the way the quick menu is (runtime/src/gs/gpu/overlay.hpp); the game never sees it.
//
// It knows nothing of SDL: fingers come in as an id and a place from 0 to 1 across the window, so it can be tested without a screen.

#include "quickmenu.hpp"

#include <cstdint>

namespace touchpad {

// The PS2's button bits, as host/window.cpp gives them to ps2::set_pad_buttons.
enum Button : std::uint16_t {
    kSelect = 1u << 0, kL3 = 1u << 1, kR3 = 1u << 2, kStart = 1u << 3,
    kUp = 1u << 4, kRight = 1u << 5, kDown = 1u << 6, kLeft = 1u << 7,
    kL2 = 1u << 8, kR2 = 1u << 9, kL1 = 1u << 10, kR1 = 1u << 11,
    kTriangle = 1u << 12, kCircle = 1u << 13, kCross = 1u << 14, kSquare = 1u << 15,
};

struct Reading {
    std::uint16_t buttons = 0;
    std::uint8_t lx = 0x80, ly = 0x80, rx = 0x80, ry = 0x80;  // 0x80 is the middle, 0 left / up, 0xFF right / down
};

// The window's size in pixels (the drawing and the places of the controls follow it). Call it before the first finger and at every
// change of size.
void set_window(int width, int height);

// SDL_FINGERDOWN, SDL_FINGERMOTION and SDL_FINGERUP: x and y from 0 to 1 across the window.
void finger_down(std::int64_t id, float x, float y);
void finger_move(std::int64_t id, float x, float y);
void finger_up(std::int64_t id);
void release_all();  // the menu opened, the app went to the background: every finger is let go

Reading read();

// True once after the MENU button was tapped (let go over it).
bool take_menu_tap();

// The picture for the window size of set_window: straight RGBA, the whole window, x = y = 0. Made again only when the size changed.
const quickmenu::Picture &picture();

}  // namespace touchpad
