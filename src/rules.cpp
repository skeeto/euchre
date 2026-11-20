// Rules: the state of a game of Euchre and the moves that change it
//
// Seats go clockwise, the direction of play: South (you), West, North,
// East. Partners sit across, so a seat's partner is s^2 and its team is
// s&1. A Game is plain data without pointers, saved after every move,
// and every decision, a computer's or yours, is one Move given to apply.
// The pauses a person needs, a finished trick resting on the table, the
// hand's summary, are phases too, left by MV_NEXT, so a saved game
// resumes exactly where it was.

enum : i32 {
    SOUTH,
    WEST,
    NORTH,
    EAST,
};

enum : i32 {
    PH_BID1,     // round one: order up the turned card, or pass
    PH_BID2,     // round two: name another suit, or pass
    PH_DISCARD,  // the dealer, having picked up, discards
    PH_PLAY,     // a seat plays to the trick
    PH_TRICK,    // the trick is complete and rests on the table
    PH_PASSED,   // everyone passed twice: the hand is thrown in
    PH_SCORE,    // the hand is over and scored
    PH_OVER,     // the game is over
    PH_COUNT,
};

enum : i32 {
    MV_PASS,
    MV_ORDER,    // round one: the turned card's suit is trump
    MV_CALL,     // round two: arg is the suit named
    MV_DISCARD,  // arg is the card
    MV_PLAY,     // arg is the card
    MV_NEXT,     // leave a pause phase
};

struct Move {
    i32 kind;
    i32 arg;
    b32 alone;  // MV_ORDER and MV_CALL: the maker plays alone
};

struct Rules {
    i32 target = 10;  // points to win: 5, 7 or 10
    b32 stick  = 1;   // the dealer must name a suit in round two
};

struct Game {
    Rules rules;
    u64   seed;         // shuffles the next deal
    i32   phase;
    i32   dealer;
    i32   turn;         // the seat to act, -1 in pause phases
    i32   handno;       // hands dealt this game, counting from 1
    i32   score[2];     // by team
    u32   hand[4];
    u32   kitty;        // the undealt four: the up-card until taken, the discard after
    i32   upcard;
    i32   trump;        // NOSUIT while bidding
    i32   maker;        // -1 while bidding
    b32   alone;
    i32   discard;      // NOCARD if none
    i32   nbids;
    i32   bids[8];      // NOSUIT for a pass, else the suit made trump
    i32   nplays;
    i32   pcard[20];    // cards played this hand, in order
    i32   pseat[20];
    i32   ntricks;      // tricks gathered
    i32   tricks[4];    // tricks taken, by seat
    i32   winner;       // PH_TRICK and after: the last trick's taker
    i32   points;       // PH_SCORE and PH_OVER: the hand's points
    i32   pointsto;     // the team that scored them
};

static i32 partner(i32 s) { return s ^ 2; }
static i32 teamof(i32 s)  { return s & 1; }

static Str seatname(i32 s)
{
    static Str names[] = {"South", "West", "North", "East"};
    return names[s];
}

// The maker's partner when the maker plays alone, else -1.
static i32 sittingout(Game *g)
{
    return g->alone && g->maker>=0 ? partner(g->maker) : -1;
}

static i32 nextseat(Game *g, i32 s)
{
    s = (s + 1) & 3;
    return s==sittingout(g) ? (s+1)&3 : s;
}

static i32 trickcards(Game *g)  // cards in a full trick
{
    return g->alone ? 3 : 4;
}

// Index in pcard of the current trick's lead.
static i32 trickbase(Game *g)
{
    return g->ntricks * trickcards(g);
}

static i32 ledsuit(Game *g)  // NOSUIT before the lead
{
    i32 b = trickbase(g);
    return g->nplays>b ? effsuit(g->pcard[b], g->trump) : NOSUIT;
}

