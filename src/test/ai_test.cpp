// Computer player tests: legal moves at every level, the solver
// against brute force, and dealing out unseen cards honestly

// Maker tricks still to come with best play, by plain minimax.
static i32 bruteforce(Pos *p)
{
    if (!p->n && !p->hand[p->turn]) {
        return 0;
    }
    u32 hand  = p->hand[p->turn];
    u32 legal = hand;
    if (p->n) {
        u32 f = hand & suitmask(p->led, p->trump);
        legal = f ? f : hand;
    }
    b32 maxer = (p->turn & 1) == p->mteam;
    i32 best  = maxer ? -1 : 99;
    for (u32 m = legal; m; m &= m-1) {
        Pos q = *p;
        b32 done;
        i32 got = dplay(&q, lowest(m), &done);
        i32 v   = got + bruteforce(&q);
        best = maxer ? maxi(best, v) : mini(best, v);
    }
    return best;
}

static i32 solved(Solver *sv, Pos *p)
{
    i32 k = 0;
    for (; k<5 && reach(sv, p, k+1); k++) {}
    return k;
}

static void test_ai(Test *t)
{
    Arena a = t->perm;

    // Whole games at every level, each move legal and each state valid
    {
        i32 chars[] = {6, 3, 0};  // Gus, Bev, Ruth
        b32 ok = 1;
        for (i32 level = 0; level<3 && ok; level++) {
            for (u64 seed = 1; seed<=(level==2 ? 1u : 6u) && ok; seed++) {
                Game g;
                Rules r = {};
                r.target = level==2 ? 5 : 10;
                r.stick  = seed & 1;
                newgame(&g, r, seed*101 + (u64)level);
                for (i32 i = 0; i<4000 && g.phase!=PH_OVER && ok; i++) {
                    Move m = {MV_NEXT, 0, 0};
                    if (g.turn >= 0) {
                        m = decide(&g, g.turn, characters + chars[level], a);
                    }
                    ok = legal(&g, m);
                    if (ok) {
                        apply(&g, m);
                        ok = validgame(&g);
                    }
                }
                ok &= g.phase == PH_OVER;
            }
        }
        expect(t, "ai.legal", yes(ok), "yes");
    }

    // The solver agrees with brute force on random endings
    {
        Solver *sv  = newsolver(&a);
        u64     rng = 9;
        b32     ok  = 1;
        for (i32 i = 0; i<400 && ok; i++) {
            i32 deck[NCARDS];
            for (i32 c = 0; c < NCARDS; c++) deck[c] = c;
            for (i32 c = NCARDS-1; c > 0; c--) {
                i32 j = randint(&rng, c+1);
                i32 x = deck[c]; deck[c] = deck[j]; deck[j] = x;
            }
            i32 each  = 1 + randint(&rng, 4);
            b32 alone = randint(&rng, 4) == 0;
            Pos p = {};
            p.trump = randint(&rng, 4);
            p.mteam = randint(&rng, 2);
            p.out   = alone ? (p.mteam ^ 2) : -1;  // a maker's partner
            p.per   = alone ? 3 : 4;
            p.led   = NOSUIT;
            i32 k = 0;
            for (i32 s = 0; s < 4; s++) {
                if (s == p.out) continue;
                for (i32 j = 0; j < each; j++) p.hand[s] |= bit(deck[k++]);
            }
            p.turn = randint(&rng, 4);
            if (p.turn == p.out) p.turn = dnext(&p, p.turn);
            // Sometimes start mid-trick
            if (randint(&rng, 2) && each > 1) {
                i32 plays = 1 + randint(&rng, p.per-1);
                for (i32 j = 0; j < plays; j++) {
                    i32 cs[8];
                    i32 nm = dmoves(&p, cs);
                    b32 done;
                    dplay(&p, cs[randint(&rng, nm)], &done);
                }
                // Keep whole tricks: unequal hands need the rest played
                // by the solver too, which it is.
            }
            Pos q = p;
            i32 want = bruteforce(&q);
            q = p;
            ok = solved(sv, &q) == want;
        }
        expect(t, "ai.solver", yes(ok), "yes");
    }

    // Deals of the unseen cards keep every known card and every void
    {
        b32 ok  = 1;
        u64 rng = 21;
        for (u64 seed = 1; seed<=30 && ok; seed++) {
            Game g;
            newgame(&g, {}, seed);
            i32 stop = randint(&rng, 16);
            for (i32 i = 0; i<stop || g.phase!=PH_PLAY; i++) {
                if (g.phase == PH_SCORE) break;
                playmove(&g, a);
            }
            if (g.phase != PH_PLAY) continue;
            for (i32 me = 0; me<4 && ok; me++) {
                if (me == sittingout(&g)) continue;
                View_ v = seatview(&g, me);
                for (i32 k = 0; k<40 && ok; k++) {
                    Deal d;
                    ok = sampledeal(&g, &v, &rng, &d, 1);
                    if (!ok) break;
                    ok = d.hand[me]==g.hand[me];
                    u32 all = d.kitty | playedmask(&g);
                    for (i32 s = 0; s < 4; s++) {
                        ok &= popcount(d.hand[s]) == popcount(g.hand[s]);
                        ok &= !(all & d.hand[s]);
                        all |= d.hand[s];
                        ok &= (d.hand[s] & v.known[s]) == v.known[s];
                        u32 voids = voidsof(&g, s);
                        for (i32 su = 0; su < 4; su++) {
                            if (voids>>su & 1) {
                                ok &= !(d.hand[s] & suitmask(su, g.trump));
                            }
                        }
                    }
                    ok &= all == ALLCARDS;
                    ok &= popcount(d.kitty) == 4;
                }
            }
        }
        expect(t, "ai.sample", yes(ok), "yes");
    }

    // Experts take sure things: with the top two trumps and an ace,
    // leading trump, they order up and don't pass.
    {
        Game g;
        newgame(&g, {}, 5);
        g.dealer = WEST;
        g.turn   = NORTH;
        g.hand[NORTH] = cardsof("JH JD AH AS KH");
        u32 rest = ALLCARDS & ~g.hand[NORTH];
        // Spread the rest: four to each of the others and the kitty
        i32 seat[] = {EAST, SOUTH, WEST};
        g.hand[EAST] = g.hand[SOUTH] = g.hand[WEST] = 0;
        g.kitty = 0;
        i32 k = 0;
        for (u32 m = rest; m; m &= m-1, k++) {
            i32 c = lowest(m);
            if (k < 15) g.hand[seat[k/5]] |= bit(c);
            else g.kitty |= bit(c);
        }
        // The up-card: a heart from the kitty if there, else any
        g.upcard = lowest(g.kitty);
        u32 hk = g.kitty & suitmask(HEARTS, NOSUIT);
        if (hk) g.upcard = lowest(hk);
        if (suitof(g.upcard) == HEARTS) {
            Move m = decide(&g, NORTH, characters + 0, a);
            expect(t, "ai.orders", yes(m.kind == MV_ORDER), "yes");
        }
    }
}
