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

#include <set>
#include <string>
#include <vector>

struct Config {
    // where to look for films, the usual places when there are none
    std::vector<std::string> roots;
    bool network = false;
    std::string engine;
};

// $XDG_CONFIG_HOME, $XDG_CACHE_HOME and $XDG_STATE_HOME with lapse-tui on
// the end, or the folders under ~/Library on macOS
std::string config_dir();
std::string cache_dir();
std::string state_dir();

Config load_config(const std::string& file);
bool save_config(const std::string& file, const Config& config);

struct Mount {
    std::string point, type;
};

std::vector<Mount> read_mounts();
std::vector<Mount> parse_mounts(const std::string& proc_mounts);
bool is_network(const std::string& type);

// home, the media folders and every disk mounted outside the system folders
std::vector<std::string> default_roots(const std::string& home, const std::vector<Mount>& mounts, bool network);

// the network shares to stay out of while walking, none when they are wanted
std::set<std::string> fences(const std::vector<Mount>& mounts, bool network);

bool make_dirs(const std::string& path);
