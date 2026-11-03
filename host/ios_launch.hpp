#pragma once

// How the iOS app starts (docs/ios.md). An app gets no command line, so scripts/port_ios.sh writes the runner's arguments into the app,
// game/args.txt, one per line, with {bundle} for the app's game folder (the program and its patches, read-only) and {documents} for
// the app's Documents folder (the disc image the player copies in with the Files app or Finder, the memory card, quick saves, keys.ini,
// reports and log.txt).

#include <string>
#include <vector>

namespace ios_launch {

// The arguments from game/args.txt, argv0 first, with the folders filled in. False when the game cannot start (no args.txt, or no disc
// image in Documents): the player has been told why in a message box.
bool arguments(const char *argv0, std::vector<std::string> &out);

}  // namespace ios_launch
