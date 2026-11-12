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
#include <set>
#include <string>
#include <vector>

#include "library.h"
#include "names.h"

// at or above SURE a pair is taken as it is, between MAYBE and SURE it is
// shown with a question mark and left out of "sync everything"
constexpr double SURE = 0.85;
constexpr double MAYBE = 0.5;

struct Video {
    std::string path, name;
    Release release;
    int64_t size = 0;
};

struct Subtitle {
    std::string path, name;
    Release release;
    bool backup = false;
    int video = -1;
    double score = 0;
    std::string why;
};

struct Choices {
    // subtitle path to video path, set by hand
    std::map<std::string, std::string> pinned;
    // "subtitle\tvideo" pairs turned down by hand
    std::set<std::string> refused;
};

struct Library {
    std::vector<Video> videos;     // sorted by title, year, episode
    std::vector<Subtitle> subs;    // sorted by path
};

Library match(const Index& index, const Choices& choices);

// how sure a subtitle belongs with a video, with a few words on why
double pair_score(const Subtitle& sub, const Video& video, std::string& why);

std::string dir_of(const std::string& path);
