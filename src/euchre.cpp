// euchre: four-handed partnership Euchre against computer players
//
// Unity build: a platform layer (main_sdl.cpp, main_test.cpp,
// main_fuzz.cpp) includes this file and implements the os_* API.
//   $ cmake -B build && cmake --build build && ctest --test-dir build
//
// This is free and unencumbered software released into the public domain.
#include "base.cpp"

#ifndef APP_VERSION
#  define APP_VERSION "dev"
#endif


// Platform API

struct Os;

// The last state given to os_save, or a null string if there is none.
static Str  os_load(Os *, Arena *);
// Replace the saved state atomically: written whole or not at all.
static b32  os_save(Os *, Arena scratch, Str state);
// Start a clip that sound.cpp synthesized, mixed over anything playing.
static void os_play(Os *, i32 sfx);
// Also: [[noreturn]] os_oom(), declared in base.cpp.


#include "cards.cpp"
#include "rules.cpp"
#include "ai.cpp"
#include "save.cpp"
#include "sound.cpp"
#include "geom.cpp"
#include "font.cpp"
#include "art.cpp"
#include "layout.cpp"
#include "ui.cpp"
#include "scene.cpp"
#include "panels.cpp"