// The seat taking the trick whose plays start at index b.
static i32 takerof(Game *g, i32 b)
{
    i32 led  = effsuit(g->pcard[b], g->trump);
    i32 best = b;
    for (i32 i = b+1; i < b+trickcards(g); i++) {
        if (power(g->pcard[i], g->trump, led) > power(g->pcard[best], g->trump, led)) {
            best = i;
        }
    }
    return g->pseat[best];
}

static u32 playedmask(Game *g)
{
    u32 m = 0;
    for (i32 i = 0; i < g->nplays; i++) {
        m |= bit(g->pcard[i]);
    }
    return m;
}

// Cards seat s may play now, given its hand: follow suit if able.
static u32 followable(Game *g, u32 hand)
{
    i32 led = ledsuit(g);
    if (led == NOSUIT) {
        return hand;
    }
    u32 f = hand & suitmask(led, g->trump);
    return f ? f : hand;
}

static void deal(Game *g)
{
    i32 deck[NCARDS];
    for (i32 i = 0; i < NCARDS; i++) {
        deck[i] = i;
    }
    for (i32 i = NCARDS-1; i > 0; i--) {
        i32 j = randint(&g->seed, i+1);
        i32 t = deck[i];
        deck[i] = deck[j];
        deck[j] = t;
    }

    g->phase   = PH_BID1;
    g->turn    = (g->dealer + 1) & 3;
    g->handno++;
    g->trump   = NOSUIT;
    g->maker   = -1;
    g->alone   = 0;
    g->discard = NOCARD;
    g->nbids   = 0;
    g->nplays  = 0;
    g->ntricks = 0;
    g->winner  = -1;
    g->points  = 0;
    g->pointsto = 0;
    for (i32 s = 0; s < 4; s++) {
        g->hand[s]   = 0;
        g->tricks[s] = 0;
    }
    for (i32 i = 0; i < 20; i++) {
        g->hand[(g->dealer + 1 + i/5) & 3] |= bit(deck[i]);
    }
    g->kitty  = 0;
    for (i32 i = 20; i < NCARDS; i++) {
        g->kitty |= bit(deck[i]);
    }
    g->upcard = deck[20];
}

static void newgame(Game *g, Rules rules, u64 seed)
{
    *g = {};
    g->rules  = rules;
    g->seed   = seed;
    g->dealer = randint(&g->seed, 4);
    deal(g);
}

static void startplay(Game *g)
{
    g->phase = PH_PLAY;
    g->turn  = nextseat(g, g->dealer);
}

static void maketrump(Game *g, i32 suit, b32 alone)
{
    g->bids[g->nbids++] = suit;
    g->trump = suit;
    g->maker = g->turn;
    g->alone = alone;
}

static b32 legal(Game *g, Move m)
{
    switch (g->phase) {
    case PH_BID1:
        return m.kind==MV_PASS || m.kind==MV_ORDER;
    case PH_BID2:
        if (m.kind == MV_PASS) {
            return !(g->rules.stick && g->turn==g->dealer);
        }
        return m.kind==MV_CALL && m.arg>=0 && m.arg<4 && m.arg!=suitof(g->upcard);
    case PH_DISCARD:
        return m.kind==MV_DISCARD && m.arg>=0 && m.arg<NCARDS &&
               has(g->hand[g->dealer], m.arg);
    case PH_PLAY:
        return m.kind==MV_PLAY && m.arg>=0 && m.arg<NCARDS &&
               has(followable(g, g->hand[g->turn]), m.arg);
    case PH_TRICK:
    case PH_PASSED:
    case PH_SCORE:
    case PH_OVER:
        return m.kind == MV_NEXT;
    }
    return 0;
}

