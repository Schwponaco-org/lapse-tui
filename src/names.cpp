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

#include "names.h"

#include <algorithm>
#include <cstring>
#include <map>
#include <set>

namespace {

const std::map<std::string, std::string> LANGUAGES = {
    {"en", "en"}, {"eng", "en"}, {"english", "en"},
    {"da", "da"}, {"dan", "da"}, {"danish", "da"}, {"dansk", "da"},
    {"sv", "sv"}, {"swe", "sv"}, {"swedish", "sv"}, {"svenska", "sv"},
    {"no", "no"}, {"nor", "no"}, {"nb", "no"}, {"nob", "no"}, {"norwegian", "no"}, {"norsk", "no"},
    {"fi", "fi"}, {"fin", "fi"}, {"finnish", "fi"}, {"suomi", "fi"},
    {"is", "is"}, {"isl", "is"}, {"ice", "is"}, {"icelandic", "is"},
    {"de", "de"}, {"ger", "de"}, {"deu", "de"}, {"german", "de"}, {"deutsch", "de"},
    {"fr", "fr"}, {"fre", "fr"}, {"fra", "fr"}, {"french", "fr"}, {"francais", "fr"},
    {"es", "es"}, {"spa", "es"}, {"spanish", "es"}, {"espanol", "es"}, {"castellano", "es"},
    {"it", "it"}, {"ita", "it"}, {"italian", "it"}, {"italiano", "it"},
    {"nl", "nl"}, {"dut", "nl"}, {"nld", "nl"}, {"dutch", "nl"}, {"nederlands", "nl"},
    {"pt", "pt"}, {"por", "pt"}, {"portuguese", "pt"}, {"pob", "pt"},
    {"pl", "pl"}, {"pol", "pl"}, {"polish", "pl"}, {"polski", "pl"},
    {"ru", "ru"}, {"rus", "ru"}, {"russian", "ru"},
    {"cs", "cs"}, {"cze", "cs"}, {"ces", "cs"}, {"czech", "cs"},
    {"tr", "tr"}, {"tur", "tr"}, {"turkish", "tr"},
    {"ar", "ar"}, {"ara", "ar"}, {"arabic", "ar"},
    {"zh", "zh"}, {"chi", "zh"}, {"zho", "zh"}, {"chinese", "zh"}, {"chs", "zh"}, {"cht", "zh"},
    {"ja", "ja"}, {"jpn", "ja"}, {"japanese", "ja"},
    {"ko", "ko"}, {"kor", "ko"}, {"korean", "ko"},
    {"hu", "hu"}, {"hun", "hu"}, {"hungarian", "hu"},
    {"ro", "ro"}, {"rum", "ro"}, {"ron", "ro"}, {"romanian", "ro"},
    {"el", "el"}, {"gre", "el"}, {"ell", "el"}, {"greek", "el"},
    {"he", "he"}, {"heb", "he"}, {"hebrew", "he"},
    {"hi", "hi"}, {"hin", "hi"}, {"hindi", "hi"},
    {"th", "th"}, {"tha", "th"}, {"thai", "th"},
    {"uk", "uk"}, {"ukr", "uk"}, {"ukrainian", "uk"},
    {"hr", "hr"}, {"hrv", "hr"}, {"croatian", "hr"},
    {"sr", "sr"}, {"srp", "sr"}, {"serbian", "sr"},
    {"sl", "sl"}, {"slv", "sl"}, {"slovenian", "sl"},
    {"sk", "sk"}, {"slo", "sk"}, {"slk", "sk"}, {"slovak", "sk"},
    {"bg", "bg"}, {"bul", "bg"}, {"bulgarian", "bg"},
    {"vi", "vi"}, {"vie", "vi"}, {"vietnamese", "vi"},
    {"id", "id"}, {"ind", "id"}, {"indonesian", "id"},
    {"fa", "fa"}, {"per", "fa"}, {"fas", "fa"}, {"persian", "fa"},
};

const std::set<std::string> FORCED = {"forced", "foreign"};
const std::set<std::string> SDH = {"sdh", "cc", "hi", "hoh"};
const std::set<std::string> NOISE = {"default", "full", "sub", "subs", "subtitle", "subtitles", "track"};

const std::set<std::string> RESOLUTIONS = {"480p", "576p", "720p", "1080p", "1080i", "2160p", "4k", "uhd"};

const std::map<std::string, std::string> SOURCES = {
    {"bluray", "BluRay"}, {"blu", "BluRay"}, {"bdrip", "BDRip"}, {"brrip", "BRRip"},
    {"remux", "Remux"}, {"bdremux", "Remux"}, {"webrip", "WEBRip"}, {"webdl", "WEB-DL"},
    {"web", "WEB"}, {"hdtv", "HDTV"}, {"pdtv", "HDTV"}, {"dvdrip", "DVDRip"}, {"dvd", "DVD"},
    {"dvdr", "DVD"}, {"hdrip", "HDRip"}, {"amzn", "WEB"}, {"nf", "WEB"}, {"dsnp", "WEB"},
    {"hmax", "WEB"}, {"atvp", "WEB"}, {"hulu", "WEB"}, {"hdcam", "CAM"}, {"cam", "CAM"},
};

const std::set<std::string> TECH = {
    "x264", "x265", "h264", "h265", "hevc", "avc", "xvid", "divx", "10bit", "8bit", "hdr",
    "hdr10", "dv", "sdr", "aac", "ac3", "dts", "ddp5", "dd5", "atmos", "truehd", "flac",
    "proper", "repack", "internal", "limited", "multi", "dual", "dubbed", "subbed", "rerip",
};

const std::map<std::string, std::string> EDITIONS = {
    {"extended", "Extended"}, {"unrated", "Unrated"}, {"uncut", "Uncut"}, {"remastered", "Remastered"},
    {"directors", "Director's Cut"}, {"theatrical", "Theatrical"}, {"imax", "IMAX"}, {"criterion", "Criterion"},
};

struct Token {
    std::string text;
    bool dash_before = false;
};

std::vector<Token> tokens(const std::string& stem) {
    std::vector<Token> out;
    std::string word;
    bool dash = false;
    for (size_t i = 0; i <= stem.size(); i++) {
        char c = i < stem.size() ? stem[i] : ' ';
        // Ocean's is one word, not ocean and s
        if (c == '\'') continue;
        if (stem.compare(i, 3, "\xE2\x80\x99") == 0) {
            i += 2;
            continue;
        }
        if (c == '&') c = ' ';
        if (std::strchr(" ._-[](){}+,~!?:;#", c)) {
            if (!word.empty()) out.push_back({word, dash});
            word.clear();
            dash = c == '-';
            continue;
        }
        word += (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
    }
    return out;
}

bool digits(const std::string& s, size_t from, size_t to) {
    if (from >= to || to > s.size()) return false;
    for (size_t i = from; i < to; i++)
        if (s[i] < '0' || s[i] > '9') return false;
    return true;
}

int year_of(const std::string& w) {
    if (w.size() != 4 || !digits(w, 0, 4)) return 0;
    int y = std::stoi(w);
    return y >= 1900 && y <= 2099 ? y : 0;
}

// s01e02, s1e2, s01e02e03 and 1x02 in one word
bool episode_of(const std::string& w, int& season, int& episode) {
    size_t e = w.find('e');
    if (w.size() >= 4 && w[0] == 's' && e != std::string::npos && e >= 2 && e <= 3 && digits(w, 1, e)) {
        size_t end = e + 1;
        while (end < w.size() && end < e + 4 && w[end] >= '0' && w[end] <= '9') end++;
        if (end == e + 1 || (end < w.size() && w[end] != 'e')) return false;
        season = std::stoi(w.substr(1, e - 1));
        episode = std::stoi(w.substr(e + 1, end - e - 1));
        return true;
    }
    size_t x = w.find('x');
    if (x != std::string::npos && x >= 1 && x <= 2 && w.size() - x - 1 >= 2 && w.size() - x - 1 <= 3 &&
        digits(w, 0, x) && digits(w, x + 1, w.size())) {
        season = std::stoi(w.substr(0, x));
        episode = std::stoi(w.substr(x + 1));
        return true;
    }
    return false;
}

bool season_word(const std::string& w) {
    return w.size() >= 2 && w.size() <= 3 && w[0] == 's' && digits(w, 1, w.size());
}

bool episode_word(const std::string& w) {
    return w.size() >= 2 && w.size() <= 4 && w[0] == 'e' && digits(w, 1, w.size());
}

int edit_distance(const std::string& a, const std::string& b) {
    std::vector<int> row(b.size() + 1);
    for (size_t j = 0; j <= b.size(); j++) row[j] = (int)j;
    for (size_t i = 1; i <= a.size(); i++) {
        int diag = row[0];
        row[0] = (int)i;
        for (size_t j = 1; j <= b.size(); j++) {
            int up = row[j];
            row[j] = std::min({row[j] + 1, row[j - 1] + 1, diag + (a[i - 1] == b[j - 1] ? 0 : 1)});
            diag = up;
        }
    }
    return row[b.size()];
}

std::string joined(const std::vector<std::string>& words) {
    std::string out;
    for (auto& w : words) out += w;
    return out;
}

}

Release parse_name(const std::string& stem) {
    Release r;
    std::vector<Token> t = tokens(stem);

    // language and flags hang off the end: .en, .eng.forced, .English.SDH
    while (!t.empty()) {
        const std::string& w = t.back().text;
        auto lang = LANGUAGES.find(w);
        if (lang != LANGUAGES.end() && r.language.empty()) r.language = lang->second;
        else if (FORCED.count(w)) r.forced = true;
        else if (SDH.count(w)) r.sdh = true;
        else if (!NOISE.count(w)) break;
        t.pop_back();
    }

    // Subs/2_English.srt is a track number and a language, no title
    if (t.size() == 1 && !r.language.empty() && t[0].text.size() <= 2 && digits(t[0].text, 0, t[0].text.size())) t.clear();
    size_t end = t.size();
    size_t title_end = end;
    for (size_t i = 0; i < end; i++) {
        const std::string& w = t[i].text;
        bool marker = true;
        int season, episode;
        if (i > 0 && year_of(w) && !r.year) r.year = year_of(w);
        else if (r.season < 0 && episode_of(w, season, episode)) r.season = season, r.episode = episode;
        else if (r.season < 0 && season_word(w) && i + 1 < end && episode_word(t[i + 1].text)) {
            r.season = std::stoi(w.substr(1));
            r.episode = std::stoi(t[i + 1].text.substr(1));
            i++;
        } else if (RESOLUTIONS.count(w)) {
            if (r.resolution.empty()) r.resolution = w == "4k" || w == "uhd" ? "2160p" : w;
        } else if (SOURCES.count(w)) {
            if (r.source.empty()) r.source = SOURCES.at(w);
        } else if (EDITIONS.count(w)) {
            if (r.edition.empty()) r.edition = EDITIONS.at(w);
        } else if (!TECH.count(w)) {
            marker = false;
        }
        if (marker && title_end == end) title_end = i;
    }
    // the group follows the last dash, after the year or the tags: x264-SPARKS
    const Token* last = t.empty() ? nullptr : &t.back();
    if (last && last->dash_before && title_end + 1 < end && !RESOLUTIONS.count(last->text) &&
        !SOURCES.count(last->text) && !TECH.count(last->text) && !EDITIONS.count(last->text) && !year_of(last->text)) {
        size_t at = stem.rfind('-');
        r.group = at == std::string::npos ? last->text : stem.substr(at + 1, last->text.size());
    }

    for (size_t i = 0; i < title_end; i++) r.title.push_back(t[i].text);
    return r;
}

std::string title_text(const Release& r) {
    std::string out;
    for (auto& w : r.title) out += (out.empty() ? "" : " ") + w;
    return out;
}

double title_likeness(const std::vector<std::string>& a, const std::vector<std::string>& b) {
    if (a.empty() || b.empty()) return 0;
    if (a == b) return 1;
    std::multiset<std::string> left(a.begin(), a.end());
    int shared = 0;
    for (auto& w : b) {
        auto it = left.find(w);
        if (it == left.end()) continue;
        left.erase(it);
        shared++;
    }
    double dice = 2.0 * shared / (double)(a.size() + b.size());

    std::string x = joined(a), y = joined(b);
    size_t longest = std::max(x.size(), y.size());
    // a typo in a short title is a different title more often than not
    if (longest < 5) return dice;
    double typo = 1.0 - (double)edit_distance(x, y) / (double)longest;
    return std::max(dice, typo);
}
