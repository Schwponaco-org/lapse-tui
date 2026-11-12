// lapse-tui - a terminal interface for the LAPSE subtitle sync engine
// Copyright (C) 2026 Rasmus Stisen Jensen (rs-jensen)
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <https://www.gnu.org/licenses/>.

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>

#include <climits>
#include <unistd.h>

#include "app.h"
#include "library.h"
#include "match.h"
#include "state.h"

namespace {

void usage(std::ostream& out) {
    out << "Usage: lapse-tui [options] [folder...]\n"
           "\n"
           "Finds the films and subtitles on this machine, pairs them, and syncs them\n"
           "with lapse. With folders it looks in those this time instead of the ones\n"
           "in the settings. Press ? inside for the keys.\n"
           "\n"
           "  --engine <path>   the lapse binary, when it is not on PATH\n"
           "  --scan            look through the folders, print what was found, and stop\n"
           "  --version         print the version\n";
}

std::string absolute(const std::string& path) {
    char buf[PATH_MAX];
    return realpath(path.c_str(), buf) ? std::string(buf) : "";
}

// what --scan prints: the same walk and pairing the screen does, as numbers
int scan_only(const Setup& s) {
    Index cached, found;
    load_index(s.index_file, cached);
    auto start = std::chrono::steady_clock::now();
    Walk walk;
    walk.roots = s.roots;
    walk.fences = s.fences;
    walk.found = [&](Dir&& d) { found.push_back(std::move(d)); };
    scan(walk, cached);
    double walked = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    save_index(s.index_file, found);
    Library lib = match(found, load_state(s.state_file).choices);

    int sure = 0, maybe = 0, loose = 0;
    for (auto& sub : lib.subs) {
        if (sub.video < 0) loose++;
        else if (sub.score >= SURE) sure++;
        else maybe++;
    }
    for (auto& r : s.roots) std::cout << "looked in " << r << '\n';
    std::cout << found.size() << " folders, " << walk.read << " read and " << walk.reused << " from the index, in "
              << walked << " s\n"
              << lib.videos.size() << " videos, " << lib.subs.size() << " subtitles: " << sure << " paired, " << maybe
              << " paired but unsure, " << loose << " with no film\n";
    return 0;
}

}

int main(int argc, char* argv[]) {
    Setup s;
    std::string dir = config_dir();
    s.config_file = dir + "/config";
    s.index_file = cache_dir() + "/index";
    s.state_file = state_dir() + "/state";
    s.config = load_config(s.config_file);
    std::string engine = s.config.engine;
    bool scan = false;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            usage(std::cout);
            return 0;
        } else if (arg == "--version") {
            std::cout << LAPSE_TUI_VERSION << '\n';
            return 0;
        } else if (arg == "--engine" && i + 1 < argc) {
            engine = argv[++i];
        } else if (arg == "--scan") {
            scan = true;
        } else if (!arg.empty() && arg[0] == '-') {
            std::cerr << "lapse-tui does not know " << arg << "\n";
            usage(std::cerr);
            return 1;
        } else {
            std::string path = absolute(arg);
            if (path.empty()) {
                std::cerr << "No such folder: " << arg << '\n';
                return 1;
            }
            s.roots.push_back(path);
            s.roots_given = true;
        }
    }

    for (auto& d : {dir, cache_dir(), state_dir()}) make_dirs(d);
    auto mounts = read_mounts();
    if (s.roots.empty()) s.roots = s.config.roots;
    if (s.roots.empty()) s.roots = default_roots(std::getenv("HOME") ? std::getenv("HOME") : "/", mounts, s.config.network);
    s.fences = fences(mounts, s.config.network);
    if (scan) return scan_only(s);

    if (!isatty(0) || !isatty(1)) {
        std::cerr << "lapse-tui draws on a terminal. For scripts, use lapse itself or lapse-tui --scan\n";
        return 1;
    }
    s.engine = find_engine(engine);
    const char* no_color = std::getenv("NO_COLOR");
    s.color = !(no_color && *no_color);
    return run_app(std::move(s));
}
