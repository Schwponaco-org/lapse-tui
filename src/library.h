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

#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <set>
#include <string>
#include <vector>

enum class Kind { Other, Video, Subtitle, Backup };

struct File {
    std::string name;
    Kind kind = Kind::Other;
    int64_t size = 0;
};

// One folder as it was last read. Folders with no films in them are kept
// too, for the folders under them
struct Dir {
    std::string path;
    int64_t mtime = 0;
    std::vector<std::string> subdirs;
    std::vector<File> files;
};

using Index = std::vector<Dir>;

Kind kind_of(const std::string& name);

struct Walk {
    std::vector<std::string> roots;
    // mount points not to go into, network shares unless asked for
    std::set<std::string> fences;
    std::function<void(Dir&&)> found;
    const std::atomic<bool>* stop = nullptr;
    std::atomic<int> read{0}, reused{0};
};

// Walks the roots and hands every folder to found. A folder whose time has
// not changed since the index was saved is taken from it without reading it
void scan(Walk& walk, const Index& cached);

// reads one folder the way scan does, without the folders under it
bool read_folder(const std::string& path, Dir& dir);

bool load_index(const std::string& file, Index& out);
bool save_index(const std::string& file, const Index& index);
