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
- Shows the current line, its translation when the source has one, and the
  lines after it, dimmed.
- Follows the configuration while it runs: font, size, colour and outline,
  how many upcoming lines to show, which screen edge and how far from it,
  click-through, which output to land on, the lyric offset, and which
  providers to ask.

## Requirements

- KDE Plasma 6 on Wayland. On X11, or on a compositor without wlr-layer-shell,
  this is not the program you are looking for.
- Qt 6.8 or later, KDE Frameworks 6, Kirigami and LayerShellQt. The two
  minimum versions are `QT_MIN_VERSION` and `KF6_MIN_VERSION` in
  [CMakeLists.txt](CMakeLists.txt).
- Development packages for KGlobalAccel and StatusNotifierItem for the tray
  icon and the global shortcuts.
- Optional: KDBusAddons, which adds the single-instance guard. Without it the
  build is complete and a second copy would simply start.

Package names differ between distributions; under Fedora they are
`kf6-kglobalaccel-devel`, `kf6-kstatusnotifieritem-devel` and
`kf6-kdbusaddons-devel`.

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

- `--output NAME` places the overlay on that Wayland output (`HDMI-A-1`, …)
  instead of the configured one. An unknown name falls back to the primary
  output.
- `--demo` plays a built-in lyric instead of following the desktop, for when
  nothing is playing.
- `--settings` opens the settings window at startup. It is also in the tray
  menu.

## Configuring

The tray icon is the way in when the overlay is click-through and out of the
taskbar: left click shows or hides the overlay, and the menu reopens the
settings window, adjusts the offset, or quits.

The settings window edits every key and writes each change immediately, so a
change reaches the running overlay as it is made. What can be set, with
defaults, ranges and the label each control shows, is declared in
[`src/config/kshadowlrcsettings.kcfg`](src/config/kshadowlrcsettings.kcfg) —
that file is the contract. It names the config file it reads and writes at the
top; it lands in KDE's standard config location.

The lyrics are cached under `QStandardPaths::CacheLocation`; see `cacheDir()`
in [src/lyrics/lyriccache.cpp](src/lyrics/lyriccache.cpp).

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
- [`LyricEngine`](src/lyrics/lyricengine.cpp) takes a track, asks the enabled
  providers for it and picks the timestamped lines to show.
- Providers implement [`LyricProvider`](src/lyrics/provider.h) and live in
  [src/lyrics/providers/](src/lyrics/providers/); every call is asynchronous.
- [`LyricsController`](src/lyrics/controller.cpp) is the QML bridge.
  [src/qml/main.qml](src/qml/main.qml) is the overlay's content and
  [src/platform/overlaywindow.cpp](src/platform/overlaywindow.cpp) is the
  layer-shell recipe, including the input region that makes it click-through.

## Tests

`ctest --test-dir build` runs them. They are deterministic and offline: the
parsers are driven with saved fixtures, and the tests that need a session bus
start a private one, so nothing reaches the network and nothing touches the
session you are logged into.

## License

GPL-3.0-or-later; the full text is in
[LICENSES/GPL-3.0-or-later.txt](LICENSES/GPL-3.0-or-later.txt).

Thanks are due to two projects whose approach shaped this one: Waylyrics for
the shape of the lyrics pipeline, and Krema for the layer-shell recipe. Both
were read as reference only and neither is part of this repository.
