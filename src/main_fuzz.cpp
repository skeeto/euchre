// Fuzz test: the save loader under AFL++, which must never crash and
// never accept a game that validgame would reject
// $ afl-clang-fast++ -std=c++20 -fno-exceptions -fno-rtti -g3 \
//       -fsanitize=address,undefined -fsanitize-trap=undefined \
//       -I build/_deps/stb-src -o fuzz src/main_fuzz.cpp
// $ mkdir i && ./build/euchre_test --seeds i   # or any saved games
// $ afl-fuzz -i i -o o ./fuzz
// This is free and unencumbered software released into the public domain.
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "euchre.cpp"

struct Os {};
static Str  os_load(Os *, Arena *) { return {}; }
static b32  os_save(Os *, Arena, Str) { return 1; }
static void os_play(Os *, i32) {}
[[noreturn]] static void os_oom() { __builtin_trap(); }

__AFL_FUZZ_INIT();

int main()
{
    __AFL_INIT();
    iz    cap = 1<<20;
    byte *mem = (byte *)malloc((uz)cap);
    u8   *buf = __AFL_FUZZ_TESTCASE_BUF;
    u8   *src = 0;
    while (__AFL_LOOP(10000)) {
        iz len = __AFL_FUZZ_TESTCASE_LEN;
        src = (u8 *)realloc(src, (uz)len + 1);
        memcpy(src, buf, (uz)len);
        Str text = {src, len};
        Saved s = loadtext(text);
        if (s.hasgame && !validgame(&s.game)) {
            __builtin_trap();
        }
        // Again with a good checksum, to reach the parser behind it
        Arena a = {mem, mem+cap};
        iz at = len;
        for (iz i = 0; i+6 <= len; i++) {
            if ((i==0 || src[i-1]=='\n') && !memcmp(src+i, "check ", 6)) {
                at = i;
            }
        }
        Buf b(&a, len + 64);
        print(&b, takehead(text, at));
        print(&b, "check ");
        printhex(&b, hash(takehead(text, at)), 16);
        putbyte(&b, '\n');
        s = loadtext(finish(&b));
        if (s.hasgame) {
            if (!validgame(&s.game)) {
                __builtin_trap();
            }
            // A loaded game must play on to its end without trouble
            Game *g = &s.game;
            for (i32 i = 0; i<400 && g->phase!=PH_OVER; i++) {
                Move ms[16];
                i32  n = legalmoves(g, ms);
                apply(g, ms[i % n]);
                if (!validgame(g)) {
                    __builtin_trap();
                }
            }
        }
    }
    return 0;
}
