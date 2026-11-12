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

// Names come off the disk as bytes and are not always UTF-8, and a name with
// an escape in it should not get to move the cursor about
std::string shown(const std::string& raw);

int cells(const std::string& s);

// cut to w cells with an ellipsis at the end, at the front, or in the middle
std::string cut(const std::string& s, int w);
std::string cut_front(const std::string& s, int w);
std::string squeeze(const std::string& s, int w);

std::string pad(const std::string& s, int w);
std::string pad_left(const std::string& s, int w);

std::string plural(long long n, const std::string& one, const std::string& many);
std::string offset_text(int ms);
std::string size_text(long long bytes);
std::string lower(std::string s);

// -1 when the letters of query are not all in text in order, else higher
// for runs of them and for hits at the start of a word. query is lower case
int fuzzy_score(const std::string& query, const std::string& text);
