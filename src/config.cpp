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

#include "config.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include <sys/stat.h>
#ifdef __APPLE__
    #include <sys/mount.h>
#endif

namespace {

const std::set<std::string> DISK_TYPES = {
    "ext2", "ext3", "ext4", "btrfs", "xfs", "zfs", "bcachefs", "f2fs", "jfs", "reiserfs",
    "vfat", "exfat", "ntfs", "ntfs3", "fuseblk", "hfs", "hfsplus", "apfs", "msdos", "udf", "iso9660",
};

const std::set<std::string> NETWORK_TYPES = {
    "nfs", "nfs4", "cifs", "smb3", "smbfs", "afpfs", "webdav", "9p", "afs", "ceph", "glusterfs",
    "lustre", "davfs", "fuse.sshfs", "sshfs", "fuse.rclone", "fuse.glusterfs", "fuse.davfs2",
};

const std::vector<std::string> SYSTEM_DIRS = {
    "/boot", "/efi", "/etc", "/usr", "/var", "/opt", "/nix", "/snap", "/proc", "/sys", "/dev",
    "/tmp", "/root", "/run", "/System", "/private", "/Library", "/Applications",
};

std::string env_or(const char* name, const std::string& fallback) {
    const char* v = std::getenv(name);
    return v && *v == '/' ? v : fallback;
}

std::string home() {
    return env_or("HOME", "/");
}

bool under(const std::string& path, const std::string& dir) {
    if (dir == "/") return true;
    return path == dir || (path.size() > dir.size() && path.compare(0, dir.size(), dir) == 0 && path[dir.size()] == '/');
}

bool is_dir(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r");
    size_t b = s.find_last_not_of(" \t\r");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

// /proc/mounts writes a space as \040 and so on
std::string unescape(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '\\' && i + 3 < s.size()) {
            out += (char)std::strtol(s.substr(i + 1, 3).c_str(), nullptr, 8);
            i += 3;
        } else {
            out += s[i];
        }
    }
    return out;
}

}

std::string config_dir() {
#ifdef __APPLE__
    return home() + "/Library/Application Support/lapse-tui";
#else
    return env_or("XDG_CONFIG_HOME", home() + "/.config") + "/lapse-tui";
#endif
}

std::string cache_dir() {
#ifdef __APPLE__
    return home() + "/Library/Caches/lapse-tui";
#else
    return env_or("XDG_CACHE_HOME", home() + "/.cache") + "/lapse-tui";
#endif
}

std::string state_dir() {
#ifdef __APPLE__
    return home() + "/Library/Application Support/lapse-tui";
#else
    return env_or("XDG_STATE_HOME", home() + "/.local/state") + "/lapse-tui";
#endif
}

bool make_dirs(const std::string& path) {
    for (size_t at = 1; at <= path.size(); at++) {
        if (at < path.size() && path[at] != '/') continue;
        if (mkdir(path.substr(0, at).c_str(), 0755) != 0 && errno != EEXIST) return false;
    }
    return true;
}

Config load_config(const std::string& file) {
    Config c;
    std::ifstream in(file);
    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        size_t eq = line.find('=');
        if (line.empty() || line[0] == '#' || eq == std::string::npos) continue;
        std::string key = trim(line.substr(0, eq)), value = trim(line.substr(eq + 1));
        if (key == "root" && !value.empty()) c.roots.push_back(value);
        else if (key == "network") c.network = value == "yes";
        else if (key == "engine") c.engine = value;
    }
    return c;
}

bool save_config(const std::string& file, const Config& c) {
    std::string tmp = file + ".tmp";
    {
        std::ofstream out(tmp, std::ios::trunc);
        if (!out) return false;
        out << "# lapse-tui settings, rewritten when they change inside it\n"
               "# Folders to look for films in. Without any, it looks in your home,\n"
               "# /mnt, /media, /srv and the disks mounted outside the system folders\n";
        for (auto& r : c.roots) out << "root = " << r << '\n';
        out << "# Go into network shares (nfs, smb, sshfs) as well: yes or no\n"
               "network = " << (c.network ? "yes" : "no") << "\n"
               "# The lapse binary, when it is not on PATH\n"
               "engine = " << c.engine << '\n';
        if (!out.flush()) return false;
    }
    return std::rename(tmp.c_str(), file.c_str()) == 0;
}

std::vector<Mount> parse_mounts(const std::string& text) {
    std::vector<Mount> out;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream fields(line);
        std::string dev, point, type;
        if (fields >> dev >> point >> type) out.push_back({unescape(point), type});
    }
    return out;
}

std::vector<Mount> read_mounts() {
#ifdef __APPLE__
    std::vector<Mount> out;
    struct statfs* all = nullptr;
    int n = getmntinfo(&all, MNT_NOWAIT);
    for (int i = 0; i < n; i++) out.push_back({all[i].f_mntonname, all[i].f_fstypename});
    return out;
#else
    std::ifstream in("/proc/self/mounts");
    std::stringstream text;
    text << in.rdbuf();
    return parse_mounts(text.str());
#endif
}

bool is_network(const std::string& type) {
    return NETWORK_TYPES.count(type) > 0;
}

std::vector<std::string> default_roots(const std::string& home, const std::vector<Mount>& mounts, bool network) {
    std::vector<std::string> roots = {home};
    for (const char* media : {"/mnt", "/media", "/srv", "/run/media", "/Volumes"})
        if (is_dir(media)) roots.push_back(media);
    for (auto& m : mounts) {
        if (!DISK_TYPES.count(m.type) && !(network && is_network(m.type))) continue;
        // / and /home hold the home already, and the rest of them is the system
        if (under(home, m.point)) continue;
        bool system = false;
        for (auto& dir : SYSTEM_DIRS) system = system || under(m.point, dir);
        if (system && !under(m.point, "/run/media")) continue;
        roots.push_back(m.point);
    }

    std::sort(roots.begin(), roots.end());
    roots.erase(std::unique(roots.begin(), roots.end()), roots.end());
    std::vector<std::string> outer;
    for (auto& r : roots) {
        bool inside = false;
        for (auto& other : roots) inside = inside || (other != r && under(r, other));
        if (!inside) outer.push_back(r);
    }
    return outer;
}

std::set<std::string> fences(const std::vector<Mount>& mounts, bool network) {
    std::set<std::string> out;
    if (network) return out;
    for (auto& m : mounts)
        if (is_network(m.type)) out.insert(m.point);
    return out;
}
