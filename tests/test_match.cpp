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

#include <chrono>

#include "check.h"
#include "match.h"

namespace {

using Words = std::vector<std::string>;

void names() {
    Release r = parse_name("The.Thing.1982.1080p.BluRay.x264-SPARKS.en.forced");
    CHECK(r.title == (Words{"the", "thing"}));
    CHECK_EQ(r.year, 1982);
    CHECK_EQ(r.resolution, std::string("1080p"));
    CHECK_EQ(r.source, std::string("BluRay"));
    CHECK_EQ(r.group, std::string("SPARKS"));
    CHECK_EQ(r.language, std::string("en"));
    CHECK(r.forced);

    r = parse_name("Heat (1995)");
    CHECK(r.title == (Words{"heat"}));
    CHECK_EQ(r.year, 1995);
    CHECK(r.group.empty());

    r = parse_name("1917.2019.2160p.WEB-DL");
    CHECK(r.title == (Words{"1917"}));
    CHECK_EQ(r.year, 2019);
    CHECK_EQ(r.source, std::string("WEB"));

    r = parse_name("1917");
    CHECK(r.title == (Words{"1917"}));
    CHECK_EQ(r.year, 0);

    r = parse_name("Show.Name.S02E05.720p.HDTV.x264-GRP");
    CHECK(r.title == (Words{"show", "name"}));
    CHECK_EQ(r.season, 2);
    CHECK_EQ(r.episode, 5);
    CHECK_EQ(r.group, std::string("GRP"));

    r = parse_name("show name 3x07 Danish SDH");
    CHECK_EQ(r.season, 3);
    CHECK_EQ(r.episode, 7);
    CHECK_EQ(r.language, std::string("da"));
    CHECK(r.sdh);

    r = parse_name("Show - s01.e10 - Pilot");
    CHECK_EQ(r.season, 1);
    CHECK_EQ(r.episode, 10);
    CHECK(r.group.empty());

    r = parse_name("Show.S01E01E02");
    CHECK_EQ(r.episode, 1);

    r = parse_name("Spider-Man.Homecoming");
    CHECK(r.title == (Words{"spider", "man", "homecoming"}));
    CHECK(r.group.empty());

    r = parse_name("Ocean's.Eleven.2001.Extended.eng");
    CHECK(r.title == (Words{"oceans", "eleven"}));
    CHECK_EQ(r.edition, std::string("Extended"));
    CHECK_EQ(r.language, std::string("en"));

    r = parse_name("2_English");
    CHECK(r.title.empty());
    CHECK_EQ(r.language, std::string("en"));

    r = parse_name("It.2017");
    CHECK(r.title == (Words{"it"}));
    CHECK(r.language.empty());

    // a name with an S and digits in it is not an episode
    r = parse_name("S1m0ne.2002");
    CHECK(r.title == (Words{"s1m0ne"}));
    CHECK_EQ(r.season, -1);

    CHECK(title_likeness({"the", "matrix"}, {"the", "matrix"}) == 1);
    CHECK(title_likeness({"the", "matirx"}, {"the", "matrix"}) > 0.7);
    CHECK(title_likeness({"heat"}, {"heap"}) < 0.8);
    CHECK(title_likeness({"alien"}, {"aliens"}) > 0.75);
    CHECK(title_likeness({"up"}, {"cars"}) == 0);
    CHECK(title_likeness({}, {"cars"}) == 0);
}

Dir dir(const std::string& path, std::vector<std::string> names) {
    Dir d;
    d.path = path;
    for (auto& n : names) d.files.push_back({n, kind_of(n), 1});
    return d;
}

const Subtitle* sub(const Library& lib, const std::string& path) {
    for (auto& s : lib.subs)
        if (s.path == path) return &s;
    return nullptr;
}

std::string video_of(const Library& lib, const std::string& path) {
    const Subtitle* s = sub(lib, path);
    return s && s->video >= 0 ? lib.videos[s->video].path : "";
}

double score_of(const Library& lib, const std::string& path) {
    const Subtitle* s = sub(lib, path);
    return s ? s->score : -1;
}

void pairing() {
    Index index = {
        dir("/m/Heat (1995)", {"Heat.1995.1080p.BluRay.x264-GRP.mkv", "Heat.1995.1080p.BluRay.x264-GRP.en.srt",
                               "Heat.1995.1080p.BluRay.x264-GRP.da.srt", "Heat.1995.1080p.BluRay.x264-GRP.da.srt.bak"}),
        dir("/m/Alien (1979)", {"movie.mkv", "English.srt"}),
        dir("/m/Scene.Film.2010.720p.WEB-DL-GRP", {"Scene.Film.2010.720p.WEB-DL-GRP.mkv"}),
        dir("/m/Scene.Film.2010.720p.WEB-DL-GRP/Subs", {"2_English.srt", "3_Danish.srt"}),
        dir("/m/Other.Film.2012", {"Other.Film.2012.mkv"}),
        dir("/m/Other.Film.2012/Subs/Other.Film.2012", {"eng.srt"}),
        dir("/m/Two", {"Blade.Runner.1982.1080p.mkv", "Blade.Runner.1982.2160p.mkv", "Blade.Runner.1982.en.srt",
                       "Blade.Runner.1982.2160p.da.srt"}),
        dir("/tv/Show/Season 1", {"Show.S01E01.mkv", "Show.S01E02.mkv", "Show.S01E01.en.srt", "show.1x02.en.srt",
                                  "Show.S01E03.en.srt"}),
        dir("/home/me/Downloads", {"The.Matrix.1999.en.srt", "Teh.Matrix.da.srt", "Unknown.Thing.srt",
                                   "Show.S01E02.da.srt", "Heat.2013.en.srt"}),
        dir("/m/The Matrix (1999)", {"The.Matrix.1999.mkv"}),
        dir("/m/Mixed", {"Up.2009.mkv", "Cars.2006.en.srt"}),
    };
    Library lib = match(index, {});

    CHECK_EQ(video_of(lib, "/m/Heat (1995)/Heat.1995.1080p.BluRay.x264-GRP.en.srt"),
             std::string("/m/Heat (1995)/Heat.1995.1080p.BluRay.x264-GRP.mkv"));
    CHECK_EQ(score_of(lib, "/m/Heat (1995)/Heat.1995.1080p.BluRay.x264-GRP.da.srt"), 1.0);
    CHECK(sub(lib, "/m/Heat (1995)/Heat.1995.1080p.BluRay.x264-GRP.da.srt")->backup);
    CHECK(!sub(lib, "/m/Heat (1995)/Heat.1995.1080p.BluRay.x264-GRP.en.srt")->backup);

    // the folder names the film when the file does not
    CHECK_EQ(video_of(lib, "/m/Alien (1979)/English.srt"), std::string("/m/Alien (1979)/movie.mkv"));
    CHECK(score_of(lib, "/m/Alien (1979)/English.srt") >= SURE);

    CHECK_EQ(video_of(lib, "/m/Scene.Film.2010.720p.WEB-DL-GRP/Subs/3_Danish.srt"),
             std::string("/m/Scene.Film.2010.720p.WEB-DL-GRP/Scene.Film.2010.720p.WEB-DL-GRP.mkv"));
    CHECK(score_of(lib, "/m/Scene.Film.2010.720p.WEB-DL-GRP/Subs/2_English.srt") >= SURE);
    CHECK_EQ(score_of(lib, "/m/Other.Film.2012/Subs/Other.Film.2012/eng.srt"), 1.0);

    // two versions: the one it is named after wins, the other is a question
    CHECK_EQ(video_of(lib, "/m/Two/Blade.Runner.1982.2160p.da.srt"), std::string("/m/Two/Blade.Runner.1982.2160p.mkv"));
    CHECK_EQ(score_of(lib, "/m/Two/Blade.Runner.1982.2160p.da.srt"), 1.0);
    double either = score_of(lib, "/m/Two/Blade.Runner.1982.en.srt");
    CHECK(either >= MAYBE && either < SURE);

    CHECK_EQ(video_of(lib, "/tv/Show/Season 1/Show.S01E01.en.srt"), std::string("/tv/Show/Season 1/Show.S01E01.mkv"));
    CHECK_EQ(video_of(lib, "/tv/Show/Season 1/show.1x02.en.srt"), std::string("/tv/Show/Season 1/Show.S01E02.mkv"));
    CHECK_EQ(video_of(lib, "/tv/Show/Season 1/Show.S01E03.en.srt"), std::string(""));

    // somewhere else entirely: paired, but as a question
    CHECK_EQ(video_of(lib, "/home/me/Downloads/The.Matrix.1999.en.srt"), std::string("/m/The Matrix (1999)/The.Matrix.1999.mkv"));
    double far = score_of(lib, "/home/me/Downloads/The.Matrix.1999.en.srt");
    CHECK(far >= MAYBE && far < SURE);
    CHECK_EQ(video_of(lib, "/home/me/Downloads/Teh.Matrix.da.srt"), std::string("/m/The Matrix (1999)/The.Matrix.1999.mkv"));
    CHECK(score_of(lib, "/home/me/Downloads/Teh.Matrix.da.srt") < score_of(lib, "/home/me/Downloads/The.Matrix.1999.en.srt"));
    CHECK_EQ(video_of(lib, "/home/me/Downloads/Show.S01E02.da.srt"), std::string("/tv/Show/Season 1/Show.S01E02.mkv"));
    CHECK_EQ(video_of(lib, "/home/me/Downloads/Unknown.Thing.srt"), std::string(""));
    CHECK_EQ(video_of(lib, "/home/me/Downloads/Heat.2013.en.srt"), std::string(""));

    // the only video in the folder, but a title nothing like it
    double mixed = score_of(lib, "/m/Mixed/Cars.2006.en.srt");
    CHECK(mixed < SURE);

    // by hand
    Choices choices;
    choices.pinned["/home/me/Downloads/Unknown.Thing.srt"] = "/m/Other.Film.2012/Other.Film.2012.mkv";
    choices.refused.insert("/home/me/Downloads/The.Matrix.1999.en.srt\t/m/The Matrix (1999)/The.Matrix.1999.mkv");
    lib = match(index, choices);
    CHECK_EQ(video_of(lib, "/home/me/Downloads/Unknown.Thing.srt"), std::string("/m/Other.Film.2012/Other.Film.2012.mkv"));
    CHECK_EQ(score_of(lib, "/home/me/Downloads/Unknown.Thing.srt"), 1.0);
    CHECK_EQ(video_of(lib, "/home/me/Downloads/The.Matrix.1999.en.srt"), std::string(""));

    // sorted by title, then year
    CHECK_EQ(lib.videos.front().path, std::string("/m/Alien (1979)/movie.mkv"));
}

// tens of thousands of files have to match in well under a second
void many() {
    Index index;
    for (int i = 0; i < 20000; i++) {
        std::string name = "Film.Number." + std::to_string(i) + "." + std::to_string(1950 + i % 70);
        index.push_back(dir("/lib/" + name, {name + ".1080p.mkv", name + ".en.srt", name + ".da.srt"}));
    }
    index.push_back(dir("/loose", {"Film.Number.123.2003.en.srt"}));
    auto start = std::chrono::steady_clock::now();
    Library lib = match(index, {});
    double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    std::cerr << "matched 60001 files in " << (int)ms << " ms\n";
    CHECK(ms < 3000);
    CHECK_EQ(video_of(lib, "/loose/Film.Number.123.2003.en.srt"), std::string("/lib/Film.Number.123.2003/Film.Number.123.2003.1080p.mkv"));
}

}

int main() {
    names();
    pairing();
    many();
    if (failures) std::cerr << failures << " checks failed\n";
    return failures ? 1 : 0;
}
