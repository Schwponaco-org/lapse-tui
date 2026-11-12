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

#include "state.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <vector>

namespace {

std::vector<std::string> fields(const std::string& line) {
    std::vector<std::string> out;
    std::istringstream in(line);
    std::string f;
    while (std::getline(in, f, '\t')) out.push_back(f);
    return out;
}

}

State load_state(const std::string& file) {
    State s;
    std::ifstream in(file);
    std::string line;
    while (std::getline(in, line)) {
        auto f = fields(line);
        if (f.size() == 3 && f[0] == "pin") s.choices.pinned[f[1]] = f[2];
        else if (f.size() == 3 && f[0] == "refuse") s.choices.refused.insert(f[1] + "\t" + f[2]);
        else if (f.size() == 6 && f[0] == "result")
            s.results[f[1]] = {f[2], std::atoi(f[3].c_str()), f[4] == "dry", std::atoll(f[5].c_str())};
    }
    return s;
}

bool save_state(const std::string& file, const State& s) {
    std::string tmp = file + ".tmp";
    {
        std::ofstream out(tmp, std::ios::trunc);
        if (!out) return false;
        for (auto& [sub, video] : s.choices.pinned) out << "pin\t" << sub << '\t' << video << '\n';
        for (auto& pair : s.choices.refused) out << "refuse\t" << pair << '\n';
        for (auto& [sub, r] : s.results)
            out << "result\t" << sub << '\t' << r.verdict << '\t' << r.offset << '\t' << (r.dry ? "dry" : "real")
                << '\t' << r.when << '\n';
        if (!out.flush()) return false;
    }
    return std::rename(tmp.c_str(), file.c_str()) == 0;
}