// Every legal move now, at most 12; returns the count. Each bid comes
// with and without alone.
static i32 legalmoves(Game *g, Move *out)
{
    i32 n = 0;
    switch (g->phase) {
    case PH_BID1:
        out[n++] = {MV_PASS, 0, 0};
        out[n++] = {MV_ORDER, 0, 0};
        out[n++] = {MV_ORDER, 0, 1};
        break;
    case PH_BID2:
        if (legal(g, {MV_PASS, 0, 0})) {
            out[n++] = {MV_PASS, 0, 0};
        }
        for (i32 s = 0; s < 4; s++) {
            if (s != suitof(g->upcard)) {
                out[n++] = {MV_CALL, s, 0};
                out[n++] = {MV_CALL, s, 1};
            }
        }
        break;
    case PH_DISCARD:
        for (u32 m = g->hand[g->dealer]; m; m &= m-1) {
            out[n++] = {MV_DISCARD, lowest(m), 0};
        }
        break;
    case PH_PLAY:
        for (u32 m = followable(g, g->hand[g->turn]); m; m &= m-1) {
            out[n++] = {MV_PLAY, lowest(m), 0};
        }
        break;
    default:
        out[n++] = {MV_NEXT, 0, 0};
    }
    return n;
}

static void scorehand(Game *g)
{
    i32 mt = teamof(g->maker);
    i32 k  = g->tricks[g->maker] + g->tricks[partner(g->maker)];
    if (k == 5) {
        g->points = g->alone ? 4 : 2;
    } else if (k >= 3) {
        g->points = 1;
    } else {
        g->points = 2;
        mt ^= 1;
    }
    g->pointsto = mt;
    g->score[mt] += g->points;
    g->phase = PH_SCORE;
    g->turn  = -1;
}

static void apply(Game *g, Move m)
{
    assert(legal(g, m));
    switch (g->phase) {
    case PH_BID1:
        if (m.kind == MV_PASS) {
            g->bids[g->nbids++] = NOSUIT;
            if (g->nbids == 4) {
                g->phase = PH_BID2;
            }
            g->turn = (g->turn + 1) & 3;
            return;
        }
        maketrump(g, suitof(g->upcard), m.alone);
        if (sittingout(g) == g->dealer) {
            startplay(g);  // the dealer sits out and takes nothing
        } else {
            g->hand[g->dealer] |= bit(g->upcard);
            g->kitty &= ~bit(g->upcard);
            g->phase = PH_DISCARD;
            g->turn  = g->dealer;
        }
        return;

    case PH_BID2:
        if (m.kind == MV_PASS) {
            g->bids[g->nbids++] = NOSUIT;
            if (g->nbids == 8) {
                g->phase = PH_PASSED;
                g->turn  = -1;
            } else {
                g->turn = (g->turn + 1) & 3;
            }
            return;
        }
        maketrump(g, m.arg, m.alone);
        startplay(g);
        return;

    case PH_DISCARD:
        g->hand[g->dealer] &= ~bit(m.arg);
        g->kitty |= bit(m.arg);
        g->discard = m.arg;
        startplay(g);
        return;

    case PH_PLAY:
        g->hand[g->turn] &= ~bit(m.arg);
        g->pcard[g->nplays] = m.arg;
        g->pseat[g->nplays] = g->turn;
        g->nplays++;
        if (g->nplays - trickbase(g) == trickcards(g)) {
            g->winner = takerof(g, trickbase(g));
            g->phase  = PH_TRICK;
            g->turn   = -1;
        } else {
            g->turn = nextseat(g, g->turn);
        }
        return;

    case PH_TRICK:
        g->tricks[g->winner]++;
        g->ntricks++;
        if (g->ntricks == 5) {
            scorehand(g);
        } else {
            g->phase = PH_PLAY;
            g->turn  = g->winner;
        }
        return;

    case PH_PASSED:
        g->dealer = (g->dealer + 1) & 3;
        deal(g);
        return;

    case PH_SCORE:
        if (g->score[0]>=g->rules.target || g->score[1]>=g->rules.target) {
            g->phase = PH_OVER;
        } else {
            g->dealer = (g->dealer + 1) & 3;
            deal(g);
        }
        return;

    case PH_OVER:
        newgame(g, g->rules, g->seed);
        return;
    }
}

// The team that won a finished game.
static i32 gamewinner(Game *g)
{
    return g->score[1] > g->score[0];
}

