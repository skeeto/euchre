// Duels: two pairs of characters play the same deals with the seats
// swapped (duplicate), so the luck of the cards cancels out; and a
// benchmark of expert decisions.

// Play out one hand from a fresh deal; returns team 0's net points.
static i32 playhand(Game *g, i32 ca, i32 cb, Arena ai)
{
    i32 start[2] = {g->score[0], g->score[1]};
    i32 handno = g->handno;
    for (i32 i = 0; i < 200; i++) {
        if (g->phase==PH_SCORE || g->phase==PH_OVER || g->handno!=handno) {
            break;
        }
        Move m = {MV_NEXT, 0, 0};
        if (g->turn >= 0) {
            i32 who = teamof(g->turn) ? cb : ca;
            m = decide(g, g->turn, characters + who, ai);
        }
        apply(g, m);
    }
    if (g->handno != handno) {
        return 0;  // thrown in
    }
    return (g->score[0]-start[0]) - (g->score[1]-start[1]);
}

static i32 duel(Arena a, i32 ca, i32 cb, i32 n)
{
    if (ca<0 || cb<0 || ca>=NCHARACTERS || cb>=NCHARACTERS || n<1) {
        fprintf(stderr, "duel: characters 0-%d\n", NCHARACTERS-1);
        return 1;
    }
    f64 sum = 0, sumsq = 0;
    for (i32 i = 0; i < n; i++) {
        Game g   = {};
        g.rules  = {};
        g.seed   = 1000 + (u64)i*7919;
        g.dealer = i & 3;
        deal(&g);
        Game h = g;
        i32 x = playhand(&g, ca, cb, a);   // A as team 0
        i32 y = playhand(&h, cb, ca, a);   // B as team 0
        f64 d = (f64)(x - y) / 2;          // A's edge on this deal
        sum   += d;
        sumsq += d*d;
        if ((i+1)%100==0 || i+1==n) {
            f64 mean = sum / (i+1);
            f64 var  = sumsq/(i+1) - mean*mean;
            f64 se   = __builtin_sqrt(var / (i+1));
            fprintf(stderr, "%5d deals: %s vs %s: %+.3f +- %.3f points/hand\n",
                    i+1, characters[ca].name.cdata, characters[cb].name.cdata,
                    mean, 1.96*se);
        }
    }
    return 0;
}

static i32 bench(Arena a)
{
    f64 worst = 0, total = 0;
    i64 nodes = 0;
    i32 count = 0;
    for (u64 seed = 1; seed <= 40; seed++) {
        Game g;
        newgame(&g, {}, seed);
        for (i32 i = 0; i<400 && g.phase!=PH_OVER && g.handno<=2; i++) {
            Move m = {MV_NEXT, 0, 0};
            if (g.turn >= 0) {
                Think th;
                Arena ai = a;
                clock_t t0 = clock();
                thinkstart(&th, &g, g.turn, characters, &ai);
                while (!thinkstep(&th, I64MAX)) {}
                f64 ms = (f64)(clock() - t0) * 1000.0 / CLOCKS_PER_SEC;
                if (th.sv) {
                    nodes += th.sv->nodes;
                    total += ms;
                    worst = ms>worst ? ms : worst;
                    count++;
                }
                m = th.result;
            }
            apply(&g, m);
        }
    }
    fprintf(stderr, "%d expert decisions: %.2f ms mean, %.2f ms worst, %lld nodes mean\n",
            count, total/count, worst, (long long)(nodes/count));
    return 0;
}
