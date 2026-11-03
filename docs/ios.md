# iOS

A game you ported on a Mac can also be built as an iPhone or iPad app: one app per game, with touch controls, game controller
support and Metal graphics through wgpu-native. **This is new and has not been tested on a device yet.** The build, the touch
controls and the runtime compile, but nobody has played a game on an iPhone with it so far. Reports are very welcome.

## What is different from the desktop

- **The app is built on a Mac.** Apple's tools (Xcode) only run on macOS. The recompiler (`eerecomp`, `vurecomp`) runs on the
  Mac too; only the runtime and the translated game are compiled for the phone.
- **No translating while you play.** On the desktop, code the first build did not see is translated and compiled while you play
  (`docs/ee-live.md`, `docs/vu-live.md`). An iOS app may not run a compiler or load code it made itself, so that is switched off
  (`PS2_NO_LIVE`):
  - main processor code the build does not know **stops the game** ("missing code" in `log.txt`);
  - vector unit programs the build does not know run in the **interpreter** (slower, but they work).

  So: **play the game on the Mac first**, as far as you can. Everything the Mac's runner finds goes into the game folder
  (`games/<name>/entries.txt`, `overlays/`, `vu1/`), and `scripts/port_ios.sh` translates the game again with all of it.
  If the iOS app stops somewhere, play that part on the Mac and build the app again.
- **Files live in the app's Documents folder**, which you see in the Files app (On My iPhone > the game) and in Finder (the device >
  Files): the disc image, the memory card, quick saves, `keys.ini`, reports and `log.txt` (the runner's log of the last start).
- **Going to the background** opens the quick menu and writes the memory card; choose *Resume* when you come back.

## What you need

- A Mac (Apple Silicon or Intel) with Xcode, Homebrew's `bash`, `cmake`, `ninja` and `python3`.
- The game already ported on that Mac: `bash scripts/port.sh mygame "My Game.iso" --native-only`, then played for a while
  with the line it prints.
- To install on a device: an Apple ID in Xcode (a free one works; its apps run for 7 days before they must be installed again),
  and Developer Mode on the device (Settings > Privacy & Security > Developer Mode).

## Build

```sh
bash scripts/port_ios.sh mygame --team ABCDE12345
```

The team ID is in Xcode > Settings > Accounts (or developer.apple.com > Membership). The script:

1. downloads wgpu-native for iOS and builds SDL2 for iOS (once, into `third_party/`),
2. translates the game again with what the Mac's runs found, and compiles the recorded vector programs,
3. puts the game's program, its patches and `args.txt` (the runner's arguments) into the app,
4. builds the app with Xcode into `build/ios-mygame/xcode/Release-iphoneos/ps2run.app`, and an `.ipa` next to it.

Then install it and copy the disc image in:

```sh
xcrun devicectl list devices
xcrun devicectl device install app --device <identifier> build/ios-mygame/xcode/Release-iphoneos/ps2run.app
```

In Finder, select the device, open *Files*, and drag the game's `.iso` onto the app (any name works). Start the app.

Options (`scripts/port_ios.sh --help`):

| Option | What it does |
|---|---|
| `--team ID` | signs the app so Xcode can install it. Without it the app is unsigned: sign the `.ipa` with AltStore or Sideloadly |
| `--simulator` | builds for the iOS Simulator of an Apple Silicon Mac; the script prints how to install it and copy the disc in |
| `--bundle-id ID` | the app's identifier (default `com.ps2native.<name>`); a free Apple ID may need one of your own |
| `--title TEXT` | the name under the icon |
| `--widescreen` | as `port.sh --widescreen` |
| `--bundle-disc` | puts the disc image inside the app (a big app, nothing to copy) |
| `--more-memory` | asks iOS for a higher memory limit; needs the *Increased Memory Limit* capability on your team |
| `--no-translate` | builds `generated/<name>` as it is |

To change something by hand, open `build/ios-mygame/xcode/ps2-native-recomp.xcodeproj` in Xcode and run the `ps2run` scheme.

## Playing

- **Touch controls** are shown while no controller is connected: D-pad and left stick on the left, face buttons and right stick
  on the right, L1/L2/R1/R2 in the top corners, L3/R3 beside the sticks, Select/Start at the top. A thumb can slide from one face
  button to the next. **MENU** opens the quick menu (save, load, picture look, resolution, quit); tap its entries.
- **Game controllers** (MFi, Xbox, DualShock 4, DualSense) work as on the desktop, and the touch controls go away while one is
  connected. Select then Start opens the quick menu.
- Twice the resolution (the quick menu, or `scale` in `keys.ini`) is for recent iPads and iPhones only.

## Reporting

`log.txt` in the app's folder holds the log of the last start; quick menu > report (or F9 on a keyboard) makes a folder in
`reports/` as on the desktop. Please say which device and iOS version you used.

## How it is put together

| Part | Where |
|---|---|
| iOS in CMake: no host tools, no live compiling, the app bundle, SDL2 and wgpu-native for iOS | `CMakeLists.txt` (`PS2_IOS`) |
| The app's start: arguments from `game/args.txt`, the Documents folder, the disc image, `log.txt` | `host/ios_launch.cpp` |
| The touch controls | `host/touchpad.cpp`, test: `tests/touchpad_test.cpp` |
| UIKit's Metal layer, fingers, going to the background | `host/window.cpp` (`PS2_TOUCH`, `PS2_IOS`) |
| Live compiling left out | `runtime/src/ee_live.cpp`, `vu_live.cpp`, `live_build.cpp` (`PS2_NO_LIVE`) |
| Info.plist and entitlements | `ios/` |
| Build scripts | `scripts/port_ios.sh`, `scripts/fetch_sdl2_ios.sh`, `scripts/fetch_wgpu.sh ios` |
| CI: the runtime and the iOS host compiled for iPhone | `.github/workflows/ci.yml`, job `ios-arm64` |

The game's code is the same C++ as on the desktop, compiled for arm64 with `-ffp-contract=off` (no fused multiply-add, as the
PS2's FPU). The pictures go through the same WebGPU renderer, on Metal.

The app is for your own device and your own disc. It contains the game's translated code, so do not share it, and it cannot go
on the App Store.
