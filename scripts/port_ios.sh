#!/usr/bin/env bash
# bash 4 or newer (${x,,} and more below); macOS ships bash 3.2 as /bin/bash
if [ -z "${BASH_VERSION:-}" ] || [ "${BASH_VERSINFO:-0}" -lt 4 ]; then
  echo "this script needs bash 4 or newer, this is ${BASH_VERSION:-not bash} (brew install bash, then run it again)" >&2; exit 1
fi
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
here="$ROOT/scripts"

usage() {
  cat <<'EOF'
usage: scripts/port_ios.sh <name> [options]

Builds the iOS app of a game that scripts/port.sh already ported on this Mac (games/<name>). The main
processor's code is translated again with every entry point and code window that the Mac's runs found,
the vector programs they recorded are compiled, and the runtime, the game and an iOS host (touch
controls, game controllers, Metal through wgpu-native) are built with Xcode into an app, plus an .ipa.

An iOS app may not compile code while it runs, so code the build does not know stops the game (the Mac
runner translates it on the spot instead). Play the game on the Mac first, as far as you can, so that
its code is found, then run this. See docs/ios.md.

options:
  --simulator        build for the iOS Simulator of this Mac instead of a device
  --team ID          Apple development team ID: signs the app so Xcode can install it (a free Apple
                     ID works too, for 7 days). Without it the device app is unsigned: sign and install
                     the .ipa with a sideloading tool (AltStore, Sideloadly) or open the Xcode project
  --bundle-id ID     the app's bundle identifier (default com.ps2native.<name>)
  --title TEXT       the name under the icon (default <name>)
  --widescreen       tell the game the TV is 16:9 (as port.sh --widescreen)
  --bundle-disc      put the disc image inside the app (a big app, but nothing to copy afterwards)
  --more-memory      ask iOS for the increased memory limit (the team needs that capability)
  --no-translate     use generated/<name> as it is
EOF
}

[ $# -ge 1 ] || { usage; exit 2; }
case "$1" in -h|--help) usage; exit 0 ;; esac
name="$1"; shift
if ! [[ "$name" =~ ^[A-Za-z0-9._-]+$ ]] || [[ "$name" == .* ]]; then
  echo "game name '$name': use letters, digits, . _ and - only, not starting with a dot" >&2; exit 2
fi
simulator=0; team=""; bundle_id=""; title="$name"; wide=0; bundle_disc=0; more_memory=OFF; translate=1
while [ $# -gt 0 ]; do
  case "$1" in
    --simulator) simulator=1 ;;
    --team) team="$2"; shift ;;
    --bundle-id) bundle_id="$2"; shift ;;
    --title) title="$2"; shift ;;
    --widescreen) wide=1 ;;
    --bundle-disc) bundle_disc=1 ;;
    --more-memory) more_memory=ON ;;
    --no-translate) translate=0 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "unknown option $1" >&2; exit 2 ;;
  esac
  shift
done

[ "$(uname -s)" = Darwin ] || { echo "an iOS app is built on a Mac with Xcode (here: $(uname -s))" >&2; exit 1; }
command -v xcodebuild > /dev/null || { echo "no xcodebuild: install Xcode from the App Store, then: sudo xcode-select -s /Applications/Xcode.app" >&2; exit 1; }
command -v cmake > /dev/null || { echo "no cmake: brew install cmake ninja" >&2; exit 1; }
if [ -z "$bundle_id" ]; then
  bundle_id="com.ps2native.$(echo "${name,,}" | tr -cd 'a-z0-9-')"
fi
[[ "$bundle_id" =~ ^[A-Za-z0-9.-]+$ ]] || { echo "bundle id '$bundle_id': letters, digits, . and - only" >&2; exit 2; }
# the title goes into Info.plist: no XML characters
title_plist=$(printf '%s' "$title" | tr -d '<>&"')

game="$ROOT/games/$name"
generated="$ROOT/generated/$name"
tools="$ROOT/build/tools"
[ -f "$game/disc/SYSTEM.CNF" ] || { echo "games/$name is not there: port the game on this Mac first, scripts/port.sh $name <disc image> --native-only" >&2; exit 1; }
[ -x "$tools/eerecomp" ] && [ -x "$tools/vurecomp" ] || "$here/setup.sh" --native-only

if [ "$simulator" = 1 ]; then kind=ios-sim; sdk=iphonesimulator; else kind=ios; sdk=iphoneos; fi
build="$ROOT/build/$kind-$name"
stage="$build/game"

echo "== [1/5] tools for $kind"
[ -f "$ROOT/third_party/wgpu-native-$kind/lib/libwgpu_native.a" ] || "$here/fetch_wgpu.sh" "$kind"
[ -f "$ROOT/third_party/sdl2-$kind/lib/libSDL2.a" ] || "$here/fetch_sdl2_ios.sh" "$kind"
echo "wgpu-native and SDL2 for $kind are in place"

