# Euchre

Four-handed partnership Euchre for one player: you and a computer
partner against two computer opponents, each a character with a level
and a style, from a sloppy beginner to an expert that samples the hidden
cards and solves every deal exactly. It saves after every move, so close
it any time, or pull the plug, and it picks up exactly where you were.

The table lays itself out for wide windows and tall ones alike, from a
phone held upright to an ultrawide monitor, so the same code can become
a web app (PWA) later. Everything is drawn as shapes, and sound effects
are synthesized at startup: the only assets are an embedded font.

See [docs/rules.md](docs/rules.md) for the rules as played, the options,
and how each level plays.

## Build

Requires CMake 3.24 or later and a C++20 compiler with `#embed` (GCC 15,
Clang 19). SDL3 and stb_truetype are fetched over HTTPS, pinned by
SHA256, and SDL3 is linked statically.

    $ cmake -B build
    $ cmake --build build
    $ ./build/euchre

Tests, and tools for measuring the computer players:

    $ cmake -B build -DCMAKE_BUILD_TYPE=Debug
    $ cmake --build build
    $ ctest --test-dir build
    $ ./build/euchre_test --duel 0 3 2000   # Ruth vs Bev, duplicate deals
    $ ./build/euchre_test --bench           # time expert decisions

The save loader has an AFL++ harness, `src/main_fuzz.cpp`; its header
says how to build and run it.

## Playing

- Bid with the buttons above your hand; toggle **Alone** before bidding
  to play without your partner.
- Tap or click a card to play it. Cards you can't play are dimmed.
- **Hint** asks the expert what it would do. Tap the trump panel to see
  the last trick, and tap the table to hurry a finished trick along.
- The menu (top right) has a new game, the players, the rules, sound,
  speed, help and statistics.
- Keys: `1`-`6` play or discard a card, `P` pass, `O` order up, `A`
  alone, `C` `S` `D` `H` name a suit, `?` hint, `M` sound, `Esc` menu,
  `Enter` continue.

Developer options: `--save FILE`, `--seed N`, `--autoplay`, and
`--shot WxH[@SCALE] FILE` to render a BMP headlessly once the table
settles, with `--until PHASE` and `--open PANEL` (see `src/main_sdl.cpp`).
`--icon assets/euchre.ico` regenerates the Windows icon.

## Code

A unity build in the style of [ssg][ssg]: `src/euchre.cpp` includes the
portable core, which uses no C++ standard library, and each platform
layer (`main_sdl.cpp`, `main_test.cpp`, `main_fuzz.cpp`) includes it and
implements its small `os_*` API. All memory comes from arenas carved out
of one block at startup.

| File | Responsibility |
|------|----------------|
| `src/base.cpp`   | Types, arenas, strings, buffers, numbers |
| `src/cards.cpp`  | The pack, bowers, trick power, notation |
| `src/rules.cpp`  | Game state, moves, scoring, validation |
| `src/ai.cpp`     | Characters, heuristics, the double-dummy expert |
| `src/save.cpp`   | The save file |
| `src/sound.cpp`  | Synthesized effects |
| `src/geom.cpp`   | Antialiased triangle mesh, suit shapes |
| `src/font.cpp`   | Text, baked by stb_truetype at the sizes in use |
| `src/art.cpp`    | Cards, felt, badges, buttons |
| `src/layout.cpp` | Wide and tall arrangements |
| `src/ui.cpp`     | App state, flow, pacing, input |
| `src/scene.cpp`  | Sprites and the table |
| `src/panels.cpp` | Summary, overlays, the frame |

## Licenses

This program is released into the public domain (see `UNLICENSE`). It
bundles SDL3 (zlib license), stb_truetype (public domain), and a subset
of Inter Bold (SIL Open Font License 1.1, `assets/Inter-LICENSE.txt`).

[ssg]: https://github.com/skeeto/skeeto.github.com/tree/master/_ssg
