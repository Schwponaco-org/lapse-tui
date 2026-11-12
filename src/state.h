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

#include <map>
#include <string>

#include "match.h"
#include "queue.h"

// what was decided by hand and what lapse said last, one record a line
struct State {
    Choices choices;
    std::map<std::string, Result> results;
};

State load_state(const std::string& file);
bool save_state(const std::string& file, const State& state);