// Whether g is a state that play could reach: every card in exactly one
// place, the counts and the history consistent with the phase. Saved
// games are checked with it before they are trusted.
static b32 validgame(Game *g)
{
    Rules *r = &g->rules;
    if ((r->target!=5 && r->target!=7 && r->target!=10) ||
        (r->stick!=0 && r->stick!=1)) {
        return 0;
    }
    if (g->phase<0 || g->phase>=PH_COUNT || g->dealer<0 || g->dealer>3 ||
        g->handno<1 || g->upcard<0 || g->upcard>=NCARDS ||
        g->nbids<0 || g->nbids>8 || g->nplays<0 || g->nplays>20 ||
        g->ntricks<0 || g->ntricks>5 || (g->alone!=0 && g->alone!=1)) {
        return 0;
    }
    for (i32 t = 0; t < 2; t++) {
        if (g->score[t]<0 || g->score[t]>=r->target+4) {
            return 0;
        }
    }
    if (g->phase!=PH_OVER && (g->score[0]>=r->target || g->score[1]>=r->target)) {
        if (g->phase != PH_SCORE) {
            return 0;
        }
    }

    // Every card in exactly one place
    u32 seen = 0;
    i32 total = 0;
    u32 places[6] = {g->hand[0], g->hand[1], g->hand[2], g->hand[3], g->kitty, 0};
    for (i32 i = 0; i < g->nplays; i++) {
        if (g->pcard[i]<0 || g->pcard[i]>=NCARDS || g->pseat[i]<0 || g->pseat[i]>3) {
            return 0;
        }
        places[5] |= bit(g->pcard[i]);
    }
    for (i32 i = 0; i < 6; i++) {
        if (places[i] & ~ALLCARDS) {
            return 0;
        }
        seen  |= places[i];
        total += popcount(places[i]);
    }
    if (seen!=ALLCARDS || total!=NCARDS || popcount(places[5])!=g->nplays) {
        return 0;
    }

    // Bidding
    b32 bidding = g->phase==PH_BID1 || g->phase==PH_BID2 || g->phase==PH_PASSED;
    i32 upsuit  = suitof(g->upcard);
    for (i32 i = 0; i < g->nbids; i++) {
        b32 last = i == g->nbids-1;
        i32 b    = g->bids[i];
        if (b!=NOSUIT && (bidding || !last || b<0 || b>3)) {
            return 0;
        }
        if (b!=NOSUIT && (i<4 ? b!=upsuit : b==upsuit)) {
            return 0;
        }
    }
    if (bidding) {
        b32 bid1 = g->phase == PH_BID1;
        i32 lo   = bid1 ? 0 : g->phase==PH_BID2 ? 4 : 8;
        i32 hi   = bid1 ? 3 : g->phase==PH_BID2 ? 7 : 8;
        if (g->nbids<lo || g->nbids>hi || g->trump!=NOSUIT || g->maker!=-1 ||
            g->alone || g->nplays || g->ntricks || g->discard!=NOCARD ||
            !has(g->kitty, g->upcard) || popcount(g->kitty)!=4) {
            return 0;
        }
        if (g->phase==PH_PASSED ? (g->turn!=-1 || r->stick)
                                : g->turn!=((g->dealer+1+g->nbids)&3)) {
            return 0;
        }
        if (r->stick && g->nbids==8) {
            return 0;
        }
        for (i32 s = 0; s < 4; s++) {
            if (popcount(g->hand[s])!=5 || g->tricks[s]) {
                return 0;
            }
        }
        return 1;
    }

    // Trump is made: by the last bidder
    if (g->nbids<1 || g->bids[g->nbids-1]!=g->trump) {
        return 0;
    }
    if (g->maker != ((g->dealer + g->nbids) & 3)) {
        return 0;
    }
    b32 round1   = g->nbids <= 4;
    i32 out      = sittingout(g);
    b32 pickedup = round1 && out!=g->dealer;
    if (g->phase == PH_DISCARD) {
        if (!pickedup || g->turn!=g->dealer || g->nplays || g->ntricks ||
            g->discard!=NOCARD || !has(g->hand[g->dealer], g->upcard) ||
            popcount(g->kitty)!=3 || popcount(g->hand[g->dealer])!=6) {
            return 0;
        }
        for (i32 s = 0; s < 4; s++) {
            if ((s!=g->dealer && popcount(g->hand[s])!=5) || g->tricks[s]) {
                return 0;
            }
        }
        return 1;
    }
    if (popcount(g->kitty) != 4) {
        return 0;
    }
    if (pickedup) {
        if (g->discard==NOCARD || !has(g->kitty, g->discard) ||
            has(g->kitty, g->upcard) == (g->discard!=g->upcard)) {
            return 0;
        }
    } else if (g->discard!=NOCARD || !has(g->kitty, g->upcard)) {
        return 0;
    }

    // Replay the tricks: seats in turn, suit followed where the cards
    // still to come show it could be, and the takers as recorded.
    i32 per    = trickcards(g);
    i32 full   = g->nplays / per;
    i32 inside = g->nplays % per;
    i32 want   = g->phase==PH_TRICK ? full-1 : full;
    if (g->ntricks != want || g->ntricks<0) {
        return 0;
    }
    switch (g->phase) {
    case PH_PLAY:  if (inside || g->ntricks<5) break; return 0;
    case PH_TRICK: if (!inside && full>=1) break; return 0;
    case PH_SCORE:
    case PH_OVER:  if (!inside && full==5) break; return 0;
    default:       return 0;
    }
    i32 tricks[4] = {};
    u32 later[4]  = {g->hand[0], g->hand[1], g->hand[2], g->hand[3]};
    for (i32 i = 0; i < g->nplays; i++) {
        later[g->pseat[i]] |= bit(g->pcard[i]);
    }
    i32 lead = nextseat(g, g->dealer);
    i32 next = lead;  // the seat to play after the history
    for (i32 b = 0; b < g->nplays; b += per) {
        i32 n   = g->nplays-b < per ? g->nplays-b : per;
        i32 led = effsuit(g->pcard[b], g->trump);
        i32 s   = lead;
        for (i32 i = b; i < b+n; i++) {
            if (g->pseat[i] != s) {
                return 0;
            }
            i32 c = g->pcard[i];
            later[s] &= ~bit(c);
            if (i>b && effsuit(c, g->trump)!=led &&
                (later[s] & suitmask(led, g->trump))) {
                return 0;  // a renege
            }
            s = nextseat(g, s);
        }
        next = s;
        if (n == per) {
            lead = next = takerof(g, b);
            if (b/per < g->ntricks) {
                tricks[lead]++;
            }
        }
    }
    for (i32 s = 0; s < 4; s++) {
        if (tricks[s] != g->tricks[s]) {
            return 0;
        }
        i32 have = popcount(g->hand[s]);
        i32 need = 5;
        if (s != out) {
            need = 5 - g->ntricks;
            for (i32 i = trickbase(g); i < g->nplays; i++) {
                need -= g->pseat[i] == s;
            }
        }
        if (have != need) {
            return 0;
        }
    }
    if (out>=0 && tricks[out]) {
        return 0;
    }

    switch (g->phase) {
    case PH_PLAY:
        return g->turn == next;
    case PH_TRICK:
        return g->turn==-1 && g->winner==lead;
    case PH_SCORE:
    case PH_OVER: {
        if (g->turn!=-1 || g->winner!=lead) {
            return 0;
        }
        i32 k  = tricks[g->maker] + tricks[partner(g->maker)];
        i32 mt = teamof(g->maker);
        i32 pts = k==5 ? (g->alone ? 4 : 2) : k>=3 ? 1 : 2;
        i32 to  = k>=3 ? mt : mt^1;
        if (g->points!=pts || g->pointsto!=to) {
            return 0;
        }
        b32 done = g->score[0]>=r->target || g->score[1]>=r->target;
        return g->phase==PH_SCORE || done;
    }
    }
    return 0;
}
