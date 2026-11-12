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

#include "match.h"

#include <algorithm>
#include <unordered_map>

namespace {

const std::set<std::string> SUBS_DIRS = {"subs", "sub", "subtitles", "subtitle", "undertekster"};

// a word this common in titles says nothing about which film it is
constexpr size_t COMMON_WORD = 200;

std::string base_of(const std::string& path) {
    size_t slash = path.rfind('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string stem_of(const std::string& name) {
    size_t dot = name.rfind('.');
    return dot == std::string::npos || dot == 0 ? name : name.substr(0, dot);
}

std::string lower(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    return s;
}

bool subs_dir(const std::string& dir) {
    return SUBS_DIRS.count(lower(base_of(dir))) > 0;
}

// the folder a subtitle's film would be in: its own, or the one above
// Subs/ and Subs/<film>/
std::string film_dir_of(const std::string& sub_dir) {
    if (subs_dir(sub_dir)) return dir_of(sub_dir);
    std::string up = dir_of(sub_dir);
    if (subs_dir(up)) return dir_of(up);
    return sub_dir;
}

// the file name says little in a Subs folder or a bare name like movie.mkv,
// so the folder fills in what the name leaves out
void fill_from(Release& r, const std::string& folder) {
    if (r.year || r.season >= 0) return;
    Release f = parse_name(base_of(folder));
    if (f.season >= 0) return;
    if (r.title.empty() || (f.year && !f.title.empty())) r.title = f.title;
    r.year = f.year;
}

bool named_after(const std::string& sub_name, const std::string& video_name) {
    std::string a = lower(stem_of(sub_name)), b = lower(stem_of(video_name));
    if (a.compare(0, b.size(), b) != 0) return false;
    return a.size() == b.size() || std::string("._ -").find(a[b.size()]) != std::string::npos;
}

}

std::string dir_of(const std::string& path) {
    size_t slash = path.rfind('/');
    if (slash == std::string::npos) return ".";
    return slash == 0 ? "/" : path.substr(0, slash);
}

double pair_score(const Subtitle& sub, const Video& video, std::string& why) {
    std::string sd = dir_of(sub.path), vd = dir_of(video.path);
    bool local = film_dir_of(sd) == vd;
    const Release &s = sub.release, &v = video.release;

    if (local && (named_after(sub.name, video.name) || (sd != vd && lower(base_of(sd)) == lower(stem_of(video.name))))) {
        why = "named after the video";
        return 1.0;
    }
    if (s.season >= 0 && v.season >= 0 && (s.season != v.season || s.episode != v.episode)) {
        why = "another episode";
        return 0;
    }
    if (s.year && v.year && s.year != v.year) {
        why = "another year";
        return 0;
    }
    double like = title_likeness(s.title, v.title);
    if (s.season >= 0 && v.season >= 0) {
        if (local) {
            why = "the same episode";
            return 0.95;
        }
        why = "the same episode of a show by that name elsewhere";
        return like >= 0.8 ? 0.6 + 0.2 * like : 0;
    }
    if (local) {
        why = "a name like the video's";
        return std::min(0.95, 0.35 + 0.5 * like + (s.year && s.year == v.year ? 0.1 : 0));
    }
    if (like < 0.75) {
        why = "nothing alike";
        return 0;
    }
    why = s.year && v.year ? "the same title and year, in another folder" : "a title like it, in another folder";
    return 0.2 + 0.4 * like + (s.year && v.year ? 0.2 : 0);
}

Library match(const Index& index, const Choices& choices) {
    Library lib;
    std::unordered_map<std::string, std::set<std::string>> backups;
    for (auto& d : index) {
        for (auto& f : d.files) {
            std::string path = d.path == "/" ? "/" + f.name : d.path + "/" + f.name;
            if (f.kind == Kind::Video) {
                Video v{path, f.name, parse_name(stem_of(f.name)), f.size};
                fill_from(v.release, d.path);
                lib.videos.push_back(std::move(v));
            } else if (f.kind == Kind::Subtitle) {
                Subtitle s;
                s.path = path;
                s.name = f.name;
                s.release = parse_name(stem_of(f.name));
                fill_from(s.release, film_dir_of(d.path));
                lib.subs.push_back(std::move(s));
            } else if (f.kind == Kind::Backup) {
                backups[d.path].insert(f.name.substr(0, f.name.size() - 4));
            }
        }
    }

    std::sort(lib.videos.begin(), lib.videos.end(), [](const Video& a, const Video& b) {
        const Release &x = a.release, &y = b.release;
        if (x.title != y.title) return x.title < y.title;
        if (x.year != y.year) return x.year < y.year;
        if (x.season != y.season) return x.season < y.season;
        if (x.episode != y.episode) return x.episode < y.episode;
        return a.path < b.path;
    });
    std::sort(lib.subs.begin(), lib.subs.end(), [](const Subtitle& a, const Subtitle& b) { return a.path < b.path; });

    std::unordered_map<std::string, std::vector<int>> in_dir, with_word;
    std::unordered_map<std::string, int> by_path;
    for (int i = 0; i < (int)lib.videos.size(); i++) {
        const Video& v = lib.videos[i];
        in_dir[dir_of(v.path)].push_back(i);
        by_path[v.path] = i;
        for (auto& w : std::set<std::string>(v.release.title.begin(), v.release.title.end())) with_word[w].push_back(i);
    }

    for (auto& s : lib.subs) {
        s.backup = backups[dir_of(s.path)].count(s.name) > 0;
        auto pin = choices.pinned.find(s.path);
        if (pin != choices.pinned.end() && by_path.count(pin->second)) {
            s.video = by_path[pin->second];
            s.score = 1;
            s.why = "paired by hand";
            continue;
        }

        std::vector<int> local;
        auto here = in_dir.find(film_dir_of(dir_of(s.path)));
        if (here != in_dir.end()) local = here->second;
        auto refused = [&](int v) { return choices.refused.count(s.path + "\t" + lib.videos[v].path) > 0; };
        local.erase(std::remove_if(local.begin(), local.end(), refused), local.end());

        std::vector<std::pair<double, int>> scored;
        std::unordered_map<int, std::string> whys;
        for (int v : local) scored.push_back({pair_score(s, lib.videos[v], whys[v]), v});

        // one video in the folder, nothing against it, it is that one
        std::vector<int> open;
        for (auto& [score, v] : scored)
            if (score > 0) open.push_back(v);
        if (open.size() == 1) {
            auto& top = *std::find_if(scored.begin(), scored.end(), [&](auto& p) { return p.second == open[0]; });
            double like = title_likeness(s.release.title, lib.videos[open[0]].release.title);
            if (top.first < 0.9 && (s.release.title.empty() || like >= 0.3)) {
                top.first = 0.9;
                whys[open[0]] = "the only video in the folder";
            }
        }

        bool settled = std::any_of(scored.begin(), scored.end(), [](auto& p) { return p.first >= SURE; });
        if (!settled) {
            std::set<int> far;
            for (auto& w : s.release.title) {
                auto it = with_word.find(w);
                if (it == with_word.end() || it->second.size() > COMMON_WORD) continue;
                far.insert(it->second.begin(), it->second.end());
            }
            for (int v : local) far.erase(v);
            for (int v : far)
                if (!refused(v)) scored.push_back({pair_score(s, lib.videos[v], whys[v]), v});
        }

        std::sort(scored.begin(), scored.end(), [](auto& a, auto& b) { return a.first > b.first; });
        if (scored.empty() || scored[0].first < MAYBE) continue;
        s.video = scored[0].second;
        s.score = scored[0].first;
        s.why = whys[s.video];
        // two versions of the film, and nothing in the name to tell which
        if (s.score < 1 && scored.size() > 1 && scored[1].first > scored[0].first - 0.05) {
            s.score = std::min(s.score, 0.7);
            s.why = "fits more than one video as well";
        }
    }
    return lib;
}
