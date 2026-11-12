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

#include "library.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <unordered_map>

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {

// The same lists the container works from
const std::set<std::string> VIDEO_EXTS = {
    ".mp4", ".mkv", ".avi", ".mov", ".m4v", ".ts", ".m2ts", ".mts", ".webm",
    ".wmv", ".mpg", ".mpeg", ".flv", ".ogv", ".ogm", ".divx", ".vob", ".rmvb", ".3gp",
};

const std::set<std::string> SUBTITLE_EXTS = {
    ".srt", ".ass", ".ssa", ".vtt", ".sub", ".idx", ".sup", ".smi", ".sami", ".ttml",
    ".dfxp", ".itt", ".sbv", ".mpl2", ".lrc", ".stl", ".scc", ".mcc", ".cap",
};

// NAS housekeeping and folders that are never a film library
const std::set<std::string> SKIP_DIRS = {
    "@eaDir", "#recycle", "#snapshot", "lost+found", "@Recycle", "$RECYCLE.BIN",
    "System Volume Information", "node_modules", "__pycache__",
};

std::string lower(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    return s;
}

std::string extension(const std::string& name) {
    size_t dot = name.rfind('.');
    return dot == std::string::npos || dot == 0 ? "" : lower(name.substr(dot));
}

int64_t mtime_of(const struct stat& st) {
#ifdef __APPLE__
    return (int64_t)st.st_mtimespec.tv_sec * 1000000000 + st.st_mtimespec.tv_nsec;
#else
    return (int64_t)st.st_mtim.tv_sec * 1000000000 + st.st_mtim.tv_nsec;
#endif
}

std::string join(const std::string& dir, const std::string& name) {
    return dir == "/" ? "/" + name : dir + "/" + name;
}

bool read_dir(const std::string& path, Dir& dir) {
    DIR* d = opendir(path.c_str());
    if (!d) return false;
    int fd = dirfd(d);
    while (dirent* e = readdir(d)) {
        std::string name = e->d_name;
        if (name.empty() || name[0] == '.' || name.find('\n') != std::string::npos) continue;
        unsigned char type = e->d_type;
        struct stat st;
        bool have_stat = false;
        if (type == DT_UNKNOWN) {
            if (fstatat(fd, e->d_name, &st, AT_SYMLINK_NOFOLLOW) != 0) continue;
            have_stat = true;
            type = S_ISDIR(st.st_mode) ? DT_DIR : S_ISREG(st.st_mode) ? DT_REG : DT_LNK;
        }
        // folders behind a symlink are left alone, they can loop back
        if (type == DT_DIR) {
            if (!SKIP_DIRS.count(name)) dir.subdirs.push_back(name);
            continue;
        }
        Kind kind = kind_of(name);
        if (kind == Kind::Other) continue;
        if (!have_stat && fstatat(fd, e->d_name, &st, 0) != 0) continue;
        if (!S_ISREG(st.st_mode)) continue;
        dir.files.push_back({name, kind, (int64_t)st.st_size});
    }
    closedir(d);
    std::sort(dir.subdirs.begin(), dir.subdirs.end());
    std::sort(dir.files.begin(), dir.files.end(), [](const File& a, const File& b) { return a.name < b.name; });
    return true;
}

}

Kind kind_of(const std::string& name) {
    std::string ext = extension(name);
    if (VIDEO_EXTS.count(ext)) return Kind::Video;
    if (SUBTITLE_EXTS.count(ext)) {
        // the guesses lapse leaves beside an unsure file are its own, not ours to pair
        if (name.find(".lapse-unsure.") != std::string::npos) return Kind::Other;
        return Kind::Subtitle;
    }
    if (ext == ".bak" && SUBTITLE_EXTS.count(extension(name.substr(0, name.size() - 4)))) return Kind::Backup;
    return Kind::Other;
}

bool read_folder(const std::string& path, Dir& dir) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) return false;
    dir = Dir{path, mtime_of(st), {}, {}};
    return read_dir(path, dir);
}

void scan(Walk& walk, const Index& cached) {
    std::unordered_map<std::string, const Dir*> before;
    for (auto& d : cached) before[d.path] = &d;

    std::set<std::string> seen;
    std::vector<std::string> todo(walk.roots.rbegin(), walk.roots.rend());
    while (!todo.empty()) {
        if (walk.stop && *walk.stop) return;
        std::string path = todo.back();
        todo.pop_back();
        if (!seen.insert(path).second) continue;

        struct stat st;
        if (stat(path.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) continue;
        Dir dir;
        dir.path = path;
        dir.mtime = mtime_of(st);
        auto old = before.find(path);
        if (old != before.end() && old->second->mtime == dir.mtime) {
            dir.subdirs = old->second->subdirs;
            dir.files = old->second->files;
            walk.reused++;
        } else {
            if (!read_dir(path, dir)) continue;
            walk.read++;
        }
        for (auto it = dir.subdirs.rbegin(); it != dir.subdirs.rend(); ++it) {
            std::string sub = join(path, *it);
            if (!walk.fences.count(sub)) todo.push_back(sub);
        }
        walk.found(std::move(dir));
    }
}


// One line a record, the kind first: D for a folder with its time and path,
// then s for a folder in it, v, t and b for a video, a subtitle and a backup
// with their size. Names with a newline in them are never indexed
bool load_index(const std::string& file, Index& out) {
    std::ifstream in(file);
    std::string line;
    if (!std::getline(in, line) || line != "lapse-tui index 1") return false;
    Index index;
    while (std::getline(in, line)) {
        if (line.size() < 3 || line[1] != ' ') return false;
        char tag = line[0];
        std::string rest = line.substr(2);
        size_t space = rest.find(' ');
        if (tag == 'D') {
            if (space == std::string::npos) return false;
            index.push_back({rest.substr(space + 1), std::atoll(rest.c_str()), {}, {}});
            continue;
        }
        if (index.empty()) return false;
        Dir& dir = index.back();
        if (tag == 's') {
            dir.subdirs.push_back(rest);
        } else if (tag == 'v' || tag == 't' || tag == 'b') {
            if (space == std::string::npos) return false;
            Kind kind = tag == 'v' ? Kind::Video : tag == 't' ? Kind::Subtitle : Kind::Backup;
            dir.files.push_back({rest.substr(space + 1), kind, std::atoll(rest.c_str())});
        } else {
            return false;
        }
    }
    out = std::move(index);
    return true;
}

bool save_index(const std::string& file, const Index& index) {
    std::string tmp = file + ".tmp";
    {
        std::ofstream out(tmp, std::ios::trunc);
        if (!out) return false;
        out << "lapse-tui index 1\n";
        for (auto& d : index) {
            out << "D " << d.mtime << ' ' << d.path << '\n';
            for (auto& s : d.subdirs) out << "s " << s << '\n';
            for (auto& f : d.files) {
                char tag = f.kind == Kind::Video ? 'v' : f.kind == Kind::Subtitle ? 't' : 'b';
                out << tag << ' ' << f.size << ' ' << f.name << '\n';
            }
        }
        if (!out.flush()) return false;
    }
    return std::rename(tmp.c_str(), file.c_str()) == 0;
}
