// Computer players: characters, heuristics, and a sampling expert
//
// Each computer seat is a character: a level and a style. Beginners
// count trump and play their highest card or their lowest. Intermediate
// players weigh the turned card by seat, pull trump as makers, cash
// aces, let a partner's winner stand, and keep track of the master card
// in each suit and who has shown out of one.
//
// Experts deal out the unseen cards many ways consistent with what
// their seat knows (its hand, the cards played, where the turned card
// went, who has shown out of what, and how each bid colors a hand), and
// solve every deal exactly, double dummy: for each candidate, whether
// the makers can still reach three tricks, and five. The average score
// over the deals picks the move. Bids, loners and the dealer's discard
// are weighed the same way, from the opening lead.
//
// An expert's decision is a job run a slice at a time under a node
// budget, so the frame rate holds on any machine, including the web's
// single thread. Its random choices are seeded from the game state, so
// a resumed game decides exactly as it would have.


// Characters

enum : i32 {
    LV_BEGINNER,
    LV_INTERMEDIATE,
    LV_EXPERT,
};

struct Character {
    Str name;
    i32 level;
    f32 bold;    // + bids more, - bids less
    f32 loner;   // + goes alone more readily
    f32 slip;    // chance of a careless card
    i32 pace;    // ms to decide, before the speed setting
    u32 color;
    Str blurb;
    b32 needbower;  // won't order up without a bower
};

static Character const characters[] = {
    {"Ruth",  LV_EXPERT,       0.00f,  0.0f, 0.00f, 700, 0x5b8def,
     "Expert. Steady and by the book.", 0},
    {"Hank",  LV_EXPERT,       0.25f,  0.6f, 0.00f, 500, 0xe0603e,
     "Expert. Aggressive, and loves a loner.", 0},
    {"Walt",  LV_EXPERT,      -0.25f, -0.3f, 0.00f, 900, 0x8a7a5c,
     "Expert. Cautious; rarely calls in round two.", 0},
    {"Bev",   LV_INTERMEDIATE, 0.00f,  0.0f, 0.02f, 650, 0x3fae7a,
     "Intermediate. A balanced club player.", 0},
    {"Earl",  LV_INTERMEDIATE, 1.00f,  0.5f, 0.03f, 450, 0xd8a634,
     "Intermediate. A reckless bidder.", 0},
    {"Marge", LV_INTERMEDIATE,-0.50f, -0.5f, 0.02f, 800, 0xb05fc4,
     "Intermediate. Won't order up without a bower.", 1},
    {"Gus",   LV_BEGINNER,     0.75f,  0.3f, 0.15f, 400, 0x46a3c0,
     "Beginner. Eager, and a little sloppy.", 0},
    {"Dot",   LV_BEGINNER,    -0.75f, -0.5f, 0.10f, 900, 0xd46a8f,
     "Beginner. Timid with her bids.", 0},
};

enum : i32 {
    NCHARACTERS     = countof(characters),
    DEFAULT_PARTNER = 0,  // Ruth
    DEFAULT_LEFT    = 3,  // Bev, West
    DEFAULT_RIGHT   = 4,  // Earl, East
};

static Str levelname(i32 level)
{
    static Str names[] = {"Beginner", "Intermediate", "Expert"};
    return names[level];
}


// What a seat can see

// Who has shown out of which effective suits, by failing to follow.
static u32 voidsof(Game *g, i32 seat)  // bit s for suit s
{
    u32 v   = 0;
    i32 per = trickcards(g);
    for (i32 b = 0; b < g->nplays; b += per) {
        i32 led = effsuit(g->pcard[b], g->trump);
        for (i32 i = b+1; i<b+per && i<g->nplays; i++) {
            if (g->pseat[i]==seat && effsuit(g->pcard[i], g->trump)!=led) {
                v |= 1u << led;
            }
        }
    }
    return v;
}

static b32 pickedup(Game *g)
{
    return g->maker>=0 && g->nbids<=4 && sittingout(g)!=g->dealer;
}

// The power within its own suit of each card, for comparing cards of
// one effective suit.
static i32 rankin(i32 c, i32 trump)
{
    return power(c, trump, effsuit(c, trump));
}

// Cards of effective suit s, not yet played and not in hand, that
// outrank card c.
static u32 higherout(Game *g, i32 c, u32 hand)
{
    i32 t = g->trump;
    i32 s = effsuit(c, t);
    u32 m = suitmask(s, t) & ~playedmask(g) & ~hand;
    u32 r = 0;
    for (; m; m &= m-1) {
        i32 x = lowest(m);
        if (rankin(x, t) > rankin(c, t)) {
            r |= bit(x);
        }
    }
    // A buried card can't win anything
    return r & ~(g->discard!=NOCARD ? bit(g->discard) : 0);
}


