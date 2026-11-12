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

#include <string>
#include <vector>

// What a scene or library name says about the film, from
// "The.Thing.1982.1080p.BluRay.x264-GRP.en.forced" or "Heat (1995)"
struct Release {
    std::vector<std::string> title;  // lower case words
    int year = 0;
    int season = -1, episode = -1;
    std::string resolution, source, edition, group;
    std::string language;  // two letters where there are two
    bool forced = false, sdh = false;
};

// stem is the file name without its extension
Release parse_name(const std::string& stem);

std::string title_text(const Release& r);

// 0 to 1, how alike two titles are, with room for a typo or a word more
double title_likeness(const std::vector<std::string>& a, const std::vector<std::string>& b);
