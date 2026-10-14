# KShadowLrc — agent guidelines

A floating desktop-lyrics overlay for KDE Plasma 6 on Wayland: C++23, Qt 6, KDE
Frameworks 6, LayerShellQt, QML.

It is a **wlr-layer-shell** surface. That is the whole point of the project: the
overlay stays above other windows, out of the taskbar and click-through **with
no manual window rules**. Anything that pushes the window back to being a plain
`xdg-toplevel` is a regression, whatever it buys elsewhere.

`RefRepo/` holds read-only reference code (Krema for the layer-shell recipe,
Waylyrics for the lyrics pipeline). It is gitignored; never modify it, never
build against it.

## Build and test

```
cmake -B build -G Ninja
cmake --build build
ctest --test-dir build -V
```

- When several agents work in parallel, **each uses its own build directory**
  (`build-<lane>`) so nobody clobbers anybody's configure step.
- Every `tests/*.cpp` becomes its own CTest target automatically — drop the file
  in, nothing else to wire. Tests must be **deterministic and offline**: never
  touch the network. To test parsing, call the same parser with a saved fixture.
- If a build error comes from a path outside your assignment, a sibling lane is
  mid-write. Wait and rebuild; do not "fix" their file, and do not loop on it.

## Hard rule: never drive the desktop

Do **not** perform GUI automation of any kind, even to "prove" your own work:

- no screenshots (`spectacle`, `grim`, …), no capturing and cropping;
- no synthetic input (`ydotool`, `wtype`, `xdotool`, …), no simulated clicks or
  keystrokes — they land in whatever window happens to have focus, including the
  user's own editor;
- no KWin scripting, no KWin debug console, no window manipulation or cursor
  probing.

Visual verification — *is the overlay actually visible, on top, click-through,
absent from the taskbar* — belongs to the **user**, who can see the screen. Ask
for it and report what they say. "I could not prove this without looking at the
screen, so I am handing it over" is a **correct, expected outcome**, not a
failure. Do not burn turns inventing a way around it.

Prove behaviour with logs, tests, and queries against your own objects instead:
log the resolved value, assert in a CTest, read your own D-Bus objects. To make
click-through testable, assert on the input region your code computes — do not
click something and see if it reacts.

Launching the app to read its own log output is fine. Manipulating the desktop
around it is not.

## Frozen interfaces

These are contracts other lanes compile against. Do not change a class shape,
method signature or signal name without the parent's explicit sign-off:

- `src/lyrics/lyrictypes.h` — `LyricLine`, `Lyric`, `LyricKind`, `SongInfo`,
  `LyricStore`, `TrackMeta`
- `src/lyrics/provider.h` — `LyricProvider` (every call is async, carries a
  caller-supplied `requestId` that the result signals echo back, and emits
  exactly one result signal per call)
- `src/player/playertracker.h` — `PlayerTracker`, `PositionAnchor`

## Conventions

- Every file starts with
  `// SPDX-License-Identifier: GPL-3.0-or-later` and
  `// SPDX-FileCopyrightText: 2026 KShadowLrc Contributors`.
- One `Q_LOGGING_CATEGORY(lcX, "kshadowlrc.<area>")` per `.cpp`, declared in
  `src/kshadowlrc.h` when shared across translation units.
- The build defines `QT_NO_KEYWORDS`: use `Q_EMIT` / `Q_SIGNALS` / `Q_SLOTS`,
  never `emit` / `signals` / `slots`.
- `.clang-format` comes from ECM (WebKit/KDE style) and the tree is **clean under
  it**: run `clang-format -i` on every file you add or change and do not
  hand-tune the style. KDE style breaks the brace after a namespace
  (`namespace kshadowlrc` / `{` on the next line) and writes `} // namespace x`
  with a single space — copy that, not whatever a neighbouring file happens to
  do. The pre-commit hook needs the `git clang-format` script on `PATH`, which
  is not installed on every machine; when the hook refuses to run that is an
  environment problem, not a formatting one, so check your files with
  `clang-format -i` directly rather than committing around it.
- No new third-party dependency without the parent's sign-off. Prefer what is
  already linked over writing a helper.
- `src/qml/main.qml` is bound to the lyrics controller; change the controller to
  suit the QML rather than editing the QML out from under another lane.

## Layout

```
src/main.cpp                 thin executable
src/kshadowlrc.h             shared logging categories
src/app/                     Application: KAboutData, i18n, single instance,
                             the QT_WAYLAND_SHELL_INTEGRATION env handling
src/platform/                OverlayWindow: the whole layer-shell recipe
src/player/                  PlayerTracker: MPRIS over QtDBus
src/lyrics/                  LRC parsing, matching, cache, engine, controller
src/lyrics/providers/        NetEase, LRCLib
src/qml/                     the overlay's QML
tests/                       one executable + one CTest per .cpp
```