// Bidding by points

// Rough worth of a hand with trump t, about 3 per expected trick.
static f32 handpoints(u32 hand, i32 t)
{
    f32 pts   = 0;
    i32 ntrump = 0;
    for (u32 m = hand; m; m &= m-1) {
        i32 c = lowest(m);
        if (istrump(c, t)) {
            ntrump++;
            static f32 const tv[] = {1.2f, 1.3f, 0, 1.5f, 1.7f, 2.1f};
            pts += isright(c, t) ? 3.3f : isleft(c, t) ? 2.7f : tv[rankof(c)];
        } else if (rankof(c) == ACE) {
            pts += 1.0f;
        } else if (rankof(c)==KING && has(hand, mkcard(suitof(c), ACE))) {
            pts += 0.5f;
        } else if (rankof(c) == KING) {
            pts += 0.2f;
        }
    }
    if (ntrump >= 2) {
        for (i32 s = 0; s < 4; s++) {
            if (s!=t && !(hand & suitmask(s, t))) {
                pts += 0.6f;
            }
        }
    }
    if (ntrump >= 4) {
        pts += 1.0f;
    }
    return pts;
}

static b32 hasbower(u32 hand, i32 t)
{
    return has(hand, mkcard(t, JACK)) || has(hand, mkcard(t^1, JACK));
}

// The dealer's discard: never trump if avoidable; a lone low card to
// make a void; else the lowest off card, keeping aces.
static i32 heuristicdiscard(u32 hand, i32 t)
{
    i32 best = NOCARD;
    f32 bestv = 1e9f;
    for (u32 m = hand; m; m &= m-1) {
        i32 c = lowest(m);
        f32 v;
        if (istrump(c, t)) {
            v = 100.0f + (f32)rankin(c, t);
        } else {
            i32 s     = effsuit(c, t);
            i32 count = popcount(hand & suitmask(s, t));
            v = (f32)rankof(c)*2;
            if (rankof(c) == ACE) {
                v += 40;
            }
            if (count == 1) {
                v -= 6;  // makes a void
            }
            if (rankof(c)==KING && has(hand, mkcard(s, ACE))) {
                v += 5;
            }
        }
        if (v < bestv) {
            bestv = v;
            best  = c;
        }
    }
    return best;
}

// A hand's points with trump t once the turned card u goes to the
// dealer: the dealer's own hand gains it, a partner's helps, an
// opponent's hurts.
static f32 bidpoints(Game *g, i32 seat, i32 t, b32 round1)
{
    u32 hand = g->hand[seat];
    f32 pts  = handpoints(hand, t);
    if (!round1) {
        return pts;
    }
    i32 u = g->upcard;
    if (seat == g->dealer) {
        u32 with = hand | bit(u);
        return handpoints(with & ~bit(heuristicdiscard(with, t)), t);
    }
    f32 uv = isright(u, t) ? 3.0f : rankof(u)==ACE ? 1.8f : rankof(u)>=QUEEN ? 1.3f : 1.0f;
    return teamof(g->dealer)==teamof(seat) ? pts + 0.5f*uv : pts - 0.6f*uv;
}

struct Bid {
    Move move;
    f32  points;
};

static Bid pointbid(Game *g, i32 seat, Character const *ch)
{
    Bid  r      = {};
    b32  round1 = g->phase == PH_BID1;
    b32  stuck  = !round1 && g->rules.stick && seat==g->dealer;
    i32  best   = NOSUIT;
    f32  bestp  = -1e9f;
    for (i32 s = 0; s < 4; s++) {
        if (round1 ? s!=suitof(g->upcard) : s==suitof(g->upcard)) {
            continue;
        }
        f32 p = bidpoints(g, seat, s, round1);
        if (!round1 && s==(suitof(g->upcard)^1) && seat==((g->dealer+1)&3)) {
            p += 0.5f;  // "next": the turned suit's partner, from the dealer's left
        }
        if (p > bestp) {
            bestp = p;
            best  = s;
        }
    }
    f32 need = ch->level==LV_BEGINNER ? 8.0f : 7.6f;
    need -= ch->bold;
    if (!round1) {
        need += 0.3f;
    }
    if (g->score[teamof(seat)^1] >= g->rules.target-2) {
        need -= 0.4f;  // the other side is close: defend by calling
    }
    r.points = bestp;
    b32 ok = bestp >= need;
    if (ch->needbower && !hasbower(g->hand[seat] | (seat==g->dealer && round1 ? bit(g->upcard) : 0), best)) {
        ok = 0;
    }
    if (!ok && !stuck) {
        r.move = {MV_PASS, 0, 0};
        return r;
    }
    b32 alone = bestp >= 13.0f - ch->loner && hasbower(g->hand[seat], best) &&
                popcount(g->hand[seat] & trumpmask(best)) >= 3;
    if (round1) {
        r.move = {MV_ORDER, 0, alone};
    } else {
        r.move = {MV_CALL, best, alone};
    }
    return r;
}


