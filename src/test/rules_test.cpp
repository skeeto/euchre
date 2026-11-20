// Rules tests: card order, following suit, scoring, and random games
// checked against validgame at every step

static Str cardsstr(Arena *a, u32 m, i32 trump)
{
    Buf b(a, 64);
    i32 cs[NCARDS];
    i32 n = sortcards(m, trump, cs);
    for (i32 i = 0; i < n; i++) {
        if (i) putbyte(&b, ' ');
        printcard(&b, cs[i]);
    }
    return finish(&b);
}

static u32 cardsof(Str s)
{
    Cards c = parsecards(s);
    assert(c.ok);
    return c.mask;
}

static i32 cardof(Str s)
{
    i32 c = parsecard(s);
    assert(c != NOCARD);
    return c;
}

// Play random legal moves until the game ends, checking every state.
static b32 randomgame(Game *g, u64 *rng, i32 maxmoves)
{
    for (i32 i = 0; i<maxmoves && g->phase!=PH_OVER; i++) {
        Move ms[16];
        i32  n = legalmoves(g, ms);
        if (!n) {
            return 0;
        }
        apply(g, ms[randint(rng, n)]);
        if (!validgame(g)) {
            return 0;
        }
    }
    return 1;
}

static void test_rules(Test *t)
{
    Arena a = t->perm;

    // Trump order, bowers first, then the display order of a hand
    expect(t, "sort.hearts", cardsstr(&a, ALLCARDS & suitmask(HEARTS, HEARTS), HEARTS),
           "JH JD AH KH QH TH 9H");
    expect(t, "sort.diamonds.left", cardsstr(&a, cardsof("JH JD 9D AH"), DIAMONDS),
           "JD JH 9D AH");
    expect(t, "sort.clubs", cardsstr(&a, cardsof("JS 9H AC KD QS JC"), CLUBS),
           "JC JS AC 9H QS KD");
    expect(t, "sort.bidding", cardsstr(&a, cardsof("JS 9H AC KD QS"), NOSUIT),
           "QS JS 9H AC KD");

    // Effective suits and power
    expect(t, "left.suit", istrump(cardof("JD"), HEARTS) ? Str("1") : "0", "1");
    expect(t, "left.notdiamond",
           has(suitmask(DIAMONDS, HEARTS), cardof("JD")) ? Str("1") : "0", "0");
    {
        i32 order[] = {cardof("JC"), cardof("JS"), cardof("AC"), cardof("KC"),
                       cardof("QC"), cardof("TC"), cardof("9C"), cardof("AH"),
                       cardof("9H")};
        b32 ok = 1;
        for (i32 i = 0; i+1 < countof(order); i++) {
            ok &= power(order[i], CLUBS, HEARTS) > power(order[i+1], CLUBS, HEARTS);
        }
        ok &= power(cardof("AD"), CLUBS, HEARTS) == 0;
        expect(t, "power.order", ok ? Str("1") : "0", "1");
    }

    // Must follow the led suit, where the left bower is trump
    {
        Game g = {};
        g.trump   = HEARTS;
        g.alone   = 0;
        g.nplays  = 1;
        g.pcard[0] = cardof("AD");
        u32 hand = cardsof("JD 9C KS");
        expect(t, "follow.leftnotdiamond", cardsstr(&a, followable(&g, hand), HEARTS),
               "JD KS 9C");
        hand = cardsof("JD TD 9C");
        expect(t, "follow.diamond", cardsstr(&a, followable(&g, hand), HEARTS), "TD");
        g.pcard[0] = cardof("9H");
        expect(t, "follow.trumplead", cardsstr(&a, followable(&g, hand), HEARTS), "JD");
    }

    // Scoring: makers' tricks to points
    {
        struct { i32 k; b32 alone; i32 pts; i32 to; } cases[] = {
            {0, 0, 2, 1}, {2, 0, 2, 1}, {3, 0, 1, 0}, {4, 0, 1, 0},
            {5, 0, 2, 0}, {5, 1, 4, 0}, {4, 1, 1, 0}, {2, 1, 2, 1},
        };
        for (i32 i = 0; i < countof(cases); i++) {
            Game g = {};
            g.rules     = {};
            g.maker     = SOUTH;
            g.alone     = cases[i].alone;
            g.tricks[SOUTH] = cases[i].k;
            g.tricks[WEST]  = 5 - cases[i].k;
            scorehand(&g);
            Buf b(&a, 16);
            print(&b, (i64)g.points);
            putbyte(&b, '/');
            print(&b, (i64)g.pointsto);
            Buf w(&a, 16);
            print(&w, (i64)cases[i].pts);
            putbyte(&w, '/');
            print(&w, (i64)cases[i].to);
            expect(t, "score.table", finish(&b), finish(&w));
        }
    }

    // The dealer's partner orders alone: the dealer sits out, no pickup,
    // and the dealer's left leads.
    {
        Game g;
        newgame(&g, {}, 1234);
        g.dealer = WEST;
        g.phase  = PH_BID1;
        g.turn   = NORTH;
        // two passes would be needed for EAST; build directly instead
        g.turn = (g.dealer+1) & 3;   // NORTH
        apply(&g, {MV_PASS, 0, 0});  // NORTH
        apply(&g, {MV_ORDER, 0, 1}); // EAST, the dealer's partner, alone
        Buf b(&a, 64);
        print(&b, (i64)g.phase);
        putbyte(&b, ' ');
        print(&b, (i64)sittingout(&g));
        putbyte(&b, ' ');
        print(&b, (i64)g.turn);
        putbyte(&b, ' ');
        print(&b, (i64)has(g.kitty, g.upcard));
        expect(t, "alone.dealerpartner", finish(&b), "3 1 2 1");
        expect(t, "alone.valid", validgame(&g) ? Str("1") : "0", "1");
    }

    // The dealer's left sits out: the next seat leads
    {
        Game g;
        newgame(&g, {}, 99);
        g.dealer = SOUTH;
        g.turn   = WEST;
        apply(&g, {MV_PASS, 0, 0});   // WEST
        apply(&g, {MV_ORDER, 0, 1});  // NORTH alone: WEST would lead but...
        // NORTH alone: SOUTH, the dealer, sits out. WEST leads.
        expect(t, "alone.lead1", seatname(g.turn), "West");
    }
    {
        Game g;
        newgame(&g, {}, 99);
        g.dealer = SOUTH;
        g.turn   = WEST;
        for (i32 i = 0; i < 5; i++) {
            apply(&g, {MV_PASS, 0, 0});
        }
        // round two, WEST passed again; NORTH names a suit
        i32 s = (suitof(g.upcard) + 1) & 3;
        apply(&g, {MV_PASS, 0, 0});           // NORTH
        apply(&g, {MV_CALL, s, 1});           // EAST alone: WEST sits out
        expect(t, "alone.lead2", seatname(g.turn), "North");
    }

    // Stick the dealer: the dealer may not pass in round two
    {
        Game g;
        newgame(&g, {}, 7);
        for (i32 i = 0; i < 7; i++) {
            apply(&g, {MV_PASS, 0, 0});
        }
        expect(t, "stick.nopass", legal(&g, {MV_PASS, 0, 0}) ? Str("1") : "0", "0");
        expect(t, "stick.nocall.up",
               legal(&g, {MV_CALL, suitof(g.upcard), 0}) ? Str("1") : "0", "0");
        Rules r = {};
        r.stick = 0;
        newgame(&g, r, 7);
        i32 dealer = g.dealer;
        for (i32 i = 0; i < 8; i++) {
            apply(&g, {MV_PASS, 0, 0});
        }
        expect(t, "throwin.phase", g.phase==PH_PASSED ? Str("1") : "0", "1");
        expect(t, "throwin.valid", validgame(&g) ? Str("1") : "0", "1");
        apply(&g, {MV_NEXT, 0, 0});
        expect(t, "throwin.rotates", g.dealer==((dealer+1)&3) ? Str("1") : "0", "1");
    }

    // Random games stay valid at every step, under every rule set
    {
        u64 rng = 1;
        b32 ok  = 1;
        i32 targets[] = {5, 7, 10};
        for (i32 i = 0; i < 300 && ok; i++) {
            Rules r = {};
            r.target = targets[i % 3];
            r.stick  = i & 1;
            Game g;
            newgame(&g, r, rng);
            ok = validgame(&g) && randomgame(&g, &rng, 4000) && g.phase==PH_OVER;
        }
        expect(t, "random.games", ok ? Str("1") : "0", "1");
    }

    // A broken state is caught: a card in two places, a renege
    {
        Game g;
        u64  rng = 5;
        newgame(&g, {}, 42);
        for (; g.phase != PH_PLAY;) {
            Move ms[16];
            i32  n = legalmoves(&g, ms);
            apply(&g, ms[randint(&rng, n)]);
        }
        Game bad = g;
        bad.hand[0] |= bad.hand[1] & (0u - bad.hand[1]);
        expect(t, "invalid.twice", validgame(&bad) ? Str("1") : "0", "0");
        bad = g;
        bad.score[0] = 99;
        expect(t, "invalid.score", validgame(&bad) ? Str("1") : "0", "0");
        bad = g;
        bad.turn = (bad.turn + 1) & 3;
        expect(t, "invalid.turn", validgame(&bad) ? Str("1") : "0", "0");
    }
}