echo "== [2/5] executable"
# the same program port.sh chose: core.elf (Gran Turismo 4), else the boot program, and the patched copy when patches were applied
boot=$(sed -n 's/^BOOT2 *= *//p' "$game/disc/SYSTEM.CNF" | tr -d '\r')
program=${boot#*\\}
program=${program%;*}
elf="$game/disc/$program"
[ ! -f "$game/core.elf" ] || elf="$game/core.elf"
[ ! -f "$game/patched/$(basename "$elf")" ] || elf="$game/patched/$(basename "$elf")"
[ -f "$elf" ] || { echo "${elf#$ROOT/} is missing: run scripts/port.sh $name again" >&2; exit 1; }
echo "${elf#$ROOT/}"

echo "== [3/5] machine code to C++, with what the Mac's runs found"
if [ "$translate" = 1 ]; then
  touch "$game/entries.txt"
  extra=()
  [ ! -d "$game/modules" ] || extra+=(--modules "$game/modules")
  [ ! -d "$game/overlays" ] || extra+=(--overlays "$game/overlays")
  [ ! -f "$game/toggles.txt" ] || extra+=(--toggles "$game/toggles.txt")
  mkdir -p "$generated"
  "$tools/eerecomp" "$elf" --entries "$game/entries.txt" "${extra[@]}" --out "$generated" | tail -1
  echo "$(wc -l < "$game/entries.txt" | tr -d ' ') entry points in games/$name/entries.txt"
  if compgen -G "$game/vu1/*.bin" > /dev/null; then
    "$tools/vurecomp" "$game/vu1" "$generated" | tail -1
  else
    rm -f "$generated"/vu1_*.cpp
    echo "no vector programs recorded: the app interprets them (slower). Playing on the Mac records them into games/$name/vu1"
  fi
else
  [ -d "$generated" ] || { echo "generated/$name is not there: leave out --no-translate" >&2; exit 1; }
  echo "skipped"
fi

echo "== [4/5] the app's game folder"
rm -rf "$stage"
mkdir -p "$stage"
cp "$elf" "$stage/"
{
  echo "#title $title"
  echo "{bundle}/$(basename "$elf")"
  echo "--disc"
  echo "{documents}/disc.iso"
  echo "--memcard"
  echo "{documents}/$name.ps2"
  echo "--quick-dir"
  echo "{documents}"
  echo "--window"
  echo "--fullscreen"
  [ "$wide" = 0 ] || echo "--widescreen"
  if [ -f "$game/patches.txt" ]; then
    cp "$game/patches.txt" "$stage/patches.txt"
    echo "--patches"
    echo "{bundle}/patches.txt"
  fi
} > "$stage/args.txt"
if [ "$bundle_disc" = 1 ]; then
  cp "$(cd "$game" && pwd -P)/disc.iso" "$stage/disc.iso" 2> /dev/null || cp -L "$game/disc.iso" "$stage/disc.iso"
  echo "the disc image goes into the app ($(du -h "$stage/disc.iso" | cut -f1))"
fi
cat "$stage/args.txt"

echo "== [5/5] Xcode build ($sdk)"
config=(-DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET="${PS2_IOS_DEPLOYMENT:-15.0}"
  -DCMAKE_OSX_SYSROOT=$sdk -DPS2_IOS_SIMULATOR=$([ "$simulator" = 1 ] && echo ON || echo OFF)
  -DPS2_GENERATED="$generated" -DPS2_IOS_GAME_DIR="$stage" -DPS2_IOS_NAME="$title_plist" -DPS2_IOS_BUNDLE_ID="$bundle_id"
  -DPS2_IOS_TEAM="$team" -DPS2_IOS_MORE_MEMORY=$more_memory)
cmake -S "$ROOT" -B "$build/xcode" -G Xcode "${config[@]}" > "$build/cmake.log" 2>&1 || { tail -30 "$build/cmake.log"; exit 1; }
signing=()
if [ "$simulator" = 1 ]; then
  signing=(CODE_SIGN_IDENTITY=- CODE_SIGNING_REQUIRED=NO)
elif [ -n "$team" ]; then
  signing=(-allowProvisioningUpdates)
else
  signing=(CODE_SIGNING_ALLOWED=NO)
fi
jobs=${PS2_JOBS:-$(sysctl -n hw.ncpu)}
echo "building with $jobs jobs (the generated code is large: this takes a while), log in ${build#$ROOT/}/xcode.log"
xcodebuild -project "$build/xcode/ps2-native-recomp.xcodeproj" -target ps2run -configuration Release -sdk $sdk -jobs "$jobs" "${signing[@]}" \
  > "$build/xcode.log" 2>&1 || { grep -E "error:|BUILD FAILED" "$build/xcode.log" | head -30; tail -15 "$build/xcode.log"; exit 1; }
app="$build/xcode/Release-$sdk/ps2run.app"
[ -d "$app" ] || app=$(find "$build/xcode" -name ps2run.app -type d -path "*Release-$sdk*" | head -1)
[ -d "$app" ] || { echo "the build ended but no ps2run.app was found under ${build#$ROOT/}/xcode" >&2; exit 1; }

echo
echo "Done: ${app#$ROOT/}"
if [ "$simulator" = 1 ]; then
  cat <<EOF
On the Simulator (open Simulator.app and start an iPhone or iPad first):
  xcrun simctl install booted "${app#$ROOT/}"
  cp games/$name/disc.iso "\$(xcrun simctl get_app_container booted $bundle_id data)/Documents/disc.iso"
  xcrun simctl launch booted $bundle_id
EOF
else
  rm -rf "$build/ipa"
  mkdir -p "$build/ipa/Payload"
  cp -R "$app" "$build/ipa/Payload/"
  (cd "$build/ipa" && zip -qry "../$name.ipa" Payload)
  rm -rf "$build/ipa"
  echo "and as an .ipa: ${build#$ROOT/}/$name.ipa"
  if [ -n "$team" ]; then
    cat <<EOF
Install it on a connected iPhone or iPad (Developer Mode on, docs/ios.md):
  xcrun devicectl list devices
  xcrun devicectl device install app --device <identifier> "${app#$ROOT/}"
EOF
  else
    echo "It is not signed: sign and install the .ipa with AltStore or Sideloadly, or build again with --team <your team ID>."
  fi
  [ "$bundle_disc" = 1 ] || echo "Then copy the disc image into the app: Finder > the device > Files > $title (or the Files app on the device)."
fi
