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
#include <map>
#include <set>
#include <string>
#include <vector>

// The installed lapse binary, run as a child process with --json
struct Engine {
    std::string path;
    std::string version;
    std::set<std::string> formats;
};

// The configured path when there is one, else lapse on PATH. Empty path when
// neither runs
Engine find_engine(const std::string& configured);

struct Ran {
    int code = -1;
    std::string out, err;
};

// Runs the engine and waits for it. Storing a non zero value in *stop from
// another thread ends it early
Ran run_engine(const std::string& binary, const std::vector<std::string>& args,
               const std::atomic<bool>* stop = nullptr);

// The engine's --json line is one flat object, so this reads that and no more
struct Json {
    std::map<std::string, std::string> text;
    std::map<std::string, double> number;
    std::map<std::string, bool> flag;
    std::map<std::string, std::vector<double>> list;
};

bool read_json(const std::string& line, Json& out);
