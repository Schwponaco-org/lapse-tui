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

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <thread>

#include <unistd.h>

#include "check.h"
#include "config.h"
#include "library.h"

namespace fs = std::filesystem;

namespace {

void touch(const fs::path& p, const std::string& text = "x") {
    fs::create_directories(p.parent_path());
    std::ofstream(p) << text;
}

Index walk(const std::string& root, const Index& cached, int& read, int& reused,
           std::set<std::string> fenced = {}) {
    Index out;
    Walk w;
    w.roots = {root};
    w.fences = std::move(fenced);
    w.found = [&](Dir&& d) { out.push_back(std::move(d)); };
    scan(w, cached);
    read = w.read;
    reused = w.reused;
    return out;
}

const Dir* find(const Index& index, const std::string& path) {
    for (auto& d : index)
        if (d.path == path) return &d;
    return nullptr;
}

void kinds() {
    CHECK(kind_of("Film.2010.mkv") == Kind::Video);
    CHECK(kind_of("Film.2010.EN.SRT") == Kind::Subtitle);
    CHECK(kind_of("Film.2010.en.srt.bak") == Kind::Backup);
    CHECK(kind_of("Film.2010.lapse-unsure.srt") == Kind::Other);
    CHECK(kind_of("notes.txt.bak") == Kind::Other);
    CHECK(kind_of("cover.jpg") == Kind::Other);
    CHECK(kind_of(".srt") == Kind::Other);
}

void scanning(const fs::path& tmp) {
    std::string root = (tmp / "lib").string();
    touch(tmp / "lib/Films/Heat (1995)/Heat.1995.1080p.mkv", "video");
    touch(tmp / "lib/Films/Heat (1995)/Heat.1995.1080p.en.srt");
    touch(tmp / "lib/Films/Heat (1995)/Heat.1995.1080p.en.srt.bak");
    touch(tmp / "lib/Films/Heat (1995)/cover.jpg");
    touch(tmp / "lib/Films/.hidden/Secret.mkv");
    touch(tmp / "lib/Films/@eaDir/Thumb.mkv");
    touch(tmp / "lib/Shows/Show/Season 1/Show.S01E01.mkv");
    touch(tmp / "lib/Fenced/Far.mkv");
    fs::create_directory_symlink(tmp / "lib", tmp / "lib/Films/loop");

    int read, reused;
    Index first = walk(root, {}, read, reused, {root + "/Fenced"});
    CHECK_EQ(reused, 0);
    CHECK(find(first, root + "/Films/.hidden") == nullptr);
    CHECK(find(first, root + "/Films/@eaDir") == nullptr);
    CHECK(find(first, root + "/Films/loop") == nullptr);
    CHECK(find(first, root + "/Fenced") == nullptr);
    const Dir* heat = find(first, root + "/Films/Heat (1995)");
    CHECK(heat != nullptr);
    if (heat) {
        CHECK_EQ(heat->files.size(), 3u);
        CHECK_EQ(heat->files[0].name, std::string("Heat.1995.1080p.en.srt"));
        CHECK(heat->files[1].kind == Kind::Backup);
        CHECK_EQ(heat->files[2].size, 5);
    }
    CHECK(find(first, root + "/Shows/Show/Season 1") != nullptr);

    std::string file = (tmp / "index").string();
    CHECK(save_index(file, first));
    Index loaded;
    CHECK(load_index(file, loaded));
    CHECK_EQ(loaded.size(), first.size());

    // the second walk reads only the folder that changed
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    touch(tmp / "lib/Shows/Show/Season 1/Show.S01E02.mkv");
    Index second = walk(root, loaded, read, reused, {root + "/Fenced"});
    CHECK_EQ(read, 1);
    CHECK_EQ(reused, (int)first.size() - 1);
    const Dir* season = find(second, root + "/Shows/Show/Season 1");
    CHECK(season && season->files.size() == 2);

    fs::remove_all(tmp / "lib/Shows");
    Index third = walk(root, second, read, reused, {root + "/Fenced"});
    CHECK(find(third, root + "/Shows/Show") == nullptr);
    CHECK_EQ(read, 1);

    std::ofstream(file) << "lapse-tui index 1\nv 3 orphan.mkv\n";
    CHECK(!load_index(file, loaded));
    std::ofstream(file) << "something else\n";
    CHECK(!load_index(file, loaded));
}

void mounts() {
    auto m = parse_mounts(
        "/dev/nvme0n1p2 / ext4 rw 0 0\n"
        "/dev/nvme0n1p3 /home btrfs rw 0 0\n"
        "proc /proc proc rw 0 0\n"
        "/dev/sda1 /boot vfat rw 0 0\n"
        "/dev/sdb1 /run/media/me/Big\\040Disk ntfs3 rw 0 0\n"
        "/dev/sdc1 /data xfs rw 0 0\n"
        "/dev/sdc2 /data/more xfs rw 0 0\n"
        "//nas/films /data/nas cifs rw 0 0\n"
        "nas:/video /video nfs4 rw 0 0\n"
        "tmpfs /tmp tmpfs rw 0 0\n");
    CHECK_EQ(m.size(), 10u);
    CHECK_EQ(m[4].point, std::string("/run/media/me/Big Disk"));

    auto roots = default_roots("/home/me", m, false);
    auto has = [&](const std::string& r) { return std::find(roots.begin(), roots.end(), r) != roots.end(); };
    CHECK(has("/home/me"));
    CHECK(has("/data"));
    CHECK(!has("/data/more"));
    CHECK(!has("/"));
    CHECK(!has("/home"));
    CHECK(!has("/boot"));
    CHECK(!has("/tmp"));
    CHECK(!has("/video"));
    auto wide = default_roots("/home/me", m, true);
    CHECK(std::find(wide.begin(), wide.end(), "/video") != wide.end());

    auto fenced = fences(m, false);
    CHECK(fenced.count("/data/nas") == 1);
    CHECK(fenced.count("/video") == 1);
    CHECK(fences(m, true).empty());
}

void config_file(const fs::path& tmp) {
    std::string file = (tmp / "config").string();
    Config c;
    c.roots = {"/srv/films", "/home/me/Films and shows"};
    c.network = true;
    c.engine = "/opt/lapse/lapse";
    CHECK(save_config(file, c));
    Config back = load_config(file);
    CHECK(back.roots == c.roots);
    CHECK(back.network);
    CHECK_EQ(back.engine, c.engine);
    CHECK(load_config((tmp / "missing").string()).roots.empty());
}

}

int main() {
    fs::path tmp = fs::temp_directory_path() / ("lapse-tui-test-" + std::to_string(getpid()));
    fs::remove_all(tmp);
    fs::create_directories(tmp);
    kinds();
    scanning(tmp);
    mounts();
    config_file(tmp);
    fs::remove_all(tmp);
    if (failures) std::cerr << failures << " checks failed\n";
    return failures ? 1 : 0;
}
