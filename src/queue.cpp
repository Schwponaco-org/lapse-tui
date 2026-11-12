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

#include "queue.h"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <sstream>

#include <sys/stat.h>

namespace {

bool exists(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0;
}

std::string last_line(const std::string& s) {
    std::istringstream in(s);
    std::string line, last;
    while (std::getline(in, line))
        if (!line.empty()) last = line;
    if (last.rfind("ERROR ", 0) == 0) last = last.substr(6);
    return last;
}

}

std::vector<std::string> engine_args(const Job& job) {
    const Options& o = job.options;
    if (job.action == Action::Undo) return {"--undo", job.subtitle};

    std::vector<std::string> args;
    if (job.action == Action::Shift) {
        args = {"--shift", std::to_string(job.shift), job.subtitle};
    } else {
        args = {job.video, job.subtitle, o.mode};
        if (o.audio_track > 0) args.insert(args.end(), {"--audio-track", std::to_string(o.audio_track)});
        if (o.sub_track > 0) args.insert(args.end(), {"--sub-track", std::to_string(o.sub_track)});
        if (o.sub_track < 0) args.push_back("--no-embedded");
        if (o.confidence > 0) args.insert(args.end(), {"--confidence", std::to_string(o.confidence)});
        if (o.snap > 0) args.insert(args.end(), {"--snap", std::to_string(o.snap)});
        if (o.force) args.push_back("--force");
    }
    args.push_back("--json");
    if (o.dry) args.push_back("--dry-run");
    if (!o.backup) args.push_back("--no-backup");
    return args;
}

void settle(Job& job, const Ran& ran) {
    Result& r = job.result;
    r.dry = job.options.dry;
    r.when = (long long)std::time(nullptr);

    if (job.action == Action::Undo) {
        r.verdict = ran.code == 0 ? "undone" : "failed";
        if (ran.code != 0) job.error = last_line(ran.err + ran.out);
        return;
    }

    Json j;
    std::string line = ran.out.substr(0, ran.out.find('\n'));
    if (ran.code < 0 || ran.code == 1 || !read_json(line, j) || j.flag["error"]) {
        r.verdict = "failed";
        job.error = last_line(ran.err);
        if (job.error.empty()) job.error = ran.code < 0 ? "lapse stopped before it was done" : "lapse gave no answer";
        return;
    }
    r.offset = (int)std::lround(j.number["offset_ms"]);
    job.mode = j.text["mode"];
    job.output = j.text["output"];
    job.why = j.text["why"];
    job.sigma = j.number["sigma"];
    job.parts = j.number.count("parts") ? (int)j.number["parts"] : 1;
    job.written = j.flag["written"];

    if (j.flag["unchanged"]) r.verdict = "same";
    else if (job.action == Action::Shift) r.verdict = "shifted";
    else if (ran.code == 2) r.verdict = "few";
    else r.verdict = j.text["verdict"].empty() ? "failed" : j.text["verdict"];
}

Queue::Queue(std::string engine, std::function<void(const Job&)> changed)
    : engine_(std::move(engine)), changed_(std::move(changed)), worker_([this] { work(); }) {}

Queue::~Queue() {
    {
        std::lock_guard<std::mutex> hold(lock_);
        quit_ = true;
        stop_running_ = true;
    }
    wake_.notify_all();
    worker_.join();
}

void Queue::add(Job job) {
    {
        std::lock_guard<std::mutex> hold(lock_);
        job.id = next_id_++;
        job.stage = Stage::Waiting;
        jobs_.push_back(std::move(job));
    }
    wake_.notify_all();
}

void Queue::cancel(int id) {
    std::lock_guard<std::mutex> hold(lock_);
    for (auto& j : jobs_) {
        if (j.id != id) continue;
        if (j.stage == Stage::Waiting) j.stage = Stage::Cancelled;
        if (j.stage == Stage::Running) stop_running_ = true;
    }
}

void Queue::cancel_all() {
    std::lock_guard<std::mutex> hold(lock_);
    for (auto& j : jobs_) {
        if (j.stage == Stage::Waiting) j.stage = Stage::Cancelled;
        if (j.stage == Stage::Running) stop_running_ = true;
    }
}

void Queue::clear_finished() {
    std::lock_guard<std::mutex> hold(lock_);
    jobs_.erase(std::remove_if(jobs_.begin(), jobs_.end(), [](const Job& j) {
        return j.stage == Stage::Done || j.stage == Stage::Cancelled;
    }), jobs_.end());
}

std::vector<Job> Queue::jobs() {
    std::lock_guard<std::mutex> hold(lock_);
    return {jobs_.begin(), jobs_.end()};
}

int Queue::left() {
    std::lock_guard<std::mutex> hold(lock_);
    return (int)std::count_if(jobs_.begin(), jobs_.end(), [](const Job& j) {
        return j.stage == Stage::Waiting || j.stage == Stage::Running;
    });
}

void Queue::drain() {
    std::unique_lock<std::mutex> hold(lock_);
    idle_.wait(hold, [this] {
        return std::none_of(jobs_.begin(), jobs_.end(), [](const Job& j) {
            return j.stage == Stage::Waiting || j.stage == Stage::Running;
        });
    });
}

void Queue::work() {
    std::unique_lock<std::mutex> hold(lock_);
    while (true) {
        auto next = jobs_.end();
        wake_.wait(hold, [&] {
            next = std::find_if(jobs_.begin(), jobs_.end(), [](const Job& j) { return j.stage == Stage::Waiting; });
            return quit_ || next != jobs_.end();
        });
        if (quit_) return;

        next->stage = Stage::Running;
        next->started = Clock::now();
        stop_running_ = false;
        Job job = *next;
        hold.unlock();
        changed_(job);

        Ran ran = run_engine(engine_, engine_args(job), &stop_running_);
        bool stopped = stop_running_;
        // lapse writes beside the file and renames, so a stopped run can
        // leave the half written copy behind and nothing else
        if (stopped && exists(job.subtitle + ".tmp")) std::remove((job.subtitle + ".tmp").c_str());
        settle(job, ran);
        if (stopped) {
            job.result.verdict = "stopped";
            job.error.clear();
        }
        job.stage = Stage::Done;
        job.ended = Clock::now();

        hold.lock();
        for (auto& j : jobs_)
            if (j.id == job.id) j = job;
        hold.unlock();
        changed_(job);
        hold.lock();
        idle_.notify_all();
    }
}
