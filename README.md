<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: 2026 KShadowLrc Contributors -->

# KShadowLrc

Desktop lyrics for KDE Plasma 6 on Wayland: the line that is playing, and the
ones coming up, drawn on a floating overlay. The overlay is a
wlr-layer-shell surface, which is what keeps it above the other windows, out
of the taskbar and click-through — with no window rule and no KWin script to
set up or to lose.

The track comes from the desktop's own media players, so anything that speaks
MPRIS works: the browser tab playing a music video, the local player, the one
in the background.

## What it does

- Follows the playing track over MPRIS — title, artist, album, length and the
  playback position — through play, pause, stop, seek and a player coming or
  going.
- Looks lyrics up by title, artist and duration through NetEase Cloud Music
  and LRCLIB, and caches what it finds so a track played once loads without a
  lookup.
- Uses the file's own lyrics when it has them — a `.lrc` beside the audio file,
  or the lyrics written into its tag. Where that source sits among the online
  ones is yours to set: see [Lyrics on your own disk](#lyrics-on-your-own-disk).
- Shows the current line, its translation when the source has one, and the
  lines after it, dimmed.
- Follows the configuration while it runs: font, size, colour and outline,
  how many upcoming lines to show, where it sits — which edge, which side of
  the screen, how far from it, or dragged somewhere by hand — click-through,
  which output to land on, the lyric offset, and which lyric sources to ask,
  in which order.
- Says what the lyrics are doing whenever there are none: the tray tooltip and
  the settings window tell "still looking" apart from "none for this song" and
  from "the lookup failed".

## Requirements

- KDE Plasma 6 on Wayland. On X11, or on a compositor without wlr-layer-shell,
  this is not the program you are looking for.
- Qt 6.8 or later, KDE Frameworks 6, Kirigami and LayerShellQt. The two
  minimum versions are `QT_MIN_VERSION` and `KF6_MIN_VERSION` in
  [CMakeLists.txt](CMakeLists.txt).
- Development packages for KGlobalAccel and StatusNotifierItem for the tray
  icon and the global shortcuts.
- TagLib, for the lyrics written into an audio file's tag.
- Optional: KDBusAddons, which adds the single-instance guard. Without it the
  build is complete and a second copy would simply start.

Package names differ between distributions; under Fedora they are
`kf6-kglobalaccel-devel`, `kf6-kstatusnotifieritem-devel`,
`taglib-devel` and `kf6-kdbusaddons-devel`.

## Build

```
cmake -B build -G Ninja
cmake --build build
ctest --test-dir build
```

`cmake --install build` installs the binary, the desktop file, the AppStream
metainfo and the icon.

## Run

```
./build/bin/kshadowlrc
```

- `--output NAME` places the overlay on that Wayland output (`HDMI-A-1`, …) at
  startup instead of the configured one. An unknown name falls back to the
  primary output. The settings window can move it while the app runs.
- `--demo` plays a built-in lyric instead of following the desktop, for when
  nothing is playing.
- `--settings` opens the settings window at startup. It is also in the tray
  menu.

## Lyrics on your own disk

A local file's own lyrics come from two places, in this order:

- `<file>.lrc` beside the audio file, with `<file>.zh.lrc` next to it when you
  have a translation. The audio file's extension is dropped, so `song.flac`
  reads `song.lrc`.
- The file's own tag, when there is no sidecar: the `LYRICS` comment of a FLAC
  or Ogg file, an ID3v2 `USLT` frame in an MP3, the `©lyr` atom of an M4A, the
  `UNSYNCEDLYRICS` value of an APE file.

A lyric with no timestamps in it is never shown: it cannot follow the music, so
it counts as no lyric at all, and the song is reported as one the lyrics were
not found for. A tag that puts the original and its translation on the same line
is shown the way it is written.

Where this source sits relative to the online ones is the `sourceOrder`
setting; the values it accepts and its default are in
[`src/config/kshadowlrcsettings.kcfg`](src/config/kshadowlrcsettings.kcfg),
and the settings window orders them for you.

## When the match is wrong

The source order and the candidates are a guess, and sometimes the guess is the
wrong song. The tray menu's **Search lyrics…** opens a small window to search by
hand: it starts from the playing track's title and artist, asks every enabled
source at once, and lists what each one returned, grouped by source in the order
you configured. Nothing is picked for you — that is the point of opening it.

Choosing an entry **pins** that lyric to the song. A pin outranks everything: the
cache and every source are skipped, so the lyric you chose is the lyric you get,
the next time the song plays and with the machine offline as well. The window
says where the pinned lyric came from and can forget it again, after which the
normal resolution takes over.

Pins live in `pins.json` under `QStandardPaths::AppDataLocation` — one per song,
in a file you can edit or delete by hand, and deliberately not in the cache,
which is disposable. A song whose player reports no name and no length has
nothing to pin a lyric to, and the window says so rather than failing quietly.
An entry whose lyric carries no timestamps is refused when you pick it, for the
same reason it would never be displayed: it cannot follow the music.

## Configuring

The tray icon is the way in when the overlay is click-through and out of the
taskbar: left click shows or hides the overlay, and the menu reopens the
settings window, opens the search window, adjusts where the overlay sits and
how large its text is, shifts the offset, or quits.

The settings window edits every key and writes each change immediately, so a
change reaches the running overlay as it is made. What can be set, with
defaults, ranges and the label each control shows, is declared in
[`src/config/kshadowlrcsettings.kcfg`](src/config/kshadowlrcsettings.kcfg) —
that file is the contract. It names the config file it reads and writes at the
top; it lands in KDE's standard config location.

The lyrics are cached under `QStandardPaths::CacheLocation`; see `cacheDir()`
in [src/lyrics/lyriccache.cpp](src/lyrics/lyriccache.cpp). The lyrics you pinned
yourself are not in there — they are a decision, not a copy, and they survive
clearing the cache.

### Moving and resizing it by hand

**Adjust position and size** in the tray menu lets you drag the overlay instead
of typing numbers: a frame and four corner handles appear, dragging the text
moves it, dragging a handle scales it, and `Esc` — or that same tray item —
finishes. Placement is anchored, so the text grows away from the edge it is
pinned to; releasing near the middle of the screen snaps it back to being
horizontally centred, which is the position that survives a font size change.

While the mode is on, the overlay takes the pointer and, only so that `Esc` can
reach it, the keyboard. That is the price of being click-through the rest of the
time, which is why the mode is entered and left deliberately. What a drag writes
is the very setting the settings window edits; nothing about the mode itself is
stored, so quitting while adjusting never leaves an overlay that swallows
clicks.

### Global shortcuts

Registered with KGlobalAccel, so they can be rebound in Plasma's *System
Settings → Shortcuts* and the new keys are kept:

| Shortcut | Action |
| --- | --- |
| `Meta`+`Alt`+`[` | shift the lyrics earlier |
| `Meta`+`Alt`+`]` | shift the lyrics later |
| `Meta`+`Alt`+`\` | reset the lyric offset |

One press moves the offset by one step, and the offset is held within a
maximum either way; both numbers are `OffsetStepMs` and `MaxOffsetMs` in
[src/system/controlservice.h](src/system/controlservice.h).

## How it works

- [`PlayerTracker`](src/player/playertracker.cpp) watches the session bus for
  `org.mpris.MediaPlayer2.*` and reports the track, its status and the
  playback position. Players can be ignored by name; the rule an entry follows
  is in [src/player/playermatch.h](src/player/playermatch.h).
- [`LyricEngine`](src/lyrics/lyricengine.cpp) takes a track and walks the lyric
  sources in the configured order, stopping at the first one that has
  something, then picks the timestamped lines to show.
- [`LocalFiles`](src/lyrics/localsource.cpp) is the source that reads a sidecar
  or an embedded tag;
  [src/lyrics/localtags.cpp](src/lyrics/localtags.cpp) is the only file that
  includes TagLib.
- Providers implement [`LyricProvider`](src/lyrics/provider.h) and live in
  [src/lyrics/providers/](src/lyrics/providers/); every call is asynchronous.
- [`LyricSearch`](src/lyrics/lyricsearch.cpp) fans a manual query out to the
  enabled providers at once and groups the answers by provider — in the
  configured order, never in the order they arrive.
- [`pinstore`](src/lyrics/pinstore.cpp) keeps the lyrics you chose yourself, one
  per song; the engine applies a matching one before it looks anywhere else.
- [`LyricsController`](src/lyrics/controller.cpp) is the QML bridge.
  [src/qml/main.qml](src/qml/main.qml) is the overlay's content and
  [src/platform/overlaywindow.cpp](src/platform/overlaywindow.cpp) is the
  layer-shell recipe, including the input region that makes it click-through.

## Tests

They use Qt Test: every `tests/*.cpp` is one test class and one CTest entry,
registered by ECM's `ecm_add_tests`. `ctest --test-dir build` runs them all and
`ctest --test-dir build -R <name> -V` runs one and prints every check.

They are deterministic and offline: the parsers are driven with saved fixtures,
and the tests that need a session bus start a private one, so nothing reaches
the network and nothing touches the session you are logged into. The tag tests
write their tags onto the small audio files committed under
[`tests/data/`](tests/data/), so they need no encoder either.

## License

GPL-3.0-or-later; the full text is in
[LICENSES/GPL-3.0-or-later.txt](LICENSES/GPL-3.0-or-later.txt).

Thanks are due to two projects whose approach shaped this one: Waylyrics for
the shape of the lyrics pipeline, and Krema for the layer-shell recipe. Both
were read as reference only and neither is part of this repository.
