// Tests: a platform layer that saves to memory, the unit tests, and
// tools for measuring the computer players
// $ cmake --build build && ./build/euchre_test
//   euchre_test               run the unit tests
//   euchre_test --duel A B N  N duplicate deals, characters A+A vs B+B
//   euchre_test --bench       time expert decisions
//   euchre_test --frames W H SCALE SECONDS
//                             time drawing an autoplayed game at 60 Hz
// This is free and unencumbered software released into the public domain.
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "euchre.cpp"

struct Os {
    Arena mem;
    Str   saved;
    i32   saves;
};

[[noreturn]] static void os_oom() { __builtin_trap(); }

static Str os_load(Os *os, Arena *a)
{
    return os->saved.data ? clone(a, os->saved) : Str{};
}

static b32 os_save(Os *os, Arena, Str state)
{
    Arena keep = os->mem;
    os->saved = clone(&keep, state);
    os->saves++;
    return 1;
}

static void os_play(Os *, i32) {}

struct Test {
    Arena perm;
    i32   run;
    i32   failed;
};

static void printstr(FILE *f, Str s)
{
    fwrite(s.data, 1, (uz)s.len, f);
}

// Compare strings, printing both on mismatch.
static b32 expect(Test *t, Str name, Str got, Str want)
{
    t->run++;
    if (got == want) {
        return 1;
    }
    t->failed++;
    fprintf(stderr, "FAIL: ");
    printstr(stderr, name);
    fprintf(stderr, "\n  got:  [");
    printstr(stderr, got);
    fprintf(stderr, "]\n  want: [");
    printstr(stderr, want);
    fprintf(stderr, "]\n");
    return 0;
}

static Str istr(Arena *a, i64 v)
{
    Buf b(a, 24);
    print(&b, v);
    return finish(&b);
}

static Str yes(b32 v)
{
    return v ? Str("yes") : Str("no");
}

#include "test/rules_test.cpp"
#include "test/save_test.cpp"
#include "test/ai_test.cpp"
#include "test/layout_test.cpp"
#include "test/duel.cpp"
#include "test/frames.cpp"

int main(int argc, char **argv)
{
    iz    cap = (iz)1<<28;
    byte *mem = (byte *)malloc((uz)cap);
    Test  t   = {};
    t.perm = {mem, mem+cap};

    if (argc >= 2) {
        Str a1 = {(u8 *)argv[1], (iz)__builtin_strlen(argv[1])};
        if (a1=="--duel" && argc>=5) {
            return duel(t.perm, atoi(argv[2]), atoi(argv[3]), atoi(argv[4]));
        }
        if (a1 == "--bench") {
            return bench(t.perm);
        }
        if (a1=="--frames" && argc>=6) {
            return frames(t.perm, (f32)atof(argv[2]), (f32)atof(argv[3]),
                          (f32)atof(argv[4]), atoi(argv[5]));
        }
        if (a1=="--seeds" && argc>=3) {
            // Saves at many states, as a fuzzing corpus
            i32 n = 0;
            for (u64 seed = 1; seed <= 3; seed++) {
                Saved s = {};
                s.hasgame = 1;
                newgame(&s.game, {}, seed);
                for (i32 i = 0; s.game.phase!=PH_OVER && i<400; i++) {
                    if (i%9 == 0) {
                        Arena tmp = t.perm;
                        Str   txt = savetext(&tmp, &s);
                        char  path[512];
                        snprintf(path, sizeof(path), "%s/save%03d.txt", argv[2], n++);
                        FILE *f = fopen(path, "wb");
                        if (f) {
                            fwrite(txt.data, 1, (uz)txt.len, f);
                            fclose(f);
                        }
                    }
                    playmove(&s.game, t.perm);
                }
            }
            return 0;
        }
    }

    test_rules(&t);
    test_save(&t);
    test_ai(&t);
    test_layout(&t);

    fprintf(stderr, "%d tests, %d failed\n", t.run, t.failed);
    return t.failed ? 1 : 0;
}
