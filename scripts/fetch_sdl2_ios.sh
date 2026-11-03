#!/bin/sh
# Builds SDL2 for iOS from its source release (SDL has no iOS package to download) into third_party/sdl2-ios, or with the argument
# ios-sim into third_party/sdl2-ios-sim for the Simulator of an Apple Silicon Mac. Needs a Mac with Xcode, CMake and Ninja (docs/ios.md).
# The source's SHA-256 is checked before it is unpacked. Another version needs it as the second argument:
# scripts/fetch_sdl2_ios.sh ios 2.32.12 <sha256>.
set -e
pinned=2.32.10
kind=${1:-ios}
version=${2:-$pinned}
sha256=${3:-}
# from the release page (GitHub's digest for the file), checked by downloading it on 2026-11-03
[ -n "$sha256" ] || [ "$version" != "$pinned" ] || sha256=5f5993c530f084535c65a6879e9b26ad441169b3e25d789d83287040a9ca5165
[ -n "$sha256" ] || { echo "SDL2: no SHA-256 known for $version: pass it as the third argument" >&2; exit 1; }
case "$kind" in
  ios) sysroot=iphoneos ;;
  ios-sim) sysroot=iphonesimulator ;;
  *) echo "usage: scripts/fetch_sdl2_ios.sh [ios | ios-sim] [version sha256]" >&2; exit 2 ;;
esac
[ "$(uname -s)" = Darwin ] || { echo "SDL2 for iOS is built on a Mac with Xcode" >&2; exit 1; }
here=$(cd "$(dirname "$0")/.." && pwd)
target="$here/third_party/sdl2-$kind"
work="$here/build/sdl2-$kind-src"
deployment=${PS2_IOS_DEPLOYMENT:-15.0}
rm -rf "$work"
mkdir -p "$work"
url="https://github.com/libsdl-org/SDL/releases/download/release-$version/SDL2-$version.tar.gz"
(cd "$work" && curl -fL -o sdl2.tar.gz "$url") || { echo "SDL2: download failed: $url" >&2; exit 1; }
got=$(cd "$work" && shasum -a 256 sdl2.tar.gz | cut -d' ' -f1)
if [ "$got" != "$sha256" ]; then rm -rf "$work"; echo "SDL2: wrong SHA-256 $got (expected $sha256)" >&2; exit 1; fi
(cd "$work" && tar -xzf sdl2.tar.gz)
cmake -S "$work/SDL2-$version" -B "$work/build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_SYSROOT=$sysroot -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET="$deployment" \
  -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TEST=OFF -DCMAKE_INSTALL_PREFIX="$target" > "$work/cmake.log" 2>&1 || { tail -30 "$work/cmake.log"; exit 1; }
cmake --build "$work/build" > "$work/build.log" 2>&1 || { tail -30 "$work/build.log"; exit 1; }
rm -rf "$target"
cmake --install "$work/build" > /dev/null
rm -rf "$work"
echo "SDL2 $version for $kind in $target"
