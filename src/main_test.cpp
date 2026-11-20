// Tests: a platform layer that saves to memory, the unit tests, and
// tools for measuring the computer players
// $ cmake --build build && ./build/euchre_test
//   euchre_test               run the unit tests
//   euchre_test --duel A B N  N duplicate deals, characters A+A vs B+B
//   euchre_test --bench       time expert decisions
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
    }

    test_rules(&t);
    test_save(&t);
    test_ai(&t);
    test_layout(&t);

    fprintf(stderr, "%d tests, %d failed\n", t.run, t.failed);
    return t.failed ? 1 : 0;
}
