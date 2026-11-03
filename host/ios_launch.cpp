#include "ios_launch.hpp"

#include "ps2/os.hpp"

#include <SDL.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <unistd.h>

namespace fs = std::filesystem;

namespace ios_launch {
namespace {

constexpr const char *kReadmeName = "Put the disc image here.txt";

std::string lower(std::string s) {
    for (char &c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

void replace_all(std::string &s, const std::string &from, const std::string &to) {
    for (std::size_t at = s.find(from); at != std::string::npos; at = s.find(from, at + to.size())) s.replace(at, from.size(), to);
}

void tell(const std::string &title, const std::string &text) {
    std::fprintf(stderr, "[ios] %s: %s\n", title.c_str(), text.c_str());
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, title.c_str(), text.c_str(), nullptr);
}

// The disc image: the one args.txt names, else the only .iso in Documents (any name), else the first by name. Empty when there is none.
fs::path find_disc(const fs::path &named, const fs::path &documents, const fs::path &bundle) {
    std::error_code error;
    if (fs::is_regular_file(named, error)) return named;
    if (fs::is_regular_file(bundle / "disc.iso", error)) return bundle / "disc.iso";  // port_ios.sh --bundle-disc
    std::vector<fs::path> found;
    for (const fs::directory_entry &e : fs::directory_iterator(documents, error))
        if (e.is_regular_file(error) && lower(e.path().extension().string()) == ".iso") found.push_back(e.path());
    std::sort(found.begin(), found.end());
    if (found.size() > 1) std::fprintf(stderr, "[ios] %zu disc images in Documents and none named %s: %s is used\n", found.size(), named.filename().string().c_str(), found.front().filename().string().c_str());
    return found.empty() ? fs::path() : found.front();
}

}  // namespace

bool arguments(const char *argv0, std::vector<std::string> &out) {
    SDL_Init(SDL_INIT_VIDEO);  // for the message boxes below; open_window initializes the rest (SDL counts the calls)
    std::error_code error;
    fs::path base;
    if (char *b = SDL_GetBasePath()) {  // the app bundle, where the game folder is
        base = b;
        SDL_free(b);
    }
    const fs::path bundle = base / "game";
    const char *home = std::getenv("HOME");  // the app's own container on iOS
    const fs::path documents = fs::path(home ? home : ".") / "Documents";
    fs::create_directories(documents, error);
    chdir(documents.c_str());  // anything written with a relative name lands where the player can see it
    // the runner's log in Documents/log.txt, for a report from a phone without Xcode (PS2_IOS_CONSOLE=1 keeps it on the console)
    if (ps2::os::env("PS2_IOS_CONSOLE").empty()) {
        const std::string log = (documents / "log.txt").string();
        if (std::freopen(log.c_str(), "w", stderr)) std::setvbuf(stderr, nullptr, _IOLBF, 0);
    }
    // pad recordings (docs/replay.md) would fill the phone: off unless asked for
    if (ps2::os::env("PS2_RECORD").empty()) ps2::os::set_env("PS2_RECORD", "0");

    std::ifstream file(bundle / "args.txt");
    if (!file) {
        tell("This app has no game", "game/args.txt is missing from the app. Build it again with scripts/port_ios.sh.");
        return false;
    }
    std::string title = "the game";
    out.assign(1, argv0 ? argv0 : "ps2run");
    for (std::string line; std::getline(file, line);) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        if (line.rfind("#title ", 0) == 0) {
            title = line.substr(7);
            continue;
        }
        if (line[0] == '#') continue;
        replace_all(line, "{bundle}", bundle.string());
        replace_all(line, "{documents}", documents.string());
        out.push_back(line);
    }
    for (std::size_t i = 1; i + 1 < out.size(); ++i) {
        if (out[i] != "--disc") continue;
        const fs::path disc = find_disc(fs::path(out[i + 1]), documents, bundle);
        if (disc.empty()) {
            const fs::path readme = documents / kReadmeName;
            if (!fs::exists(readme, error)) {
                std::ofstream note(readme);
                note << "Copy the disc image (.iso) of " << title << " into this folder, then open the app again.\n"
                     << "The memory card, the quick saves, keys.ini and log.txt are kept here too.\n";
            }
            tell("Disc image needed",
                 "Copy the disc image (.iso) of " + title + " into this app's folder, then open the app again.\n\n"
                 "On the iPhone or iPad: Files > On My iPhone (or iPad) > this app.\n"
                 "From a Mac: Finder > your device > Files > this app.");
            return false;
        }
        out[i + 1] = disc.string();
        std::fprintf(stderr, "[ios] disc image %s\n", out[i + 1].c_str());
    }
    std::fprintf(stderr, "[ios] %s: program %s, files in %s\n", title.c_str(), out.size() > 1 ? out[1].c_str() : "?", documents.string().c_str());
    return out.size() > 1;
}

}  // namespace ios_launch
