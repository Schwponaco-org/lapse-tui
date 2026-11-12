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

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "engine.h"

using Clock = std::chrono::steady_clock;

// the engine options a sync can take, each left to the engine when unset
struct Options {
    std::string mode = "auto";
    int audio_track = 0;     // 0 the engine's choice
    int sub_track = 0;       // 0 the engine's choice, -1 none, listen to the audio
    int confidence = 0;      // 0 the engine's default
    int snap = 0;            // 0 off, else the window in ms
    bool dry = false;
    bool backup = true;
    bool force = false;
};

enum class Action { Sync, Shift, Undo };
enum class Stage { Waiting, Running, Done, Cancelled };

// What the last run said about a subtitle, kept between sessions
struct Result {
    std::string verdict;   // solid unsure nothing few failed shifted undone same
    int offset = 0;
    bool dry = false;
    long long when = 0;    // unix seconds
};

struct Job {
    int id = 0;
    Action action = Action::Sync;
    std::string video, subtitle;
    Options options;
    int shift = 0;

    Stage stage = Stage::Waiting;
    Result result;
    std::string mode, output, why, error;
    double sigma = 0;
    int parts = 1;
    bool written = false;
    Clock::time_point started, ended;
};

std::vector<std::string> engine_args(const Job& job);

// fills in the result from what the engine printed and how it ended
void settle(Job& job, const Ran& ran);

// Runs one job at a time in a thread of its own. changed is called from that
// thread whenever a job starts or ends
class Queue {
  public:
    Queue(std::string engine, std::function<void(const Job&)> changed);
    ~Queue();

    void add(Job job);
    // a waiting job is dropped, the running one is stopped
    void cancel(int id);
    void cancel_all();
    void clear_finished();
    std::vector<Job> jobs();
    int left();
    // blocks until nothing is waiting or running
    void drain();

  private:
    void work();

    std::string engine_;
    std::function<void(const Job&)> changed_;
    std::mutex lock_;
    std::condition_variable wake_, idle_;
    std::deque<Job> jobs_;
    int next_id_ = 1;
    bool quit_ = false;
    std::atomic<bool> stop_running_{false};
    std::thread worker_;
};
