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

#include <filesystem>

#include <unistd.h>

#include "check.h"
#include "state.h"
#include "text.h"

namespace fs = std::filesystem;

namespace {

using Args = std::vector<std::string>;

void args() {
    Job job;
    job.video = "/m/a.mkv";
    job.subtitle = "/m/a.en.srt";
    CHECK(engine_args(job) == (Args{"/m/a.mkv", "/m/a.en.srt", "auto", "--json"}));

    job.options.mode = "split";
    job.options.audio_track = 2;
    job.options.sub_track = -1;
    job.options.confidence = 12;
    job.options.snap = 120;
    job.options.dry = true;
    job.options.backup = false;
    job.options.force = true;
    CHECK(engine_args(job) == (Args{"/m/a.mkv", "/m/a.en.srt", "split", "--audio-track", "2", "--no-embedded",
                                    "--confidence", "12", "--snap", "120", "--force", "--json", "--dry-run",
                                    "--no-backup"}));

    job.options = Options();
    job.options.sub_track = 3;
    CHECK(engine_args(job) == (Args{"/m/a.mkv", "/m/a.en.srt", "auto", "--sub-track", "3", "--json"}));

    job.action = Action::Shift;
    job.shift = -500;
    CHECK(engine_args(job) == (Args{"--shift", "-500", "/m/a.en.srt", "--json"}));

    job.action = Action::Undo;
    CHECK(engine_args(job) == (Args{"--undo", "/m/a.en.srt"}));
}

Job settled(Action action, int code, const std::string& out, const std::string& err = "") {
    Job job;
    job.action = action;
    Ran ran;
    ran.code = code;
    ran.out = out;
    ran.err = err;
    settle(job, ran);
    return job;
}

void answers() {
    Job j = settled(Action::Sync, 0,
                    "{\"mode\":\"auto/shifted\",\"reference\":\"vad\",\"offset_ms\":-4200,\"sigma\":12.5,\"verdict\":"
                    "\"solid\",\"parts\":1,\"written\":true,\"unchanged\":false,\"output\":\"a.srt\",\"splits\":[],"
                    "\"offsets\":[-4200]}\n");
    CHECK_EQ(j.result.verdict, std::string("solid"));
    CHECK_EQ(j.result.offset, -4200);
    CHECK_EQ(j.mode, std::string("auto/shifted"));
    CHECK(j.written);

    j = settled(Action::Sync, 3,
                "{\"offset_ms\":-115.4,\"verdict\":\"unsure\",\"written\":true,\"why\":\"not sure enough to touch the "
                "original\",\"output\":\"a.lapse-unsure.srt\"}\n");
    CHECK_EQ(j.result.verdict, std::string("unsure"));
    CHECK_EQ(j.result.offset, -115);
    CHECK_EQ(j.why, std::string("not sure enough to touch the original"));

    j = settled(Action::Sync, 2, "{\"reference\":\"none\",\"written\":false,\"verdict\":\"nothing\"}\n");
    CHECK_EQ(j.result.verdict, std::string("few"));

    j = settled(Action::Sync, 0, "{\"verdict\":\"solid\",\"unchanged\":true,\"offset_ms\":0}\n");
    CHECK_EQ(j.result.verdict, std::string("same"));

    j = settled(Action::Sync, 1, "", "Reading the video\nERROR could not open the file\n");
    CHECK_EQ(j.result.verdict, std::string("failed"));
    CHECK_EQ(j.error, std::string("could not open the file"));

    j = settled(Action::Sync, -1, "", "");
    CHECK_EQ(j.result.verdict, std::string("failed"));
    CHECK(!j.error.empty());

    j = settled(Action::Sync, 0, "{\"error\":true}\n");
    CHECK_EQ(j.result.verdict, std::string("failed"));

    j = settled(Action::Shift, 0, "{\"mode\":\"shift\",\"offset_ms\":500,\"verdict\":\"solid\",\"written\":true}\n");
    CHECK_EQ(j.result.verdict, std::string("shifted"));
    CHECK_EQ(j.result.offset, 500);

    CHECK_EQ(settled(Action::Undo, 0, "Restored a.srt from the backup\n").result.verdict, std::string("undone"));
    CHECK_EQ(settled(Action::Undo, 1, "", "ERROR no backup\n").error, std::string("no backup"));

    Json json;
    CHECK(read_json("{\"a\":\"x\\\"y\\u00e6\",\"b\":-1.5e3,\"c\":[1,2],\"d\":null}", json));
    CHECK_EQ(json.text["a"], std::string("x\"y\xC3\xA6"));
    CHECK_EQ(json.number["b"], -1500.0);
    CHECK_EQ(json.list["c"].size(), 2u);
    CHECK(!read_json("{\"a\":", json));
    CHECK(!read_json("not json", json));
}

void state(const fs::path& tmp) {
    State s;
    s.choices.pinned["/m/sub.srt"] = "/m/film.mkv";
    s.choices.refused.insert("/m/other.srt\t/m/film.mkv");
    s.results["/m/sub.srt"] = {"solid", -4200, false, 1794496436};
    s.results["/m/dry.srt"] = {"unsure", 300, true, 1794496437};
    std::string file = (tmp / "state").string();
    CHECK(save_state(file, s));
    State back = load_state(file);
    CHECK(back.choices.pinned == s.choices.pinned);
    CHECK(back.choices.refused == s.choices.refused);
    CHECK_EQ(back.results["/m/sub.srt"].offset, -4200);
    CHECK(back.results["/m/dry.srt"].dry);
    CHECK_EQ(back.results["/m/dry.srt"].when, 1794496437LL);
}

void fuzzy() {
    CHECK(fuzzy_score("mtrx", "The Matrix (1999)/The.Matrix.1999.mkv") > 0);
    CHECK(fuzzy_score("xm", "matrix") < 0);
    CHECK(fuzzy_score("", "anything") == 0);
    CHECK(fuzzy_score("heat", "Heat (1995)/Heat.mkv") > fuzzy_score("heat", "The.Wheat.Field.mkv"));
    CHECK(fuzzy_score("blade runner", "Blade.Runner.1982.mkv") > 0);
    CHECK_EQ(squeeze("Heat.1995.1080p.BluRay.x264-GRP.en.srt", 20), std::string("Heat.1995.…RP.en.srt"));
    CHECK_EQ(shown("bad\x1b[2Jname"), std::string("bad?[2Jname"));
}

}

int main() {
    fs::path tmp = fs::temp_directory_path() / ("lapse-tui-queue-" + std::to_string(getpid()));
    fs::create_directories(tmp);
    args();
    answers();
    state(tmp);
    fuzzy();
    fs::remove_all(tmp);
    if (failures) std::cerr << failures << " checks failed\n";
    return failures ? 1 : 0;
}
