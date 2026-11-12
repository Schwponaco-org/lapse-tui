# lapse-tui

lapse-tui is a terminal program that finds the films and subtitles on a machine, works out which subtitle belongs to which film, and syncs them with [LAPSE](https://github.com/Schwponaco-org/lapse). It is made for a server you reach over SSH and for people who live in a terminal, and it runs the `lapse` binary you already have rather than carrying an engine of its own.

```
 lapse-tui   Library   Queue                                        lapse 2.3.3
 All To do Questions No subtitles                         8 videos, 9 subtitles
────────────────────────────────────────────────────────────────────────────────
 Alien (1979)                                                             1 sub
    en  English.srt                                   95%  solid -4200 ms
 Blade Runner (1982)                                        1080p 1 of 2  1 sub
    en  Blade.Runner.1982.en.srt                      70%?
 Blade Runner (1982)                                      2160p 2 of 2  no subs
 Heat (1995)                                                             2 subs
    da  Heat.1995.1080p.BluRay.x264-GRP.da.srt       100%  solid +1500 ms
    en  Heat.1995.1080p.BluRay.x264-GRP.en.srt       100%  solid -4200 ms
 Scene.Film.2010.720p.WEB-DL-GRP                                          1 sub
    en  2_English.srt                                 95%  solid -4200 ms
 Show.S01E01                                                              1 sub
    en  Show.S01E01.en.srt                           100%
 Show.S01E02                                                              1 sub
    en  Show.S01E02.en.srt                           100%
 The Matrix (1999)                                                        1 sub
    en  The.Matrix.1999.en.srt                        80%?
────────────────────────────────────────────────────────────────────────────────
 /srv/media/Films/Blade Runner (1982)/Blade.Runner.1982.en.srt
 Pairs with Blade.Runner.1982.1080p.mkv, 70%, fits more than one video as well
 Not synced yet
 c yes  x no  enter sync  space mark  a sync all  m pair  / find  f filter
```

## Install

It needs `lapse` 2.3 or later, either on `PATH` or named in the config file. Get it from the [LAPSE releases](https://github.com/Schwponaco-org/lapse/releases).

lapse-tui itself is built with CMake and a C++20 compiler. FTXUI, the one library it uses, is downloaded during the build when the system does not have it.

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

For a server, build one file with nothing to install beside it and copy it next to `lapse`:

```bash
cmake -B build -DLAPSE_TUI_STATIC=ON
cmake --build build -j
scp build/lapse-tui server:~/.local/bin/
```

`-DLAPSE_TUI_STATIC=ON` links everything in on Linux. On macOS it does nothing, the system libraries are always there. Each CI run also keeps the Linux binary it built as an artifact.

Linux is what it is used on. macOS builds and passes the same tests in CI but has not been used by hand, so take it as experimental. Windows is not supported.

## Running it

```bash
lapse-tui                    # look in the usual places
lapse-tui /srv/media/Films   # look only here, this time
lapse-tui --scan             # look, print what was found, and stop
```

It draws on the alternate screen and leaves the terminal as it was. It works with a plain `TERM` such as `xterm` or `screen`, inside tmux and screen, and without a mouse. It only draws when something changes, so an idle screen sends nothing over the link. It uses the sixteen colours of your terminal's own theme, so it reads on light and dark backgrounds alike, and with `NO_COLOR` set it uses bold and dim and nothing else.

Syncs run one at a time, each a `lapse` of its own. If the SSH connection drops with syncs still queued, lapse-tui keeps going without the screen until the queue is empty, saves what lapse said, and exits. This is experimental; for a long queue, start lapse-tui inside tmux or screen and detach instead.

## Keys

The keys for what is under the cursor are always shown on the bottom line, and `?` lists them all.

| Key | |
| --- | --- |
| `↑` `↓` `j` `k`, `PgUp` `PgDn`, `g` `G` | Move |
| `Enter` | Sync what is marked, or the subtitle under the cursor, or every subtitle of the film under it |
| `Space` | Mark a subtitle, or every subtitle of a film |
| `a` | Sync every sure pair in the list that is not done yet, after a yes |
| `/` | Find any film or subtitle by name as you type |
| `m` | Pair by hand: pick the film for a subtitle, or a subtitle for a film. `Tab` there browses the disk |
| `c` `x` | Say yes or no to a pair marked with `?` |
| `u` | Put the `.bak` back |
| `s` | Move by a number of milliseconds |
| `d` | Dry run on or off |
| `o` | Options: mode, audio track, subtitle track, confidence, snap, dry run, backup, force |
| `f` | Show all, what is still to do, the questions, or the films without subtitles |
| `p` | The folders it looks in |
| `r` | Look through the folders again |
| `Tab` | The library or the queue |
| `x` `X` `C` | In the queue: stop one, stop all, clear the finished |
| `q` | Quit |

The mouse picks lines, scrolls, and switches between the library and the queue.

lapse keeps a `.bak` of every subtitle it writes over, and `u` puts it back. Turning the backup off in the options takes a yes, and `NO BACKUP` stays in the header for as long as it is off. `DRY RUN` and `FORCE` show there the same way. The options are not saved, every start begins with lapse's defaults.

The queue shows how long the current sync has run, not how far it is. lapse does not report its progress when it answers in JSON.

## How it finds films

Without folders on the command line or in the config file, it looks in your home folder, in `/mnt`, `/media`, `/srv`, `/run/media` and `/Volumes`, and in every disk mounted outside the system folders. Network shares (NFS, SMB, SSHFS and the like) are left out, also when they are mounted inside one of those folders, unless you turn them on with `n` in the places list or `network = yes` in the config file. Hidden folders, NAS folders like `@eaDir` and `#recycle`, and folders behind a symlink are not looked in.

The first look happens in the background while you can already use the screen, and the films show up as they are found. What it found is saved, and at the next start the list is there at once. It then looks again, but only reads a folder whose time has changed, which is a fraction of a second for a few thousand folders.

## How matching works

Every file name is read the way release names are written: title, year, season and episode (`S01E02`, `1x02`), resolution, source, edition and group, and for a subtitle the language and the forced and SDH tags at the end (`.en`, `.eng.forced`, `.Danish.SDH`). When the name says little, like `movie.mkv` or `Subs/2_English.srt`, the folder it is in fills in the title and year.

Each subtitle then gets the video it most likely belongs to, with a score:

- named after the video, or in `Subs/<video name>/`: 100%
- the same episode in the same folder: 95%
- the only video in its folder, or in the folder above `Subs/`: 90%
- a name like the video's in the same folder: up to 95%
- the same title and year somewhere else on the disk: at most 80%

From 85% a pair is sure. Below that it is a question, shown with a `?`, and left out of `a`. A subtitle that fits two versions of a film equally well is held at 70% so you are asked rather than given a guess. A different episode or year rules a video out, and below 50% a subtitle is listed under "Subtitles with no film". A film can have any number of subtitles, and versions of the same film are shown as "1 of 2" and so on.

What you decide by hand with `m`, `c` and `x` is kept and wins over all of the above.

## Files

| | Linux | macOS |
| --- | --- | --- |
| Settings | `$XDG_CONFIG_HOME/lapse-tui/config` | `~/Library/Application Support/lapse-tui/config` |
| Pairs made by hand and the last results | `$XDG_STATE_HOME/lapse-tui/state` | `~/Library/Application Support/lapse-tui/state` |
| The index of folders | `$XDG_CACHE_HOME/lapse-tui/index` | `~/Library/Caches/lapse-tui/index` |

Without the XDG variables these are under `~/.config`, `~/.local/state` and `~/.cache`. The config file is a few lines, and lapse-tui writes it again when you change the places from inside:

```
root = /srv/media/Films
root = /srv/media/Shows
network = no
engine = /opt/lapse/lapse
```

The index can be deleted at any time; it is built again on the next start.

## Tests

```bash
ctest --test-dir build                                    # names, matching, the index, the queue
tests/drive-tui.sh build/lapse-tui /path/to/lapse         # the screen, driven in tmux
```

`drive-tui.sh` needs tmux, espeak-ng and ffmpeg. It builds a small library of films with real speech in them and 3000 fake ones, and goes through finding, syncing, undoing, pairing by hand, resizing and quitting the way a person would, checking the screen and the files on disk as it goes.

## Licence

GPL-3.0, the same as LAPSE. See [LICENSE](LICENSE).
