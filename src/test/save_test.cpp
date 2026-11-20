// Saving tests: round trips, damaged files, and resuming mid-game

// Play a game by intermediate characters, one move per call.
static void playmove(Game *g, Arena ai)
{
    Move m = {MV_NEXT, 0, 0};
    if (g->turn >= 0) {
        m = decide(g, g->turn, characters + 3, ai);
    }
    apply(g, m);
}

static void test_save(Test *t)
{
    Arena a = t->perm;

    // Every state of a few games survives the trip through text
    {
        b32 ok = 1;
        i32 n  = 0;
        for (u64 seed = 1; seed<=4 && ok; seed++) {
            Saved s = {};
            s.hasgame = 1;
            s.table[1] = 3; s.table[2] = 0; s.table[3] = 4;
            Rules r = {};
            r.target = seed&1 ? 5 : 7;
            r.stick  = seed < 3;
            newgame(&s.game, r, seed);
            for (; s.game.phase!=PH_OVER && ok; n++) {
                Arena tmp  = a;
                Str   text = savetext(&tmp, &s);
                Saved back = loadtext(text);
                ok = back.hasgame && savetext(&tmp, &back)==text;
                if (!ok) {
                    printstr(stderr, text);
                }
                playmove(&s.game, tmp);
            }
        }
        expect(t, "save.roundtrip", yes(ok), "yes");
        expect(t, "save.states", yes(n > 200), "yes");
    }

    // Settings and stats come back; missing pieces take defaults
    {
        Saved s = {};
        s.set.rules.target = 7;
        s.set.rules.stick  = 0;
        s.set.speed        = 2;
        s.set.sound        = 0;
        s.set.seats[1]     = 6;
        s.stats.games      = 12;
        s.stats.lonewins   = 3;
        s.win = {10, 20, 900, 700, 1};
        Arena tmp  = a;
        Saved back = loadtext(savetext(&tmp, &s));
        Buf b(&tmp, 64);
        print(&b, (i64)back.set.rules.target);
        print(&b, (i64)back.set.rules.stick);
        print(&b, (i64)back.set.speed);
        print(&b, (i64)back.set.sound);
        print(&b, (i64)back.set.seats[1]);
        putbyte(&b, ' ');
        print(&b, (i64)back.stats.games);
        print(&b, (i64)back.stats.lonewins);
        putbyte(&b, ' ');
        print(&b, (i64)back.win.w);
        print(&b, (i64)back.win.max);
        print(&b, (i64)back.hasgame);
        expect(t, "save.settings", finish(&b), "70206 123 90010");
        Saved none = loadtext("");
        expect(t, "save.empty", istr(&tmp, none.set.rules.target), "10");
    }

    // Damage: every truncation, and random bytes changed, never load a
    // bad game; with the checksum fixed, validgame still guards.
    {
        Saved s = {};
        s.hasgame = 1;
        newgame(&s.game, {}, 77);
        for (i32 i = 0; i < 23; i++) {
            playmove(&s.game, a);
        }
        Arena tmp  = a;
        Str   text = savetext(&tmp, &s);
        b32   ok   = 1;
        for (iz n = 0; n < text.len; n++) {
            Saved back = loadtext(takehead(text, n));
            ok &= !back.hasgame || validgame(&back.game);
        }
        expect(t, "save.truncate", yes(ok), "yes");

        u64 rng = 3;
        i32 loaded = 0;
        for (i32 i = 0; i < 3000; i++) {
            Arena t2 = tmp;
            Str   cp = clone(&t2, text);
            iz    at = randint(&rng, (i32)cp.len);
            static u8 const junk[] = "0123456789-JQKAT9CSDH \nx";
            cp[at] = junk[randint(&rng, countof(junk)-1)];
            // Re-sign it, so the parser and validgame see the damage
            iz cut0 = cp.len - 1;
            for (; cut0>0 && !(cp[cut0-1]=='\n' && startswith(cuthead(cp, cut0), "check ")); cut0--) {}
            if (cut0 > 0) {
                Str body = takehead(cp, cut0);
                Buf b(&t2, 32);
                print(&b, body);
                print(&b, "check ");
                printhex(&b, hash(body), 16);
                putbyte(&b, '\n');
                cp = finish(&b);
            }
            Saved back = loadtext(cp);
            ok &= !back.hasgame || validgame(&back.game);
            loaded += back.hasgame;
        }
        expect(t, "save.corrupt", yes(ok), "yes");
        expect(t, "save.corrupt.some", yes(loaded > 0), "yes");
    }

    // A game reloaded after every move plays out exactly as one that
    // never stopped: the decisions are seeded from the state alone.
    {
        b32 same = 1;
        for (u64 seed = 10; seed < 13; seed++) {
            Game x, y;
            newgame(&x, {}, seed);
            y = x;
            for (i32 i = 0; i<3000 && x.phase!=PH_OVER; i++) {
                playmove(&x, a);
                Arena tmp = a;
                Saved s = {};
                s.hasgame = 1;
                s.game    = y;
                Saved back = loadtext(savetext(&tmp, &s));
                y = back.game;
                playmove(&y, tmp);
                Arena t1 = a, t2 = a;
                Saved sx = {}, sy = {};
                sx.hasgame = sy.hasgame = 1;
                sx.game = x;
                sy.game = y;
                if (!(savetext(&t1, &sx) == savetext(&t2, &sy))) {
                    same = 0;
                    break;
                }
            }
        }
        expect(t, "save.resume", yes(same), "yes");
    }
}
