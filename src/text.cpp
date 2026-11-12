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

#include "text.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include <ftxui/screen/string.hpp>

namespace {

// each glyph with the cells it takes, a wide one keeps both of its cells
std::vector<std::pair<std::string, int>> glyphs(const std::string& s) {
    std::vector<std::pair<std::string, int>> out;
    for (auto& g : ftxui::Utf8ToGlyphs(s)) {
        if (g.empty()) {
            if (!out.empty()) out.back().second = 2;
            continue;
        }
        out.push_back({g, 1});
    }
    return out;
}

std::string first_cells(const std::vector<std::pair<std::string, int>>& g, int w) {
    std::string out;
    int used = 0;
    for (auto& [text, width] : g) {
        if (used + width > w) break;
        out += text;
        used += width;
    }
    return out;
}

std::string last_cells(const std::vector<std::pair<std::string, int>>& g, int w) {
    std::string out;
    int used = 0;
    for (auto it = g.rbegin(); it != g.rend(); ++it) {
        if (used + it->second > w) break;
        out = it->first + out;
        used += it->second;
    }
    return out;
}

bool word_start(const std::string& text, size_t i) {
    if (i == 0) return true;
    char before = text[i - 1];
    return before == ' ' || before == '.' || before == '_' || before == '-' || before == '/' || before == '(' ||
           before == '[';
}

}

std::string shown(const std::string& raw) {
    std::string out;
    size_t i = 0;
    while (i < raw.size()) {
        unsigned char c = (unsigned char)raw[i];
        int len = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 0;
        bool good = len > 0 && i + len <= raw.size();
        uint32_t code = len == 1 ? c : len == 2 ? (c & 0x1F) : len == 3 ? (c & 0x0F) : (c & 0x07);
        for (int k = 1; good && k < len; k++) {
            unsigned char next = (unsigned char)raw[i + k];
            good = (next >> 6) == 0x2;
            code = (code << 6) | (next & 0x3F);
        }
        if (good && len > 1) {
            static const uint32_t least[] = {0, 0, 0x80, 0x800, 0x10000};
            good = code >= least[len] && code <= 0x10FFFF && !(code >= 0xD800 && code <= 0xDFFF);
        }
        if (!good) {
            out += "\xEF\xBF\xBD";
            i++;
            continue;
        }
        if (code < 0x20 || code == 0x7F || (code >= 0x80 && code < 0xA0))
            out += code == '\t' ? ' ' : '?';
        else
            out.append(raw, i, len);
        i += len;
    }
    return out;
}

int cells(const std::string& s) {
    return ftxui::string_width(s);
}

std::string cut(const std::string& s, int w) {
    if (w <= 0) return "";
    if (cells(s) <= w) return s;
    return first_cells(glyphs(s), w - 1) + "…";
}

std::string cut_front(const std::string& s, int w) {
    if (w <= 0) return "";
    if (cells(s) <= w) return s;
    return "…" + last_cells(glyphs(s), w - 1);
}

std::string squeeze(const std::string& s, int w) {
    if (w <= 0) return "";
    if (cells(s) <= w) return s;
    auto g = glyphs(s);
    std::string end = last_cells(g, (w - 1) / 2);
    return first_cells(g, w - 1 - cells(end)) + "…" + end;
}

std::string pad(const std::string& s, int w) {
    std::string out = cut(s, w);
    return out + std::string(std::max(0, w - cells(out)), ' ');
}

std::string pad_left(const std::string& s, int w) {
    std::string out = cut(s, w);
    return std::string(std::max(0, w - cells(out)), ' ') + out;
}

std::string plural(long long n, const std::string& one, const std::string& many) {
    return std::to_string(n) + " " + (n == 1 ? one : many);
}

std::string offset_text(int ms) {
    char buf[32];
    if (std::abs(ms) < 100000) std::snprintf(buf, sizeof(buf), "%+d ms", ms);
    else std::snprintf(buf, sizeof(buf), "%+.1f s", ms / 1000.0);
    return ms == 0 ? "0 ms" : buf;
}

std::string size_text(long long bytes) {
    char buf[32];
    if (bytes >= 1000000000) std::snprintf(buf, sizeof(buf), "%.1f GB", bytes / 1e9);
    else if (bytes >= 1000000) std::snprintf(buf, sizeof(buf), "%.0f MB", bytes / 1e6);
    else std::snprintf(buf, sizeof(buf), "%lld kB", bytes / 1000);
    return buf;
}

std::string lower(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    return s;
}

int fuzzy_score(const std::string& query, const std::string& text) {
    int score = 0, run = 0;
    size_t at = 0;
    for (char q : query) {
        if (q == ' ') {
            run = 0;
            continue;
        }
        size_t found = std::string::npos;
        for (size_t i = at; i < text.size(); i++) {
            char c = text[i];
            if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
            if (c == q) {
                found = i;
                break;
            }
        }
        if (found == std::string::npos) return -1;
        run = found == at && at > 0 ? run + 1 : 0;
        score += 1 + run * 3 + (word_start(text, found) ? 4 : 0) - (int)std::min<size_t>(found - at, 8) / 2;
        at = found + 1;
    }
    return score;
}