// Playing by rules of thumb

// The best card on the table and who played it.
struct Lead {
    i32 led;
    i32 best;   // card, NOCARD on the lead
    i32 seat;
    i32 n;      // cards played to this trick
};

static Lead trickstate(Game *g)
{
    Lead r = {NOSUIT, NOCARD, -1, 0};
    i32  b = trickbase(g);
    r.n = g->nplays - b;
    if (!r.n) {
        return r;
    }
    r.led = effsuit(g->pcard[b], g->trump);
    for (i32 i = b; i < g->nplays; i++) {
        if (r.best==NOCARD ||
            power(g->pcard[i], g->trump, r.led) > power(r.best, g->trump, r.led)) {
            r.best = g->pcard[i];
            r.seat = g->pseat[i];
        }
    }
    return r;
}

static i32 lowestof(u32 m, i32 t)
{
    i32 best = NOCARD;
    for (; m; m &= m-1) {
        i32 c = lowest(m);
        // Off-suit before trump, then the lowest rank
        i32 k  = (istrump(c, t) ? 100 : 0) + rankin(c, t);
        i32 kb = best==NOCARD ? 1000 : (istrump(best, t) ? 100 : 0) + rankin(best, t);
        if (k < kb) best = c;
    }
    return best;
}

static i32 highestof(u32 m, i32 t)
{
    i32 best = NOCARD;
    for (; m; m &= m-1) {
        i32 c = lowest(m);
        i32 k  = (istrump(c, t) ? 100 : 0) + rankin(c, t);
        i32 kb = best==NOCARD ? -1 : (istrump(best, t) ? 100 : 0) + rankin(best, t);
        if (k > kb) best = c;
    }
    return best;
}

// Cards in m that beat the trick so far.
static u32 winners(Game *g, Lead *l, u32 m)
{
    u32 r = 0;
    for (; m; m &= m-1) {
        i32 c = lowest(m);
        if (l->best==NOCARD || power(c, g->trump, l->led)>power(l->best, g->trump, l->led)) {
            r |= bit(c);
        }
    }
    return r;
}

static i32 beginnerplay(Game *g, i32 seat)
{
    u32  legal = followable(g, g->hand[seat]);
    Lead l     = trickstate(g);
    if (!l.n) {
        return highestof(legal, g->trump);
    }
    u32 w = winners(g, &l, legal);
    return w ? highestof(w, g->trump) : lowestof(legal, g->trump);
}

// The lowest card of m that makes a void or keeps the shortest suit.
static i32 throwaway(Game *g, u32 hand, u32 m)
{
    i32 t    = g->trump;
    i32 best = NOCARD;
    i32 bestk = 1<<30;
    for (; m; m &= m-1) {
        i32 c = lowest(m);
        i32 s = effsuit(c, t);
        i32 k = rankin(c, t)*4 + popcount(hand & suitmask(s, t));
        if (istrump(c, t)) k += 1000;
        if (rankof(c)==ACE && !istrump(c, t)) k += 200;
        if (k < bestk) {
            bestk = k;
            best  = c;
        }
    }
    return best;
}

