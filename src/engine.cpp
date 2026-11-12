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

#include "engine.h"

#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <sstream>

#include <fcntl.h>
#include <poll.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

namespace {

std::string on_path(const std::string& name) {
    const char* path = std::getenv("PATH");
    std::istringstream dirs(path ? path : "");
    std::string dir;
    while (std::getline(dirs, dir, ':')) {
        std::string full = (dir.empty() ? "." : dir) + "/" + name;
        if (access(full.c_str(), X_OK) == 0) return full;
    }
    return "";
}

std::string first_line(const std::string& s) {
    return s.substr(0, s.find('\n'));
}

}

Engine find_engine(const std::string& configured) {
    Engine e;
    std::string path = configured.empty() ? on_path("lapse") : configured;
    if (path.empty()) return e;

    Ran version = run_engine(path, {"--version"});
    if (version.code != 0) return e;
    e.path = path;
    e.version = first_line(version.out);

    std::istringstream formats(run_engine(path, {"--formats"}).out);
    std::string ext;
    while (formats >> ext) e.formats.insert(ext);

    // with nothing to do it prints the usage, which names every option it has
    Ran usage = run_engine(path, {});
    e.shift = (usage.out + usage.err).find("--shift") != std::string::npos;
    return e;
}

Ran run_engine(const std::string& binary, const std::vector<std::string>& args, const std::atomic<bool>* stop) {
    Ran ran;
    int out[2], err[2];
    if (pipe(out) != 0) return ran;
    if (pipe(err) != 0) {
        close(out[0]);
        close(out[1]);
        return ran;
    }

    std::vector<char*> argv;
    argv.push_back(const_cast<char*>(binary.c_str()));
    for (auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
    argv.push_back(nullptr);

    pid_t pid = fork();
    if (pid == 0) {
        // its own process group, so a hangup or ^C meant for the interface
        // does not cut a subtitle off half written
        setpgid(0, 0);
        int null = open("/dev/null", O_RDONLY);
        dup2(null, 0);
        dup2(out[1], 1);
        dup2(err[1], 2);
        execve(binary.c_str(), argv.data(), environ);
        _exit(127);
    }
    close(out[1]);
    close(err[1]);
    if (pid < 0) {
        close(out[0]);
        close(err[0]);
        return ran;
    }

    pollfd fds[2] = {{out[0], POLLIN, 0}, {err[0], POLLIN, 0}};
    std::string* into[2] = {&ran.out, &ran.err};
    int open_fds = 2;
    bool killed = false;
    while (open_fds > 0) {
        if (stop && *stop && !killed) {
            kill(pid, SIGTERM);
            killed = true;
        }
        if (poll(fds, 2, 100) < 0 && errno != EINTR) break;
        for (int i = 0; i < 2; i++) {
            if (fds[i].fd < 0 || !(fds[i].revents & (POLLIN | POLLHUP | POLLERR))) continue;
            char buf[4096];
            ssize_t n = read(fds[i].fd, buf, sizeof(buf));
            if (n > 0) {
                into[i]->append(buf, (size_t)n);
            } else if (n == 0 || errno != EINTR) {
                close(fds[i].fd);
                fds[i].fd = -1;
                open_fds--;
            }
        }
    }
    for (auto& f : fds)
        if (f.fd >= 0) close(f.fd);

    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
    ran.code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    return ran;
}


namespace {

struct Reader {
    const std::string& s;
    size_t i = 0;

    void skip() {
        while (i < s.size() && std::strchr(" \t\r\n", s[i])) i++;
    }

    bool eat(char c) {
        skip();
        if (i < s.size() && s[i] == c) {
            i++;
            return true;
        }
        return false;
    }

    static void put_utf8(std::string& out, unsigned code) {
        if (code < 0x80) {
            out += (char)code;
        } else if (code < 0x800) {
            out += (char)(0xC0 | (code >> 6));
            out += (char)(0x80 | (code & 0x3F));
        } else {
            out += (char)(0xE0 | (code >> 12));
            out += (char)(0x80 | ((code >> 6) & 0x3F));
            out += (char)(0x80 | (code & 0x3F));
        }
    }

    bool text(std::string& out) {
        if (!eat('"')) return false;
        while (i < s.size() && s[i] != '"') {
            char c = s[i++];
            if (c != '\\') {
                out += c;
                continue;
            }
            if (i >= s.size()) return false;
            char e = s[i++];
            if (e == 'n') out += '\n';
            else if (e == 't') out += '\t';
            else if (e == 'u' && i + 4 <= s.size()) {
                put_utf8(out, (unsigned)std::strtoul(s.substr(i, 4).c_str(), nullptr, 16));
                i += 4;
            } else out += e;
        }
        return eat('"');
    }

    bool number(double& v) {
        skip();
        const char* start = s.c_str() + i;
        char* end = nullptr;
        v = std::strtod(start, &end);
        if (end == start) return false;
        i += (size_t)(end - start);
        return true;
    }

    bool word(const char* w) {
        skip();
        size_t n = std::strlen(w);
        if (s.compare(i, n, w) != 0) return false;
        i += n;
        return true;
    }
};

}

bool read_json(const std::string& line, Json& out) {
    Reader r{line};
    if (!r.eat('{')) return false;
    if (r.eat('}')) return true;
    while (true) {
        std::string key;
        if (!r.text(key) || !r.eat(':')) return false;
        r.skip();
        char c = r.i < line.size() ? line[r.i] : 0;
        if (c == '"') {
            if (!r.text(out.text[key])) return false;
        } else if (c == '[') {
            r.i++;
            auto& list = out.list[key];
            while (!r.eat(']')) {
                double v;
                if (!r.number(v)) return false;
                list.push_back(v);
                r.eat(',');
            }
        } else if (r.word("true")) {
            out.flag[key] = true;
        } else if (r.word("false")) {
            out.flag[key] = false;
        } else if (r.word("null")) {
        } else {
            double v;
            if (!r.number(v)) return false;
            out.number[key] = v;
        }
        if (r.eat(',')) continue;
        return r.eat('}');
    }
}
