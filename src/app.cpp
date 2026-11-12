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

#include "app.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <csignal>
#include <ctime>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

#include <dirent.h>
#include <sys/stat.h>

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/mouse.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/terminal.hpp>

#include "library.h"
#include "match.h"
#include "queue.h"
#include "state.h"
#include "text.h"

using namespace ftxui;

namespace {

std::atomic<bool> hung_up{false};

void on_hangup(int) {
    hung_up = true;
}

enum class View { Library, Queue };
enum class Filter { All, Todo, Questions, Bare };
enum class Overlay { None, Help, Find, Browse, Options, Places, Prompt, Confirm };
// what a pick in Find or Browse is for
enum class Pick { Jump, Video, Subtitle, Root };

const char* FILTER_NAMES[] = {"All", "To do", "Questions", "No subtitles"};
const std::vector<std::string> MODES = {"auto", "ols", "nosplit", "split"};

struct Row {
    enum Type { Film, Sub, Heading } type = Film;
    int index = -1;
    std::string text;
};

struct Found {
    bool video = false;
    int index = 0;
    int score = 0;
};

struct Entry {
    std::string name;
    bool dir = false;
};

std::string base_of(const std::string& path) {
    size_t slash = path.rfind('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string stem_of(const std::string& name) {
    size_t dot = name.rfind('.');
    return dot == std::string::npos || dot == 0 ? name : name.substr(0, dot);
}

bool under(const std::string& path, const std::string& dir) {
    return path == dir || (path.size() > dir.size() && path.compare(0, dir.size(), dir) == 0 &&
                           (dir == "/" || path[dir.size()] == '/'));
}

bool is_dir(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

bool finished(const Result& r) {
    return !r.dry && (r.verdict == "solid" || r.verdict == "same" || r.verdict == "shifted");
}

std::string percent(double score) {
    return std::to_string((int)(score * 100 + 0.5)) + "%";
}

std::string seconds(Clock::duration d) {
    long s = (long)std::chrono::duration_cast<std::chrono::seconds>(d).count();
    return s < 60 ? std::to_string(s) + "s" : std::to_string(s / 60) + "m" + std::to_string(s % 60) + "s";
}

std::string clock_text(long long when) {
    char buf[32];
    std::time_t t = (std::time_t)when;
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", std::localtime(&t));
    return buf;
}

// a line of text input: letters, backspace, ^U and ^W
bool edit(std::string& s, const Event& e) {
    if (e == Event::Backspace) {
        while (!s.empty() && ((unsigned char)s.back() & 0xC0) == 0x80) s.pop_back();
        if (!s.empty()) s.pop_back();
        return true;
    }
    if (e == Event::Special("\x15")) {
        s.clear();
        return true;
    }
    if (e == Event::Special("\x17")) {
        while (!s.empty() && s.back() == ' ') s.pop_back();
        while (!s.empty() && s.back() != ' ') s.pop_back();
        return true;
    }
    if (e.is_character()) {
        s += e.character();
        return true;
    }
    return false;
}

class App {
  public:
    App(Setup setup, ScreenInteractive& screen)
        : s_(std::move(setup)), screen_(screen),
          queue_(s_.engine.path, [this](const Job& job) { post([this, job] { job_changed(job); }); }) {}

    Component component() {
        return CatchEvent(Renderer([this] { return render(); }), [this](Event e) { return on_event(e); });
    }

    void start() {
        state_ = load_state(s_.state_file);
        have_cache_ = load_index(s_.index_file, index_);
        lib_ = std::make_shared<Library>();
        matcher_ = std::thread([this] { match_loop(); });
        ticker_ = std::thread([this] { tick(); });
        request_match();
        rescan();
    }

    int finish() {
        stopping_ = true;
        {
            std::lock_guard<std::mutex> hold(mail_lock_);
            mail_cv_.notify_all();
        }
        matcher_.join();
        ticker_.join();
        if (scanner_.joinable()) scanner_.join();
        // the terminal went away with jobs left, they get to finish without it
        if (hung_up) queue_.drain();
        else queue_.cancel_all();
        queue_.drain();
        for (auto& job : queue_.jobs()) remember(job);
        save_state(s_.state_file, state_);
        if (scanned_) save_index(s_.index_file, index_);
        return 0;
    }

  private:
    Setup s_;
    ScreenInteractive& screen_;
    Queue queue_;

    // only the screen's thread touches these, the others Post to it
    Index index_;
    bool have_cache_ = false, scanning_ = false, scanned_ = false;
    int scanned_folders_ = 0;
    State state_;
    std::shared_ptr<const Library> lib_;
    std::vector<int> versions_, version_, subs_count_;
    std::vector<std::string> film_names_;
    std::vector<Row> rows_;
    std::set<std::string> selected_;
    std::map<std::string, Job> busy_;
    std::vector<Job> jobs_;
    View view_ = View::Library;
    Filter filter_ = Filter::All;
    Overlay overlay_ = Overlay::None;
    int cursor_ = 0, top_ = 0, job_cursor_ = 0, job_top_ = 0, list_height_ = 10;
    std::string message_;
    bool message_bad_ = false;
    Options options_;
    int option_row_ = 0, place_row_ = 0;

    Pick pick_ = Pick::Jump;
    int pick_for_ = -1;
    std::string query_;
    std::vector<Found> found_;
    int found_cursor_ = 0;

    std::string browse_dir_;
    std::vector<Entry> entries_;
    int entry_cursor_ = 0;

    std::string prompt_, input_;
    std::function<void(const std::string&)> on_input_;
    std::function<void()> on_yes_;

    std::atomic<bool> stopping_{false};
    std::thread scanner_, matcher_, ticker_;
    std::mutex mail_lock_;
    std::condition_variable mail_cv_;
    std::optional<std::pair<Index, Choices>> mail_;

    // threads

    // runs fn on the screen's thread and draws again after it
    template <typename F>
    void post(F fn) {
        screen_.Post(Closure([fn = std::make_shared<F>(std::move(fn))] { (*fn)(); }));
        screen_.PostEvent(Event::Custom);
    }

    void match_loop() {
        std::unique_lock<std::mutex> hold(mail_lock_);
        while (true) {
            mail_cv_.wait(hold, [this] { return stopping_ || mail_.has_value(); });
            if (stopping_) return;
            auto [index, choices] = std::move(*mail_);
            mail_.reset();
            hold.unlock();
            auto lib = std::make_shared<const Library>(match(index, choices));
            post([this, lib] { set_library(lib); });
            hold.lock();
        }
    }

    void request_match() {
        std::lock_guard<std::mutex> hold(mail_lock_);
        mail_ = std::make_pair(index_, state_.choices);
        mail_cv_.notify_all();
    }

    void tick() {
        int n = 0;
        while (!stopping_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            if (hung_up) {
                screen_.Post(screen_.ExitLoopClosure());
                return;
            }
            // only the time a running job has taken changes on its own
            if (++n % 5 == 0 && queue_.left() > 0) screen_.PostEvent(Event::Custom);
        }
    }

    std::vector<std::string> scan_roots() {
        std::vector<std::string> roots = s_.roots;
        // a film paired by hand outside the roots is looked at too
        for (auto& [sub, video] : state_.choices.pinned) {
            for (const std::string& path : {sub, video}) {
                std::string dir = dir_of(path);
                bool inside = false;
                for (auto& r : roots) inside = inside || under(dir, r);
                if (!inside && is_dir(dir)) roots.push_back(dir);
            }
        }
        return roots;
    }

    void rescan() {
        if (scanning_) return;
        if (scanner_.joinable()) scanner_.join();
        scanning_ = true;
        scanned_folders_ = 0;
        bool partial = !have_cache_;
        scanner_ = std::thread([this, roots = scan_roots(), cached = index_, partial] {
            Index found;
            Walk walk;
            walk.roots = roots;
            walk.fences = s_.fences;
            walk.stop = &stopping_;
            auto last = Clock::now();
            walk.found = [&](Dir&& d) {
                found.push_back(std::move(d));
                if (Clock::now() - last < std::chrono::milliseconds(500)) return;
                last = Clock::now();
                int n = (int)found.size();
                // with nothing saved the films show up as they are found
                Index part = partial ? found : Index();
                post([this, n, part = std::move(part), partial]() mutable {
                    scanned_folders_ = n;
                    if (!partial) return;
                    index_ = std::move(part);
                    request_match();
                });
            };
            scan(walk, cached);
            if (stopping_) return;
            save_index(s_.index_file, found);
            post([this, found = std::move(found)]() mutable {
                index_ = std::move(found);
                scanned_folders_ = (int)index_.size();
                scanning_ = false;
                scanned_ = have_cache_ = true;
                request_match();
            });
        });
    }

    // what comes back from the threads

    void set_library(std::shared_ptr<const Library> lib) {
        lib_ = std::move(lib);
        const auto& v = lib_->videos;
        versions_.assign(v.size(), 1);
        version_.assign(v.size(), 1);
        for (size_t i = 1; i < v.size(); i++) {
            const Release &a = v[i - 1].release, &b = v[i].release;
            if (a.title.empty() || a.title != b.title || a.year != b.year || a.season != b.season || a.episode != b.episode)
                continue;
            version_[i] = version_[i - 1] + 1;
        }
        for (size_t i = v.size(); i-- > 0;)
            versions_[i] = i + 1 < v.size() && version_[i + 1] > version_[i] ? versions_[i + 1] : version_[i];
        film_names_ = film_names(*lib_);
        subs_count_.assign(v.size(), 0);
        for (auto& s : lib_->subs)
            if (s.video >= 0) subs_count_[s.video]++;
        build_rows();
    }

    // "Heat (1995)" says it better than Heat.1995.1080p.BluRay.x264-GRP, so a
    // folder that holds one film, in one or more versions, names it
    static std::vector<std::string> film_names(const Library& lib) {
        std::map<std::string, std::set<std::string>> films_in;
        auto film_key = [](const Release& r) {
            return title_text(r) + "|" + std::to_string(r.year) + "|" + std::to_string(r.season) + "|" + std::to_string(r.episode);
        };
        for (auto& v : lib.videos) films_in[dir_of(v.path)].insert(film_key(v.release));
        std::vector<std::string> out;
        for (auto& v : lib.videos) {
            std::string dir = dir_of(v.path);
            bool one = films_in[dir].size() == 1 && v.release.season < 0 && dir != "/";
            out.push_back(shown(one ? base_of(dir) : stem_of(v.name)));
        }
        return out;
    }

    void job_changed(const Job& job) {
        jobs_ = queue_.jobs();
        busy_.clear();
        for (auto& j : jobs_)
            if (j.stage == Stage::Waiting || j.stage == Stage::Running) busy_[j.subtitle] = j;
        if (job.stage != Stage::Done) return;
        remember(job);
        save_state(s_.state_file, state_);
        // a backup came or went, and lapse may have left a guess beside it
        Dir fresh;
        std::string dir = dir_of(job.subtitle);
        if (read_folder(dir, fresh)) {
            auto it = std::find_if(index_.begin(), index_.end(), [&](const Dir& d) { return d.path == dir; });
            if (it != index_.end()) *it = std::move(fresh);
            request_match();
        }
        if (job.result.verdict == "failed") say(base_of(job.subtitle) + ": " + job.error, true);
    }

    void remember(const Job& job) {
        if (job.stage != Stage::Done || job.result.verdict == "stopped") return;
        state_.results[job.subtitle] = job.result;
    }

    // the list

    bool to_do(const Subtitle& sub) {
        auto r = state_.results.find(sub.path);
        return sub.video >= 0 && (r == state_.results.end() || !finished(r->second));
    }

    bool shows(const Subtitle& sub) {
        switch (filter_) {
            case Filter::All: return true;
            case Filter::Todo: return to_do(sub);
            case Filter::Questions: return sub.score < SURE;
            case Filter::Bare: return false;
        }
        return true;
    }

    std::string row_key(const Row& r) {
        if (r.type == Row::Film) return "v" + lib_->videos[r.index].path;
        if (r.type == Row::Sub) return "s" + lib_->subs[r.index].path;
        return "h";
    }

    void build_rows() {
        std::string keep = cursor_ < (int)rows_.size() ? row_key(rows_[cursor_]) : "";
        rows_.clear();
        const Library& L = *lib_;
        std::vector<std::vector<int>> subs_of(L.videos.size());
        std::vector<int> loose;
        for (int i = 0; i < (int)L.subs.size(); i++) {
            if (L.subs[i].video >= 0) subs_of[L.subs[i].video].push_back(i);
            else loose.push_back(i);
        }
        for (int v = 0; v < (int)L.videos.size(); v++) {
            std::vector<int> subs;
            for (int i : subs_of[v])
                if (shows(L.subs[i])) subs.push_back(i);
            bool film = filter_ == Filter::All || !subs.empty() || (filter_ == Filter::Bare && subs_of[v].empty());
            if (!film) continue;
            rows_.push_back({Row::Film, v, ""});
            for (int i : subs) rows_.push_back({Row::Sub, i, ""});
        }
        if ((filter_ == Filter::All || filter_ == Filter::Questions) && !loose.empty()) {
            rows_.push_back({Row::Heading, -1, "Subtitles with no film (" + std::to_string(loose.size()) + ")"});
            for (int i : loose) rows_.push_back({Row::Sub, i, ""});
        }
        cursor_ = 0;
        for (int i = 0; i < (int)rows_.size(); i++)
            if (row_key(rows_[i]) == keep) cursor_ = i;
        if (cursor_ == 0 && !rows_.empty() && rows_[0].type == Row::Heading && rows_.size() > 1) cursor_ = 1;
    }

    const Subtitle* current_sub() {
        if (cursor_ >= (int)rows_.size() || rows_[cursor_].type != Row::Sub) return nullptr;
        return &lib_->subs[rows_[cursor_].index];
    }

    const Video* current_video() {
        if (cursor_ >= (int)rows_.size() || rows_[cursor_].type != Row::Film) return nullptr;
        return &lib_->videos[rows_[cursor_].index];
    }

    // the selection, or the subtitle under the cursor, or every one of the
    // film under the cursor
    std::vector<const Subtitle*> targets() {
        std::vector<const Subtitle*> out;
        if (!selected_.empty()) {
            for (auto& s : lib_->subs)
                if (selected_.count(s.path)) out.push_back(&s);
            return out;
        }
        if (const Subtitle* s = current_sub()) out.push_back(s);
        if (current_video()) {
            for (int i = cursor_ + 1; i < (int)rows_.size() && rows_[i].type == Row::Sub; i++)
                out.push_back(&lib_->subs[rows_[i].index]);
        }
        return out;
    }

    void say(const std::string& text, bool bad = false) {
        message_ = shown(text);
        message_bad_ = bad;
    }

    // actions

    bool engine_ready() {
        if (!s_.engine.path.empty()) return true;
        say("lapse was not found. Put it on PATH or set engine = in " + s_.config_file, true);
        return false;
    }

    void enqueue(Action task, const std::vector<const Subtitle*>& subs, int shift = 0) {
        if (!engine_ready()) return;
        int added = 0, skipped = 0;
        for (auto* s : subs) {
            bool needs_video = task == Action::Sync;
            bool no_backup = task == Action::Undo && !s->backup;
            if (busy_.count(s->path) || (needs_video && s->video < 0) || no_backup) {
                skipped++;
                continue;
            }
            Job job;
            job.action = task;
            job.subtitle = s->path;
            job.video = s->video >= 0 ? lib_->videos[s->video].path : "";
            job.options = options_;
            job.shift = shift;
            queue_.add(job);
            busy_[s->path] = job;
            added++;
        }
        jobs_ = queue_.jobs();
        selected_.clear();
        std::string what = task == Action::Sync ? "to sync" : task == Action::Undo ? "to put back" : "to shift";
        std::string text = plural(added, "subtitle", "subtitles") + " queued " + what;
        if (skipped) {
            text += ", " + std::to_string(skipped) + " left out: ";
            text += task == Action::Undo ? "no backup or already queued" : "no film or already queued";
        }
        say(text, added == 0);
    }

    void sync_all() {
        std::vector<const Subtitle*> subs;
        for (auto& r : rows_) {
            if (r.type != Row::Sub) continue;
            const Subtitle& s = lib_->subs[r.index];
            if (s.score >= SURE && to_do(s) && !busy_.count(s.path)) subs.push_back(&s);
        }
        if (subs.empty()) {
            say("Nothing sure is left to sync here");
            return;
        }
        ask("Sync " + plural((long long)subs.size(), "subtitle", "subtitles") +
                " that are sure and not done yet? Questions are left out",
            [this, subs] { enqueue(Action::Sync, subs); });
    }

    void pin(const std::string& sub, const std::string& video) {
        state_.choices.pinned[sub] = video;
        state_.choices.refused.erase(sub + "\t" + video);
        save_state(s_.state_file, state_);
        request_match();
        say("Paired " + base_of(sub) + " with " + base_of(video));
    }

    void unpair(const Subtitle& sub) {
        if (sub.video < 0) return;
        const std::string& video = lib_->videos[sub.video].path;
        state_.choices.pinned.erase(sub.path);
        state_.choices.refused.insert(sub.path + "\t" + video);
        save_state(s_.state_file, state_);
        request_match();
        say("Will not pair " + base_of(sub.path) + " with " + base_of(video) + " again. m pairs it by hand");
    }

    void ask(const std::string& question, std::function<void()> yes) {
        prompt_ = question;
        on_yes_ = std::move(yes);
        overlay_ = Overlay::Confirm;
    }

    void prompt(const std::string& question, std::function<void(const std::string&)> done) {
        prompt_ = question;
        input_.clear();
        on_input_ = std::move(done);
        overlay_ = Overlay::Prompt;
    }

    void shift() {
        auto subs = targets();
        if (subs.empty()) return;
        prompt("Move " + plural((long long)subs.size(), "subtitle", "subtitles") +
                   " by how many ms? Negative is earlier",
               [this, subs](const std::string& text) {
                   char* end = nullptr;
                   long ms = std::strtol(text.c_str(), &end, 10);
                   if (text.empty() || *end || ms == 0 || std::labs(ms) > 36000000) {
                       say("A shift is a whole number of ms, not 0", true);
                       return;
                   }
                   enqueue(Action::Shift, subs, (int)ms);
               });
    }

    void open_find(Pick pick, int index) {
        pick_ = pick;
        pick_for_ = index;
        query_.clear();
        // pairing starts from the title, it is the likely answer
        if (pick == Pick::Video) query_ = title_text(lib_->subs[index].release);
        if (pick == Pick::Subtitle) query_ = title_text(lib_->videos[index].release);
        overlay_ = Overlay::Find;
        refind();
    }

    void refind() {
        found_.clear();
        found_cursor_ = 0;
        std::string q = lower(query_);
        const Library& L = *lib_;
        auto consider = [&](bool video, int i, const std::string& path) {
            std::string text = base_of(dir_of(path)) + "/" + base_of(path);
            int score = fuzzy_score(q, text);
            if (score >= 0) found_.push_back({video, i, score});
        };
        if (pick_ != Pick::Subtitle)
            for (int i = 0; i < (int)L.videos.size(); i++) consider(true, i, L.videos[i].path);
        if (pick_ != Pick::Video)
            for (int i = 0; i < (int)L.subs.size(); i++) consider(false, i, L.subs[i].path);
        size_t keep = std::min<size_t>(found_.size(), 200);
        std::partial_sort(found_.begin(), found_.begin() + keep, found_.end(),
                          [](const Found& a, const Found& b) {
                              if (a.score != b.score) return a.score > b.score;
                              return a.video != b.video ? a.video : a.index < b.index;
                          });
        found_.resize(keep);
    }

    void choose_found() {
        if (found_cursor_ >= (int)found_.size()) return;
        Found f = found_[found_cursor_];
        overlay_ = Overlay::None;
        if (pick_ == Pick::Video) {
            pin(lib_->subs[pick_for_].path, lib_->videos[f.index].path);
        } else if (pick_ == Pick::Subtitle) {
            pin(lib_->subs[f.index].path, lib_->videos[pick_for_].path);
        } else {
            filter_ = Filter::All;
            build_rows();
            std::string key = (f.video ? "v" + lib_->videos[f.index].path : "s" + lib_->subs[f.index].path);
            for (int i = 0; i < (int)rows_.size(); i++)
                if (row_key(rows_[i]) == key) cursor_ = i;
        }
    }

    void open_browser(const std::string& dir) {
        browse_dir_ = dir;
        entries_.clear();
        entry_cursor_ = 0;
        if (DIR* d = opendir(dir.c_str())) {
            while (dirent* e = readdir(d)) {
                std::string name = e->d_name;
                if (name.empty() || name[0] == '.') continue;
                std::string full = dir == "/" ? "/" + name : dir + "/" + name;
                bool folder = is_dir(full);
                Kind k = kind_of(name);
                bool wanted = pick_ == Pick::Video ? k == Kind::Video : pick_ == Pick::Subtitle ? k == Kind::Subtitle : false;
                if (folder || wanted) entries_.push_back({name, folder});
            }
            closedir(d);
        }
        std::sort(entries_.begin(), entries_.end(), [](const Entry& a, const Entry& b) {
            return a.dir != b.dir ? a.dir : lower(a.name) < lower(b.name);
        });
        entries_.insert(entries_.begin(), {"..", true});
        overlay_ = Overlay::Browse;
    }

    void choose_entry() {
        if (entry_cursor_ >= (int)entries_.size()) return;
        Entry e = entries_[entry_cursor_];
        std::string path = e.name == ".." ? dir_of(browse_dir_) : (browse_dir_ == "/" ? "" : browse_dir_) + "/" + e.name;
        if (e.dir) {
            open_browser(path);
            return;
        }
        overlay_ = Overlay::None;
        // the file may be outside the roots, its folder goes in the index
        Dir fresh;
        std::string dir = dir_of(path);
        if (read_folder(dir, fresh)) {
            auto it = std::find_if(index_.begin(), index_.end(), [&](const Dir& d) { return d.path == dir; });
            if (it != index_.end()) *it = std::move(fresh);
            else index_.push_back(std::move(fresh));
        }
        if (pick_ == Pick::Video) pin(lib_->subs[pick_for_].path, path);
        else pin(path, lib_->videos[pick_for_].path);
    }

    void add_root(const std::string& dir) {
        overlay_ = Overlay::Places;
        if (std::find(s_.roots.begin(), s_.roots.end(), dir) != s_.roots.end()) return;
        s_.roots.push_back(dir);
        places_changed();
    }

    void places_changed() {
        s_.config.roots = s_.roots;
        s_.fences = fences(read_mounts(), s_.config.network);
        if (!s_.roots_given) save_config(s_.config_file, s_.config);
        say(s_.roots_given ? "Looking again, this time only, the folders came from the command line"
                           : "Saved in " + s_.config_file + ", looking again");
        rescan();
    }

    // events

    bool on_event(Event e) {
        if (e == Event::Custom) return true;
        if (e.is_mouse()) return on_mouse(e);
        if (e == Event::CtrlC) {
            quit();
            return true;
        }
        if (overlay_ != Overlay::None) {
            on_overlay(e);
            return true;
        }
        message_.clear();
        if (e == Event::Character('q')) quit();
        else if (e == Event::Tab || e == Event::Character('1') || e == Event::Character('2'))
            view_ = e == Event::Character('1') ? View::Library
                  : e == Event::Character('2') ? View::Queue
                  : view_ == View::Library     ? View::Queue
                                               : View::Library;
        else if (e == Event::Character('?')) overlay_ = Overlay::Help;
        else if (e == Event::Character('/')) open_find(Pick::Jump, -1);
        else if (e == Event::Character('o')) overlay_ = Overlay::Options;
        else if (e == Event::Character('p')) overlay_ = Overlay::Places;
        else if (e == Event::Character('d')) options_.dry = !options_.dry;
        else if (e == Event::Character('r')) {
            if (scanning_) say("Already looking");
            else rescan();
        } else if (view_ == View::Library) return on_library(e);
        else return on_queue(e);
        return true;
    }

    void quit() {
        int left = queue_.left();
        if (left == 0) {
            screen_.Exit();
            return;
        }
        ask(plural(left, "job is", "jobs are") + " not done. Stop and quit?", [this] { screen_.Exit(); });
    }

    bool move(const Event& e, int& cursor, int count) {
        int page = std::max(1, list_height_ - 1);
        if (e == Event::ArrowUp || e == Event::Character('k')) cursor--;
        else if (e == Event::ArrowDown || e == Event::Character('j')) cursor++;
        else if (e == Event::PageUp) cursor -= page;
        else if (e == Event::PageDown) cursor += page;
        else if (e == Event::Home || e == Event::Character('g')) cursor = 0;
        else if (e == Event::End || e == Event::Character('G')) cursor = count - 1;
        else return false;
        cursor = std::clamp(cursor, 0, std::max(0, count - 1));
        return true;
    }

    bool on_library(const Event& e) {
        if (move(e, cursor_, (int)rows_.size())) return true;
        const Subtitle* sub = current_sub();
        const Video* video = current_video();
        if (e == Event::Character(' ')) {
            for (auto* s : targets_here()) {
                if (selected_.count(s->path)) selected_.erase(s->path);
                else selected_.insert(s->path);
            }
            // on to the next line, past the subtitles of a film just marked
            do cursor_++;
            while (video && cursor_ < (int)rows_.size() && rows_[cursor_].type == Row::Sub);
            cursor_ = std::min(cursor_, (int)rows_.size() - 1);
        } else if (e == Event::Escape) {
            selected_.clear();
        } else if (e == Event::Return) {
            enqueue(Action::Sync, targets());
        } else if (e == Event::Character('a')) {
            sync_all();
        } else if (e == Event::Character('u')) {
            enqueue(Action::Undo, targets());
        } else if (e == Event::Character('s')) {
            shift();
        } else if (e == Event::Character('f')) {
            filter_ = (Filter)(((int)filter_ + 1) % 4);
            build_rows();
        } else if (e == Event::Character('m')) {
            if (sub) open_find(Pick::Video, rows_[cursor_].index);
            else if (video) open_find(Pick::Subtitle, rows_[cursor_].index);
        } else if (e == Event::Character('c')) {
            if (sub && sub->video >= 0) pin(sub->path, lib_->videos[sub->video].path);
        } else if (e == Event::Character('x')) {
            if (sub) unpair(*sub);
        } else {
            return false;
        }
        return true;
    }

    // space marks what is under the cursor, a film with all of its subtitles
    std::vector<const Subtitle*> targets_here() {
        auto keep = std::move(selected_);
        selected_.clear();
        auto out = targets();
        selected_ = std::move(keep);
        return out;
    }

    bool on_queue(const Event& e) {
        if (move(e, job_cursor_, (int)jobs_.size())) return true;
        if (e == Event::Character('x') && job_cursor_ < (int)jobs_.size()) {
            queue_.cancel(jobs_[job_cursor_].id);
        } else if (e == Event::Character('X')) {
            queue_.cancel_all();
        } else if (e == Event::Character('C')) {
            queue_.clear_finished();
        } else if (e == Event::Return && job_cursor_ < (int)jobs_.size()) {
            std::string key = "s" + jobs_[job_cursor_].subtitle;
            filter_ = Filter::All;
            build_rows();
            for (int i = 0; i < (int)rows_.size(); i++)
                if (row_key(rows_[i]) == key) cursor_ = i;
            view_ = View::Library;
            return true;
        } else {
            return false;
        }
        jobs_ = queue_.jobs();
        job_cursor_ = std::min(job_cursor_, std::max(0, (int)jobs_.size() - 1));
        return true;
    }

    bool on_mouse(Event& e) {
        if (overlay_ != Overlay::None) return true;
        Mouse m = e.mouse();
        int& cursor = view_ == View::Library ? cursor_ : job_cursor_;
        int& top = view_ == View::Library ? top_ : job_top_;
        int count = view_ == View::Library ? (int)rows_.size() : (int)jobs_.size();
        if (m.button == Mouse::WheelUp || m.button == Mouse::WheelDown) {
            cursor = std::clamp(cursor + (m.button == Mouse::WheelUp ? -3 : 3), 0, std::max(0, count - 1));
            return true;
        }
        if (m.button != Mouse::Left || m.motion != Mouse::Pressed) return true;
        if (m.y == 0) {
            view_ = m.x < 22 ? View::Library : View::Queue;
            return true;
        }
        if (m.y == 1 && view_ == View::Library) {
            filter_ = (Filter)(((int)filter_ + 1) % 4);
            build_rows();
            return true;
        }
        int row = top + m.y - 3;
        if (m.y >= 3 && m.y < 3 + list_height_ && row < count) cursor = row;
        return true;
    }

    void on_overlay(const Event& e) {
        if (e == Event::Escape && overlay_ != Overlay::Browse) {
            overlay_ = overlay_ == Overlay::Find || overlay_ == Overlay::Help || overlay_ == Overlay::Confirm ||
                               overlay_ == Overlay::Prompt || overlay_ == Overlay::Options || overlay_ == Overlay::Places
                           ? Overlay::None
                           : overlay_;
            return;
        }
        switch (overlay_) {
            case Overlay::Help:
                overlay_ = Overlay::None;
                break;
            case Overlay::Confirm:
                if (e == Event::Character('y') || e == Event::Character('Y') || e == Event::Return) {
                    overlay_ = Overlay::None;
                    on_yes_();
                } else if (e.is_character()) {
                    overlay_ = Overlay::None;
                }
                break;
            case Overlay::Prompt:
                if (e == Event::Return) {
                    overlay_ = Overlay::None;
                    on_input_(input_);
                } else {
                    edit(input_, e);
                }
                break;
            case Overlay::Find: on_find(e); break;
            case Overlay::Browse: on_browse(e); break;
            case Overlay::Options: on_options(e); break;
            case Overlay::Places: on_places(e); break;
            case Overlay::None: break;
        }
    }

    void on_find(const Event& e) {
        if (e == Event::ArrowUp || e == Event::ArrowDown || e == Event::PageUp || e == Event::PageDown) {
            move(e, found_cursor_, (int)found_.size());
        } else if (e == Event::Return) {
            choose_found();
        } else if (e == Event::Tab && pick_ != Pick::Jump) {
            std::string start = pick_ == Pick::Video ? dir_of(lib_->subs[pick_for_].path) : dir_of(lib_->videos[pick_for_].path);
            open_browser(start);
        } else if (edit(query_, e)) {
            refind();
        }
    }

    void on_browse(const Event& e) {
        if (move(e, entry_cursor_, (int)entries_.size())) return;
        if (e == Event::Return || e == Event::ArrowRight || e == Event::Character('l')) {
            choose_entry();
        } else if (e == Event::Backspace || e == Event::ArrowLeft || e == Event::Character('h')) {
            open_browser(dir_of(browse_dir_));
        } else if (e == Event::Character('~')) {
            const char* home = std::getenv("HOME");
            open_browser(home ? home : "/");
        } else if (e == Event::Character('a') && pick_ == Pick::Root) {
            add_root(browse_dir_);
        } else if (e == Event::Escape) {
            overlay_ = pick_ == Pick::Root ? Overlay::Places : Overlay::Find;
        }
    }

    void on_places(const Event& e) {
        if (move(e, place_row_, (int)s_.roots.size())) return;
        if (e == Event::Character('a')) {
            pick_ = Pick::Root;
            const char* home = std::getenv("HOME");
            open_browser(home ? home : "/");
        } else if (e == Event::Character('d') && place_row_ < (int)s_.roots.size() && s_.roots.size() > 1) {
            s_.roots.erase(s_.roots.begin() + place_row_);
            place_row_ = std::min(place_row_, (int)s_.roots.size() - 1);
            places_changed();
        } else if (e == Event::Character('n')) {
            s_.config.network = !s_.config.network;
            places_changed();
        }
    }

    // the options, each a list of values that left and right go through
    struct Choice {
        std::string name;
        std::vector<std::string> values;
        int at;
        std::string note;
    };

    std::vector<Choice> choices() {
        auto index_of = [](const std::vector<int>& v, int x) {
            return (int)(std::find(v.begin(), v.end(), x) - v.begin()) % (int)v.size();
        };
        int mode = (int)(std::find(MODES.begin(), MODES.end(), options_.mode) - MODES.begin());
        return {
            {"Mode", MODES, mode, "auto finds shifts, drift and cuts on its own"},
            {"Audio track", {"lapse picks", "1", "2", "3", "4"}, index_of({0, 1, 2, 3, 4}, options_.audio_track),
             "the audio to listen to"},
            {"Subtitle track", {"lapse picks", "none", "1", "2", "3"}, index_of({0, -1, 1, 2, 3}, options_.sub_track),
             "a subtitle inside the video to follow, none listens to the audio"},
            {"Confidence", {"default (8)", "4", "6", "10", "12", "16"}, index_of({0, 4, 6, 10, 12, 16}, options_.confidence),
             "how sure lapse has to be to write over the file"},
            {"Snap to cuts", {"off", "60 ms", "120 ms", "250 ms"}, index_of({0, 60, 120, 250}, options_.snap),
             "move line starts onto picture cuts close by"},
            {"Dry run", {"off", "on"}, options_.dry ? 1 : 0, "work out the answer, write nothing"},
            {"Backup", {"keep a .bak", "off"}, options_.backup ? 0 : 1, "the .bak is what u puts back"},
            {"Force", {"off", "on"}, options_.force ? 1 : 0, "write even when lapse is unsure"},
        };
    }

    void set_choice(int row, int at) {
        static const std::vector<std::vector<int>> numbers = {
            {}, {0, 1, 2, 3, 4}, {0, -1, 1, 2, 3}, {0, 4, 6, 10, 12, 16}, {0, 60, 120, 250}};
        switch (row) {
            case 0: options_.mode = MODES[at]; break;
            case 1: options_.audio_track = numbers[1][at]; break;
            case 2: options_.sub_track = numbers[2][at]; break;
            case 3: options_.confidence = numbers[3][at]; break;
            case 4: options_.snap = numbers[4][at]; break;
            case 5: options_.dry = at == 1; break;
            case 6: options_.backup = at == 0; break;
            case 7: options_.force = at == 1; break;
        }
    }

    void on_options(const Event& e) {
        auto list = choices();
        if (move(e, option_row_, (int)list.size())) return;
        int step = e == Event::ArrowRight || e == Event::Character('l') || e == Event::Character(' ') ? 1
                 : e == Event::ArrowLeft || e == Event::Character('h')                               ? -1
                                                                                                      : 0;
        if (e == Event::Return) overlay_ = Overlay::None;
        if (!step) return;
        Choice& c = list[option_row_];
        int at = (c.at + step + (int)c.values.size()) % (int)c.values.size();
        // the backup is what makes a sync safe to try, so it takes a yes to drop it
        if (option_row_ == 6 && at == 1) {
            ask("Write over subtitles with no .bak to put back?", [this] {
                options_.backup = false;
                overlay_ = Overlay::Options;
            });
            return;
        }
        set_choice(option_row_, at);
    }

    // drawing

    Decorator tint(Color c) {
        return s_.color ? color(c) : nothing;
    }

    Element render() {
        auto size = Terminal::Size();
        int w = std::max(20, size.dimx), h = std::max(8, size.dimy);
        list_height_ = std::max(1, h - 8);
        Elements lines = {header(), subheader(w), separator()};
        if (view_ == View::Library) {
            library_lines(lines, w);
            lines.push_back(separator());
            for (auto& l : library_detail(w)) lines.push_back(l);
        } else {
            queue_lines(lines, w);
            lines.push_back(separator());
            for (auto& l : queue_detail(w)) lines.push_back(l);
        }
        lines.push_back(hints(w));
        Element main = vbox(std::move(lines));
        if (overlay_ == Overlay::None) return main;
        return dbox({main, overlay(w, h) | clear_under | center});
    }

    Element header() {
        int left = queue_.left();
        std::string queue = "Queue";
        if (left) queue += " " + std::to_string(left);
        Elements parts = {text(" lapse-tui ") | bold, text(" "),
                          text(" Library ") | (view_ == View::Library ? inverted : dim), text(" "),
                          text(" " + queue + " ") | (view_ == View::Queue ? inverted : dim)};
        Elements right;
        if (options_.dry) right.push_back(text(" DRY RUN ") | bold | tint(Color::Cyan));
        if (!options_.backup) right.push_back(text(" NO BACKUP ") | bold | tint(Color::Red));
        if (options_.force) right.push_back(text(" FORCE ") | bold | tint(Color::Yellow));
        std::string engine = s_.engine.path.empty() ? "no lapse found" : "lapse " + s_.engine.version;
        right.push_back(text(" " + engine + " ") | (s_.engine.path.empty() ? tint(Color::Red) : dim));
        parts.push_back(filler());
        for (auto& r : right) parts.push_back(r);
        return hbox(std::move(parts));
    }

    Element subheader(int w) {
        Elements parts;
        if (view_ == View::Library) {
            for (int i = 0; i < 4; i++) {
                std::string name = FILTER_NAMES[i];
                parts.push_back(text(" ") | dim);
                parts.push_back((int)filter_ == i ? text(name) | bold | underlined : text(name) | dim);
            }
        } else {
            int done = 0, failed = 0;
            for (auto& j : jobs_) {
                done += j.stage == Stage::Done;
                failed += j.stage == Stage::Done && j.result.verdict == "failed";
            }
            std::string line = jobs_.empty() ? "" : " " + std::to_string(done) + " of " + std::to_string(jobs_.size()) + " done ";
            if (failed) line += ", " + std::to_string(failed) + " failed";
            parts.push_back(text(line));
            if (!jobs_.empty()) parts.push_back(gauge((float)done / (float)jobs_.size()) | size(WIDTH, EQUAL, std::min(20, w / 5)));
        }
        int room = w - 2;
        for (auto& p : parts) {
            p->ComputeRequirement();
            room -= p->requirement().min_x;
        }
        parts.push_back(filler());
        if (!message_.empty()) {
            parts.push_back(text(" " + cut(message_, room) + " ") | (message_bad_ ? tint(Color::Red) : nothing));
        } else {
            std::string counts = plural((long long)lib_->videos.size(), "video", "videos") + ", " +
                                 plural((long long)lib_->subs.size(), "subtitle", "subtitles");
            if (scanning_) counts = "looking, " + plural(scanned_folders_, "folder", "folders") + " so far, " + counts;
            parts.push_back(text(" " + cut(counts, room) + " ") | dim);
        }
        return hbox(std::move(parts));
    }

    std::string result_text(const std::string& path, Color& c) {
        auto busy = busy_.find(path);
        if (busy != busy_.end()) {
            c = Color::Cyan;
            const Job& j = busy->second;
            if (j.stage == Stage::Running) return "syncing " + seconds(Clock::now() - j.started);
            return "waiting";
        }
        auto r = state_.results.find(path);
        if (r == state_.results.end()) return "";
        const Result& res = r->second;
        const std::string& v = res.verdict;
        c = v == "solid" || v == "shifted" || v == "same" || v == "undone" ? Color::Green
          : v == "failed"                                                ? Color::Red
                                                                         : Color::Yellow;
        std::string out = v == "few" ? "too few cues" : v;
        if (v == "solid" || v == "unsure" || v == "nothing" || v == "shifted") out += " " + offset_text(res.offset);
        if (res.dry) out = "dry " + out;
        return out;
    }

    void library_lines(Elements& lines, int w) {
        if (rows_.empty()) {
            std::string why = scanning_ ? "Looking for films..." : filter_ == Filter::All ? "No films found. p shows where it looks" : "Nothing here. f shows the next list";
            lines.push_back(text("  " + why) | dim);
            for (int i = 1; i < list_height_; i++) lines.push_back(text(""));
            return;
        }
        cursor_ = std::clamp(cursor_, 0, (int)rows_.size() - 1);
        top_ = std::clamp(top_, std::max(0, cursor_ - list_height_ + 1), cursor_);
        top_ = std::min(top_, std::max(0, (int)rows_.size() - list_height_));
        for (int i = top_; i < top_ + list_height_; i++) {
            if (i >= (int)rows_.size()) {
                lines.push_back(text(""));
                continue;
            }
            Element line = library_row(rows_[i], w);
            if (i == cursor_) line = line | inverted;
            lines.push_back(line);
        }
    }

    Element library_row(const Row& r, int w) {
        const Library& L = *lib_;
        if (r.type == Row::Heading) return text(" " + pad(r.text, w - 1)) | bold;
        if (r.type == Row::Film) {
            const Video& v = L.videos[r.index];
            int subs = subs_count_[r.index];
            std::string right;
            if (versions_[r.index] > 1) {
                const Release& rel = v.release;
                right = rel.resolution.empty() ? "" : rel.resolution + " ";
                right += (rel.edition.empty() ? "" : rel.edition + " ") + std::to_string(version_[r.index]) + " of " +
                         std::to_string(versions_[r.index]) + "  ";
            }
            right += subs ? plural(subs, "sub", "subs") : "no subs";
            int name_w = w - 3 - cells(right);
            return hbox({text(" " + pad(film_names_[r.index], name_w) + " ") | bold, text(right + " ") | dim});
        }
        const Subtitle& s = L.subs[r.index];
        bool picked = selected_.count(s.path) > 0;
        std::string lang = s.release.language.empty() ? "--" : s.release.language;
        if (s.release.forced) lang += "f";
        Color c = Color::Default;
        std::string result = result_text(s.path, c);
        std::string score = s.video < 0 ? "" : s.why == "paired by hand" ? "hand" : percent(s.score);
        bool question = s.video >= 0 && s.score < SURE;
        int result_w = std::min(22, std::max(10, w / 4));
        int name_w = std::max(4, w - 8 - 6 - result_w - 2);
        Elements parts = {
            text(picked ? "+" : " ") | bold | tint(Color::Cyan),
            text("   " + pad(lang, 4)) | dim,
            text(pad(squeeze(shown(s.name), name_w), name_w)),
            text(pad_left(score, 5)) | (question ? tint(Color::Yellow) : dim),
            text(question ? "? " : "  ") | tint(Color::Yellow),
            text(pad(result, result_w)) | tint(c),
        };
        return hbox(std::move(parts));
    }

    Elements library_detail(int w) {
        std::vector<std::pair<std::string, Decorator>> l;
        const Library& L = *lib_;
        if (const Subtitle* s = current_sub()) {
            l.push_back({cut_front(shown(s->path), w - 2), nothing});
            if (s->video < 0) {
                l.push_back({"No film found for it. m picks one", tint(Color::Yellow)});
            } else {
                std::string pair = "Pairs with " + shown(L.videos[s->video].name) + ", " +
                                   (s->why == "paired by hand" ? "paired by hand" : percent(s->score) + ", " + s->why);
                l.push_back({pair, s->score < SURE ? tint(Color::Yellow) : nothing});
            }
            std::string last = "Not synced yet";
            auto r = state_.results.find(s->path);
            if (r != state_.results.end())
                last = "Last: " + (r->second.dry ? std::string("dry run, ") : "") + r->second.verdict + ", " +
                       offset_text(r->second.offset) + ", " + clock_text(r->second.when);
            if (s->backup) last += ". A .bak is kept, u puts it back";
            l.push_back({last, dim});
        } else if (const Video* v = current_video()) {
            int i = rows_[cursor_].index;
            const Release& rel = v->release;
            l.push_back({cut_front(shown(v->path), w - 2), nothing});
            std::string facts = size_text(v->size);
            for (auto* part : {&rel.resolution, &rel.source, &rel.edition, &rel.group})
                if (!part->empty()) facts += ", " + *part;
            if (versions_[i] > 1) facts += ", version " + std::to_string(version_[i]) + " of " + std::to_string(versions_[i]);
            l.push_back({facts, nothing});
            l.push_back({"Enter syncs its subtitles, m pairs one by hand, space marks them all", dim});
        } else {
            l = {{"", nothing}, {"", nothing}, {"", nothing}};
        }
        Elements out;
        for (auto& [line, deco] : l) out.push_back(text(" " + cut(line, w - 2)) | deco);
        return out;
    }

    void queue_lines(Elements& lines, int w) {
        if (jobs_.empty()) {
            lines.push_back(text("  Nothing queued. Enter in the library syncs what is under the cursor") | dim);
            for (int i = 1; i < list_height_; i++) lines.push_back(text(""));
            return;
        }
        job_cursor_ = std::clamp(job_cursor_, 0, (int)jobs_.size() - 1);
        job_top_ = std::clamp(job_top_, std::max(0, job_cursor_ - list_height_ + 1), job_cursor_);
        for (int i = job_top_; i < job_top_ + list_height_; i++) {
            if (i >= (int)jobs_.size()) {
                lines.push_back(text(""));
                continue;
            }
            const Job& j = jobs_[i];
            Color c = Color::Default;
            std::string state;
            if (j.stage == Stage::Waiting) state = "waiting", c = Color::Default;
            else if (j.stage == Stage::Running) state = "running " + seconds(Clock::now() - j.started), c = Color::Cyan;
            else if (j.stage == Stage::Cancelled) state = "cancelled";
            else {
                const std::string& v = j.result.verdict;
                c = v == "failed" ? Color::Red : v == "unsure" || v == "nothing" || v == "few" || v == "stopped" ? Color::Yellow : Color::Green;
                state = v == "few" ? "too few cues" : v;
                if (v == "solid" || v == "unsure" || v == "nothing" || v == "shifted") state += " " + offset_text(j.result.offset);
                if (j.result.dry) state = "dry " + state;
            }
            std::string task = j.action == Action::Undo ? "undo" : j.action == Action::Shift ? "shift " + offset_text(j.shift) : j.options.mode;
            int state_w = std::min(24, w / 3);
            Element line = hbox({text(" " + pad(state, state_w)) | tint(c), text(pad(shown(base_of(j.subtitle)), w - state_w - 15)),
                                 text(pad_left(task, 12) + " ") | dim});
            if (i == job_cursor_) line = line | inverted;
            lines.push_back(line);
        }
    }

    Elements queue_detail(int w) {
        std::vector<std::string> l = {"", "", ""};
        if (job_cursor_ < (int)jobs_.size()) {
            const Job& j = jobs_[job_cursor_];
            l[0] = cut_front(shown(j.subtitle), w - 2);
            std::string args = "lapse";
            for (std::string a : engine_args(j)) {
                if (a == j.video || a == j.subtitle) a = base_of(a);
                args += " " + (a.find(' ') != std::string::npos ? "\"" + a + "\"" : a);
            }
            l[1] = shown(args);
            if (j.stage == Stage::Done) {
                if (!j.error.empty()) l[2] = j.error;
                else if (!j.why.empty()) l[2] = j.why + (j.output.empty() ? "" : ", see " + base_of(j.output));
                else if (!j.mode.empty()) l[2] = j.mode + ", " + plural(j.parts, "part", "parts") + (j.written ? ", written" : ", nothing written");
                if (j.stage == Stage::Done && !j.mode.empty()) l[2] += ", took " + seconds(j.ended - j.started);
            }
        }
        Elements out;
        out.push_back(text(" " + cut(l[0], w - 2)));
        out.push_back(text(" " + cut(l[1], w - 2)) | dim);
        out.push_back(text(" " + cut(l[2], w - 2)));
        return out;
    }

    Element hints(int w) {
        std::vector<std::pair<std::string, std::string>> keys;
        const Subtitle* sub = view_ == View::Library ? current_sub() : nullptr;
        if (sub && sub->video >= 0 && sub->score < SURE) keys = {{"c", "yes"}, {"x", "no"}};
        if (view_ == View::Library)
            keys.insert(keys.end(), {{"enter", "sync"}, {"space", "mark"}, {"a", "sync all"}, {"m", "pair"}, {"/", "find"},
                    {"f", "filter"}, {"u", "undo"}, {"s", "shift"}, {"o", "options"}, {"tab", "queue"}, {"?", "help"}, {"q", "quit"}});
        else
            keys = {{"x", "cancel"}, {"X", "cancel all"}, {"C", "clear done"}, {"enter", "show"}, {"tab", "library"},
                    {"?", "help"}, {"q", "quit"}};
        Elements parts;
        int used = 0;
        for (auto& [k, what] : keys) {
            int need = cells(k) + cells(what) + 3;
            if (used + need > w) break;
            parts.push_back(text(" " + k) | bold);
            parts.push_back(text(" " + what + " ") | dim);
            used += need;
        }
        return hbox(std::move(parts));
    }

    Element window_of(const std::string& title, Elements body, int w) {
        return window(text(" " + title + " ") | bold, vbox(std::move(body))) | size(WIDTH, EQUAL, w);
    }

    Element overlay(int w, int h) {
        int ow = std::min(w - 2, 76), oh = std::max(5, std::min(h - 4, 20));
        int inner = ow - 2;
        Elements b;
        switch (overlay_) {
            case Overlay::Help: return help(ow);
            case Overlay::Confirm:
                b.push_back(paragraph(prompt_));
                b.push_back(text(""));
                b.push_back(hbox({text("y") | bold, text(" yes  ") | dim, text("any other key") | bold, text(" no") | dim}));
                return window_of("Sure?", std::move(b), std::min(ow, 60));
            case Overlay::Prompt:
                b.push_back(paragraph(prompt_));
                b.push_back(hbox({text("> "), text(input_), text(" ") | inverted}));
                return window_of("Shift", std::move(b), std::min(ow, 60));
            case Overlay::Find: return find_box(ow, oh, inner);
            case Overlay::Browse: return browse_box(ow, oh, inner);
            case Overlay::Options: return options_box(ow, inner);
            case Overlay::Places: return places_box(ow, oh, inner);
            case Overlay::None: break;
        }
        return text("");
    }

    Element find_box(int ow, int oh, int inner) {
        Elements b;
        b.push_back(hbox({text("> ") | bold, text(query_), text(" ") | inverted}));
        int rows = oh - 4;
        int top = std::max(0, found_cursor_ - rows + 1);
        for (int i = top; i < top + rows; i++) {
            if (i >= (int)found_.size()) {
                b.push_back(text(i == 0 ? "  nothing matches" : "") | dim);
                continue;
            }
            const Found& f = found_[i];
            const std::string& path = f.video ? lib_->videos[f.index].path : lib_->subs[f.index].path;
            // a release folder is named like the file in it, the one above says more
            std::string dir = dir_of(path);
            if (lower(base_of(path)).rfind(lower(base_of(dir)) + ".", 0) == 0) dir = dir_of(dir);
            std::string folder = shown(base_of(dir));
            int name_w = inner - 8 - std::min(24, inner / 3);
            Element line = hbox({text(f.video ? " film  " : " sub   ") | dim, text(pad(squeeze(shown(base_of(path)), name_w), name_w)),
                                 text(" " + cut_front(folder, inner - name_w - 8)) | dim});
            if (i == found_cursor_) line = line | inverted;
            b.push_back(line);
        }
        std::string hint = pick_ == Pick::Jump ? "enter go to it   esc close" : "enter pair   tab browse the disk   esc close";
        b.push_back(text(hint) | dim);
        std::string title = pick_ == Pick::Jump ? "Find" : pick_ == Pick::Video ? "Pick the film" : "Pick a subtitle";
        return window_of(title, std::move(b), ow);
    }

    Element browse_box(int ow, int oh, int inner) {
        Elements b;
        b.push_back(text(cut_front(shown(browse_dir_), inner)) | bold);
        int rows = oh - 4;
        int top = std::max(0, entry_cursor_ - rows + 1);
        for (int i = top; i < top + rows; i++) {
            if (i >= (int)entries_.size()) {
                b.push_back(text(""));
                continue;
            }
            const Entry& e = entries_[i];
            Element line = text(" " + pad(shown(e.name) + (e.dir ? "/" : ""), inner - 1)) | (e.dir ? nothing : tint(Color::Cyan));
            if (i == entry_cursor_) line = line | inverted;
            b.push_back(line);
        }
        b.push_back(text(pick_ == Pick::Root ? "enter open   a add this folder   backspace up   esc back"
                                             : "enter open or pick   backspace up   ~ home   esc back") | dim);
        return window_of(pick_ == Pick::Root ? "Add a folder to look in" : "Browse", std::move(b), ow);
    }

    Element options_box(int ow, int inner) {
        Elements b;
        auto list = choices();
        for (int i = 0; i < (int)list.size(); i++) {
            const Choice& c = list[i];
            bool warn = (i == 6 && c.at == 1) || (i == 7 && c.at == 1);
            Element line = hbox({text(" " + pad(c.name, 16)), text("< " + pad(c.values[c.at], 12) + " >") | (warn ? tint(Color::Red) : bold)});
            if (i == option_row_) line = line | inverted;
            b.push_back(line);
        }
        b.push_back(text(""));
        b.push_back(paragraph(list[option_row_].note) | dim);
        b.push_back(text(cut("left and right change it   enter or esc close", inner)) | dim);
        return window_of("Options for the next syncs", std::move(b), std::min(ow, 60));
    }

    Element places_box(int ow, int oh, int inner) {
        Elements b;
        b.push_back(text("Looking for films in:") | dim);
        int rows = std::max(1, oh - 8);
        for (int i = 0; i < (int)s_.roots.size() && i < rows; i++) {
            Element line = text(" " + pad(cut_front(shown(s_.roots[i]), inner - 2), inner - 1));
            if (i == place_row_) line = line | inverted;
            b.push_back(line);
        }
        b.push_back(text(""));
        b.push_back(text("Network shares: " + std::string(s_.config.network ? "looked in" : "left out")));
        b.push_back(text(cut_front("Settings: " + s_.config_file, inner)) | dim);
        b.push_back(text(cut("a add   d remove   n network shares   esc close", inner)) | dim);
        return window_of("Places", std::move(b), ow);
    }

    Element help(int ow) {
        std::vector<std::pair<std::string, std::string>> keys = {
            {"up down j k", "move, PgUp PgDn g G for pages and ends"},
            {"enter", "sync what is marked, or the line under the cursor"},
            {"space", "mark a subtitle, or every subtitle of a film"},
            {"a", "sync every sure pair in the list that is not done"},
            {"m", "pair by hand: pick the film or the subtitle"},
            {"c  x", "say yes or no to a pair marked with ?"},
            {"u", "put the .bak back"},
            {"s", "move by a number of ms"},
            {"d  o", "dry run on or off, and the other options"},
            {"/", "find any film or subtitle by name"},
            {"f", "show all, to do, questions or films without subs"},
            {"p", "the folders it looks in"},
            {"r", "look through the folders again"},
            {"tab", "the library or the queue"},
            {"x  X  C", "in the queue: stop one, stop all, clear the done"},
            {"q", "quit"},
        };
        Elements b;
        for (auto& [k, what] : keys) b.push_back(hbox({text(pad(k, 13)) | bold, text(what)}));
        b.push_back(text(""));
        b.push_back(paragraph("A ? beside a pair means lapse-tui is not sure the subtitle belongs to that film. "
                              "Those are left out of a, and synced when you ask for them by name.") | dim);
        return window_of("Keys", std::move(b), std::min(ow, 70));
    }
};

}

int run_app(Setup setup) {
    auto screen = ScreenInteractive::Fullscreen();
    screen.ForceHandleCtrlC(false);
    App app(std::move(setup), screen);
    std::signal(SIGHUP, on_hangup);
    app.start();
    screen.Loop(app.component());
    return app.finish();
}