static i32 intermediateplay(Game *g, i32 seat)
{
    i32  t     = g->trump;
    u32  hand  = g->hand[seat];
    u32  legal = followable(g, hand);
    u32  trump = hand & trumpmask(t);
    Lead l     = trickstate(g);
    b32  makers = teamof(g->maker) == teamof(seat);
    if (popcount(legal) == 1) {
        return lowest(legal);
    }

    if (!l.n) {
        // Pull trump when making with the master trump
        if (makers && trump) {
            i32 top = highestof(trump, t);
            if (!higherout(g, top, hand) && popcount(trump)>=2) {
                return top;
            }
        }
        // Cash a master off-suit card
        for (u32 m = legal & ~trump; m; m &= m-1) {
            i32 c = lowest(m);
            if (!higherout(g, c, hand) && rankof(c)>=KING) {
                return c;
            }
        }
        // Partner called: lead them trump
        if (g->maker==partner(seat) && trump) {
            return highestof(trump, t);
        }
        // Otherwise a low card from the shortest off suit
        if (legal & ~trump) {
            return throwaway(g, hand, legal & ~trump);
        }
        return makers ? highestof(trump, t) : lowestof(trump, t);
    }

    b32 last   = l.n == trickcards(g)-1;
    b32 mate   = l.seat == partner(seat);
    u32 w      = winners(g, &l, legal);
    b32 secure = mate && (last || !higherout(g, l.best, hand));
    if (secure || (mate && l.n>=2 && istrump(l.best, t))) {
        return throwaway(g, hand, legal);
    }
    if (w) {
        if (last) {
            return lowestof(w, t);  // just enough
        }
        i32 top = highestof(w, t);
        if (!higherout(g, top, hand)) {
            // A sure winner, but not wasted when a lower one does
            i32 low = lowestof(w, t);
            return !higherout(g, low, hand) ? low : top;
        }
        return l.n>=2 || !mate ? top : throwaway(g, hand, legal);
    }
    return throwaway(g, hand, legal);
}


// Double-dummy solver

struct Pos {
    u32 hand[4];
    i32 trump;
    i32 out;       // the seat sitting out, or -1
    i32 per;       // cards in a full trick
    i32 mteam;     // the makers' team
    i32 turn;
    i32 n;         // cards in the trick so far
    i32 led;
    i32 best;      // power of the best card in the trick
    i32 bestseat;
    u32 table;     // cards in the trick
};

struct TTEntry {
    u64 k1, k2;
    i8  lo, hi;    // bounds on the makers' tricks still to come
};

enum : i32 {
    TTBITS = 16,
};

struct Solver {
    TTEntry *tt;
    i64      nodes;
};

static Solver *newsolver(Arena *a)
{
    Solver *s = alloc<Solver>(a);
    s->tt = alloc<TTEntry>(a, (iz)1 << TTBITS);
    return s;
}

static i32 dnext(Pos *p, i32 s)
{
    s = (s + 1) & 3;
    return s==p->out ? (s+1)&3 : s;
}

// Play c for the seat to move; returns 1 when it completes a trick won
// by the makers, and leaves the trick's winner to lead.
static i32 dplay(Pos *p, i32 c, b32 *done)
{
    i32 seat = p->turn;
    p->hand[seat] &= ~bit(c);
    if (!p->n) {
        p->led = effsuit(c, p->trump);
    }
    i32 pw = power(c, p->trump, p->led);
    if (!p->n || pw>p->best) {
        p->best     = pw;
        p->bestseat = seat;
    }
    p->table |= bit(c);
    p->n++;
    *done = p->n == p->per;
    if (*done) {
        i32 w = p->bestseat;
        p->n     = 0;
        p->table = 0;
        p->led   = NOSUIT;
        p->turn  = w;
        return (w & 1) == p->mteam;
    }
    p->turn = dnext(p, seat);
    return 0;
}

// Candidate cards for the seat to move, with equivalent cards (adjacent
// within a suit among the cards still out) reduced to one, ordered to
// cut off early: winners first for the side to move.
static i32 dmoves(Pos *p, i32 *out)
{
    i32 t     = p->trump;
    u32 hand  = p->hand[p->turn];
    u32 legal = hand;
    if (p->n) {
        u32 f = hand & suitmask(p->led, t);
        legal = f ? f : hand;
    }
    u32 live = p->hand[0] | p->hand[1] | p->hand[2] | p->hand[3] | p->table;
    i32 n = 0;
    for (i32 s = 0; s < 4; s++) {
        u32 m = legal & suitmask(s, t);
        if (!m) continue;
        // Walk the live cards of the suit from the top; keep one card
        // per run of our own adjacent cards.
        u32 suit = live & suitmask(s, t);
        i32 cs[8], nc = 0;
        for (u32 x = suit; x; x &= x-1) {
            i32 c = lowest(x);
            i32 k = nc++;
            for (; k>0 && rankin(cs[k-1], t)<rankin(c, t); k--) {
                cs[k] = cs[k-1];
            }
            cs[k] = c;
        }
        b32 prev = 0;
        for (i32 i = 0; i < nc; i++) {
            b32 mine = has(m, cs[i]);
            if (mine && !prev) {
                out[n++] = cs[i];
            }
            prev = mine;
        }
    }
    // Order: cards that take the trick (cheapest first), then the rest
    // from the bottom.
    i32 key[8];
    for (i32 i = 0; i < n; i++) {
        i32 c  = out[i];
        i32 pw = p->n ? power(c, t, p->led) : 20 + rankin(c, t);
        key[i] = p->n ? (pw>p->best ? pw : 100 + rankin(c, t)) : 64 - pw;
    }
    for (i32 i = 1; i < n; i++) {
        for (i32 j = i; j>0 && key[j-1]>key[j]; j--) {
            i32 tk = key[j]; key[j] = key[j-1]; key[j-1] = tk;
            i32 tc = out[j]; out[j] = out[j-1]; out[j-1] = tc;
        }
    }
    return n;
}

// Can the makers take at least need of the tricks still to be played?
static b32 reach(Solver *sv, Pos *p, i32 need)
{
    sv->nodes++;
    TTEntry *e = 0;
    u64 k1 = 0, k2 = 0;
    if (!p->n) {
        i32 left = popcount(p->hand[p->turn]);
        if (need <= 0) return 1;
        if (need > left) return 0;
        k1 = (u64)p->hand[0] | (u64)p->hand[1]<<24 | (u64)(p->hand[2] & 0xffff)<<48;
        k2 = (u64)(p->hand[2]>>16) | (u64)p->hand[3]<<8 | (u64)p->turn<<32 |
             (u64)(p->trump+1)<<34 | (u64)(p->out+1)<<37 | (u64)p->mteam<<40;
        u64 h = hashmix(hashmix(0x9e3779b97f4a7c15u, k1), k2);
        e = sv->tt + (h >> (64 - TTBITS));
        if (e->k1==k1 && e->k2==k2) {
            if (e->lo >= need) return 1;
            if (e->hi <  need) return 0;
        }
    }

    i32 cs[8];
    i32 n     = dmoves(p, cs);
    b32 maxer = (p->turn & 1) == p->mteam;
    b32 r     = !maxer;
    for (i32 i = 0; i < n; i++) {
        Pos q = *p;
        b32 done;
        i32 got = dplay(&q, cs[i], &done);
        b32 ok  = reach(sv, &q, need - got);
        if (ok == maxer) {
            r = ok;
            break;
        }
    }

    if (e) {
        if (e->k1!=k1 || e->k2!=k2) {
            e->k1 = k1;
            e->k2 = k2;
            e->lo = 0;
            e->hi = 5;
        }
        if (r) {
            e->lo = (i8)maxi(e->lo, need);
        } else {
            e->hi = (i8)mini(e->hi, need-1);
        }
    }
    return r;
}

// The makers' points (negative: the defenders') when the makers have
// made tricks already and the position p is still to play.
static i32 ddpoints(Solver *sv, Pos *p, i32 made, b32 alone)
{
    if (!reach(sv, p, 3 - made)) {
        return -2;
    }
    if (reach(sv, p, 5 - made)) {
        return alone ? 4 : 2;
    }
    return 1;
}


// Dealing out the unseen cards

struct Deal {
    u32 hand[4];
    u32 kitty;
};

// What seat me knows: cards whose places are known, and how many
// unknown cards each place holds.
struct View_ {
    u32 known[4];     // cards known to be in each hand
    u32 knownkitty;
    i32 count[5];     // unknown cards per hand, then the kitty
    u32 pool;         // the unknown cards
    u32 voids[4];     // effective suits each seat can't hold
};

static View_ seatview(Game *g, i32 me)
{
    View_ v = {};
    u32 played = playedmask(g);
    v.known[me] = g->hand[me];
    if (g->maker>=0 && pickedup(g)) {
        if (g->dealer==me) {
            v.knownkitty |= g->discard!=NOCARD ? bit(g->discard) : 0;
        } else if (!has(played, g->upcard)) {
            v.known[g->dealer] |= bit(g->upcard);
        }
    } else {
        v.knownkitty |= bit(g->upcard);  // turned down, or never taken
    }
    u32 knownall = v.knownkitty | played;
    for (i32 s = 0; s < 4; s++) {
        knownall |= v.known[s];
    }
    v.pool = ALLCARDS & ~knownall;
    for (i32 s = 0; s < 4; s++) {
        v.count[s] = popcount(g->hand[s]) - popcount(v.known[s]);
        v.voids[s] = s==me ? 0 : voidsof(g, s);
    }
    v.count[4] = popcount(g->kitty) - popcount(v.knownkitty);
    return v;
}

// A random deal of the pool consistent with the counts and voids, or
// ok 0 after too many dead ends.
static b32 sampledeal(Game *g, View_ *v, u64 *rng, Deal *d, b32 usevoids)
{
    i32 pool[NCARDS];
    i32 n = 0;
    for (u32 m = v->pool; m; m &= m-1) {
        pool[n++] = lowest(m);
    }
    for (i32 tries = 0; tries < 50; tries++) {
        for (i32 i = n-1; i > 0; i--) {
            i32 j = randint(rng, i+1);
            i32 x = pool[i]; pool[i] = pool[j]; pool[j] = x;
        }
        i32 room[5];
        for (i32 s = 0; s < 5; s++) room[s] = v->count[s];
        for (i32 s = 0; s < 4; s++) d->hand[s] = v->known[s];
        d->kitty = v->knownkitty;
        b32 ok = 1;
        for (i32 i = 0; i<n && ok; i++) {
            i32 c = pool[i];
            i32 s = g->trump!=NOSUIT ? effsuit(c, g->trump) : suitof(c);
            i32 total = 0;
            i32 w[5];
            for (i32 h = 0; h < 5; h++) {
                b32 can = room[h]>0 && (h==4 || !usevoids || !(v->voids[h]>>s & 1));
                w[h] = can ? room[h] : 0;
                total += w[h];
            }
            if (!total) {
                ok = 0;
                break;
            }
            i32 r = randint(rng, total);
            i32 h = 0;
            for (; r >= w[h]; h++) {
                r -= w[h];
            }
            room[h]--;
            if (h == 4) {
                d->kitty |= bit(c);
            } else {
                d->hand[h] |= bit(c);
            }
        }
        if (ok) {
            return 1;
        }
    }
    return 0;
}

// How likely the bidding makes a deal: a maker short of trump, or a
// passer who held a strong hand, is less likely.
static f32 dealweight(Game *g, Deal *d, i32 me)
{
    f32 w = 1;
    if (g->maker>=0 && g->maker!=me) {
        u32 had = d->hand[g->maker];
        for (i32 i = 0; i < g->nplays; i++) {
            if (g->pseat[i] == g->maker) had |= bit(g->pcard[i]);
        }
        if (g->dealer==g->maker && pickedup(g) && g->discard!=NOCARD) {
            had |= bit(g->discard);
        }
        i32 nt = popcount(had & trumpmask(g->trump));
        static f32 const tw[] = {0.05f, 0.3f, 0.75f, 1, 1, 1, 1};
        w *= tw[nt];
        if (g->alone && nt<3) w *= 0.3f;
    }
    // Round-one passers rarely sat on three trump with a bower
    i32 up = suitof(g->upcard);
    for (i32 i = 0; i<g->nbids && i<4; i++) {
        i32 s = (g->dealer + 1 + i) & 3;
        if (s==me || g->bids[i]!=NOSUIT) continue;
        u32 had = d->hand[s];
        for (i32 k = 0; k < g->nplays; k++) {
            if (g->pseat[k] == s) had |= bit(g->pcard[k]);
        }
        if (popcount(had & trumpmask(up))>=3 && hasbower(had, up)) {
            w *= 0.4f;
        }
    }
    return w;
}

static b32 sampleweighted(Game *g, View_ *v, u64 *rng, Deal *d, i32 me)
{
    for (i32 i = 0; i < 20; i++) {
        if (!sampledeal(g, v, rng, d, 1) && !sampledeal(g, v, rng, d, 0)) {
            return 0;
        }
        if (randf(rng) < dealweight(g, d, me)) {
            return 1;
        }
    }
    return 1;  // the last one, unweighted
}

// The opening position once trump is made with the given deal.
static Pos openpos(Game *g, Deal *d, i32 trump, i32 maker, b32 alone, b32 round1)
{
    Pos p = {};
    for (i32 s = 0; s < 4; s++) p.hand[s] = d->hand[s];
    p.trump = trump;
    p.out   = alone ? partner(maker) : -1;
    p.per   = alone ? 3 : 4;
    p.mteam = teamof(maker);
    p.led   = NOSUIT;
    if (round1 && p.out!=g->dealer) {
        u32 h = p.hand[g->dealer] | bit(g->upcard);
        p.hand[g->dealer] = h & ~bit(heuristicdiscard(h, trump));
    }
    if (p.out >= 0) {
        p.hand[p.out] = 0;
    }
    p.turn = dnext(&p, g->dealer);
    return p;
}

// The position now, with the hidden hands from a deal.
static Pos nowpos(Game *g, Deal *d)
{
    Pos p = {};
    for (i32 s = 0; s < 4; s++) p.hand[s] = d->hand[s];
    p.trump = g->trump;
    p.out   = sittingout(g);
    p.per   = trickcards(g);
    p.mteam = teamof(g->maker);
    p.led   = NOSUIT;
    if (p.out >= 0) {
        p.hand[p.out] = 0;
    }
    p.turn  = g->turn;
    for (i32 i = trickbase(g); i < g->nplays; i++) {
        i32 c  = g->pcard[i];
        if (!p.n) p.led = effsuit(c, p.trump);
        i32 pw = power(c, p.trump, p.led);
        if (!p.n || pw>p.best) {
            p.best     = pw;
            p.bestseat = g->pseat[i];
        }
        p.table |= bit(c);
        p.n++;
    }
    return p;
}

static i32 makertricks(Game *g)
{
    return g->tricks[g->maker] + g->tricks[partner(g->maker)];
}


// Decisions

// Value for team us of the makers scoring pts (negative: defenders).
static f32 utility(Game *g, i32 us, i32 mteam, i32 pts)
{
    i32 team = pts>0 ? mteam : mteam^1;
    i32 p    = pts>0 ? pts : -pts;
    i32 need = g->rules.target - g->score[team];
    f32 v    = p>=need ? (f32)need + 1.5f : (f32)p;
    return team==us ? v : -v;
}

enum : i32 {
    MAXCAND = 12,
};

struct Think {
    b32       active;
    b32       done;
    i32       seat;
    Character const *ch;
    Game      g;
    Move      cand[MAXCAND];
    i32       ncand;
    f32       sum[MAXCAND];
    i32       samples;     // dealt so far
    i32       want;        // samples to deal
    u64       rng;
    Solver   *sv;
    View_     view;
    Move      result;
    f32       values[MAXCAND];  // final averages, for hints and tests
};

static u64 decisionseed(Game *g, i32 seat)
{
    u64 h = hashmix(g->seed, (u64)g->handno);
    h = hashmix(h, (u64)(g->nbids*32 + g->nplays));
    h = hashmix(h, (u64)(g->phase*4 + seat));
    return hashmix(h, (u64)(g->score[0]*16 + g->score[1]));
}

static Move heuristicmove(Game *g, i32 seat, Character const *ch, u64 *rng)
{
    switch (g->phase) {
    case PH_BID1:
    case PH_BID2:
        return pointbid(g, seat, ch).move;
    case PH_DISCARD:
        return {MV_DISCARD, heuristicdiscard(g->hand[seat], g->trump), 0};
    case PH_PLAY: {
        u32 legal = followable(g, g->hand[seat]);
        if (ch->slip>0 && randf(rng)<ch->slip) {
            i32 k = randint(rng, popcount(legal));
            for (; k; k--) legal &= legal-1;
            return {MV_PLAY, lowest(legal), 0};
        }
        i32 c = ch->level==LV_BEGINNER ? beginnerplay(g, seat) : intermediateplay(g, seat);
        return {MV_PLAY, c, 0};
    }
    }
    return {MV_NEXT, 0, 0};
}

// Start deciding for seat: a heuristic player decides at once, an
// expert sets up its job.
static void thinkstart(Think *t, Game *g, i32 seat, Character const *ch, Arena *ai)
{
    *t        = {};
    t->active = 1;
    t->seat   = seat;
    t->ch     = ch;
    t->g      = *g;
    t->rng    = decisionseed(g, seat);
    if (ch->level!=LV_EXPERT || g->phase>=PH_TRICK) {
        t->result = heuristicmove(g, seat, ch, &t->rng);
        t->done   = 1;
        return;
    }
    Move ms[16];
    i32  n = legalmoves(g, ms);
    if (n == 1) {
        t->result = ms[0];
        t->done   = 1;
        return;
    }
    for (i32 i = 0; i < n; i++) {
        if (ms[i].kind != MV_PASS) {
            t->cand[t->ncand++] = ms[i];
        }
    }
    t->want = g->phase==PH_PLAY ? 32 : 28;
    t->sv   = newsolver(ai);
    t->view = seatview(g, seat);
}

// The expert's verdict once every sample is in.
static void thinkfinish(Think *t)
{
    Game *g  = &t->g;
    i32 best = 0;
    for (i32 i = 0; i < t->ncand; i++) {
        t->values[i] = t->samples ? t->sum[i]/(f32)t->samples : 0;
        if (t->values[i] > t->values[best]) {
            best = i;
        }
    }
    t->result = t->cand[best];
    t->done   = 1;
    if (g->phase!=PH_BID1 && g->phase!=PH_BID2) {
        // Among cards equally good on the deals, play as a careful
        // player would: the rule of thumb if it's one, else the cheapest.
        u32 tied = 0;
        for (i32 i = 0; i < t->ncand; i++) {
            if (t->values[i] >= t->values[best]-0.02f) {
                tied |= bit(t->cand[i].arg);
            }
        }
        i32 c = g->phase==PH_PLAY ? intermediateplay(g, t->seat)
                                  : heuristicdiscard(g->hand[t->seat], g->trump);
        if (!has(tied, c)) {
            c = g->phase==PH_PLAY ? lowestof(tied, g->trump)
                                  : heuristicdiscard(tied, g->trump);
        }
        t->result.arg = c;
        return;
    }

    // A bid must beat passing, worth a little either way by seat; going
    // alone must beat playing with a partner.
    b32 round1 = g->phase == PH_BID1;
    b32 stuck  = !round1 && g->rules.stick && t->seat==g->dealer;
    i32 bestsuit = -1;
    f32 bestv    = -1e9f;
    for (i32 i = 0; i < t->ncand; i++) {
        if (!t->cand[i].alone && t->values[i]>bestv) {
            bestv    = t->values[i];
            bestsuit = i;
        }
    }
    f32 need = round1 ? 0.15f : 0.25f;
    need -= t->ch->bold * 0.35f;
    if (bestsuit<0 || (bestv<need && !stuck)) {
        t->result = {MV_PASS, 0, 0};
        return;
    }
    Move m = t->cand[bestsuit];
    for (i32 i = 0; i < t->ncand; i++) {
        Move a = t->cand[i];
        if (a.alone && a.arg==m.arg &&
            t->values[i] > bestv + 0.35f - 0.25f*t->ch->loner) {
            m = a;
        }
    }
    t->result = m;
}

// Score every candidate on one more deal.
static void thinksample(Think *t)
{
    Game *g  = &t->g;
    Deal  d;
    i32   us = teamof(t->seat);
    if (!sampleweighted(g, &t->view, &t->rng, &d, t->seat)) {
        t->samples = t->want;  // can't happen with consistent state
        return;
    }
    for (i32 i = 0; i < t->ncand; i++) {
        Move m   = t->cand[i];
        f32  val = 0;
        switch (g->phase) {
        case PH_BID1:
        case PH_BID2: {
            b32 round1 = g->phase == PH_BID1;
            i32 trump  = round1 ? suitof(g->upcard) : m.arg;
            Pos p      = openpos(g, &d, trump, t->seat, m.alone, round1);
            val = utility(g, us, us, ddpoints(t->sv, &p, 0, m.alone));
        } break;
        case PH_DISCARD: {
            Pos p = openpos(g, &d, g->trump, g->maker, g->alone, 0);
            p.hand[t->seat] &= ~bit(m.arg);
            val = utility(g, us, teamof(g->maker),
                          ddpoints(t->sv, &p, 0, g->alone));
        } break;
        case PH_PLAY: {
            Pos  p = nowpos(g, &d);
            b32  done;
            i32  got = dplay(&p, m.arg, &done);
            i32  pts = ddpoints(t->sv, &p, makertricks(g) + got, g->alone);
            val = utility(g, us, teamof(g->maker), pts);
        } break;
        }
        t->sum[i] += val;
    }
    t->samples++;
}

// Work on the decision for about budget solver nodes; 1 when decided.
static b32 thinkstep(Think *t, i64 budget)
{
    if (t->done) {
        return 1;
    }
    i64 start = t->sv->nodes;
    while (t->samples<t->want && t->sv->nodes-start<budget) {
        thinksample(t);
    }
    if (t->samples >= t->want) {
        thinkfinish(t);
    }
    return t->done;
}

// Decide at once, for tests and duels.
static Move decide(Game *g, i32 seat, Character const *ch, Arena ai)
{
    Think t;
    thinkstart(&t, g, seat, ch, &ai);
    while (!thinkstep(&t, I64MAX)) {}
    return t.result;
}
