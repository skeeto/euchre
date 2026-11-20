// UI: the app's state, its flow and pacing, and its input
//
// Every decision is committed through commit, which applies it, keeps
// the statistics, starts its sound and its animation, and saves at
// once, so the game on disk is never behind what's on screen. Computer
// players wait a think time from their character's pace, and nothing
// advances while cards are still moving. The pauses people need, a
// finished trick resting, the hand's summary, are phases of the game.
//
// Hit targets are recorded as each frame is drawn (scene.cpp,
// panels.cpp), so a tap lands on what was on screen.

enum : i32 {
    IN_DOWN,
    IN_UP,
    IN_MOVE,
    IN_LEAVE,
    IN_KEY,
    IN_FOCUS,   // focus lost
    IN_WINDOW,  // the window moved or resized
};

enum : i32 {
    KEY_ENTER = 13,
    KEY_ESC   = 27,
    KEY_LEFT  = 256,
    KEY_RIGHT,
};

enum : i32 {
    MINWIN_W = 480,
    MINWIN_H = 420,
};

struct Input {
    i32     kind;
    f32     x, y;
    i32     key;
    b32     touch;
    WinGeom win;
};

struct Options {
    u64 seed;
    b32 seeded;
    b32 autoplay;
    i32 until = -1;  // a phase to play on to, then hold
    Str open;        // an overlay to open
    b32 headless;
};

enum : i32 {
    OV_NONE,
    OV_MENU,
    OV_PLAYERS,
    OV_RULES,
    OV_STATS,
    OV_HELP,
    OV_WELCOME,
    OV_LASTTRICK,
};

enum : i32 {
    ID_NONE,
    ID_CARD,                      // + card
    ID_PASS     = ID_CARD + NCARDS,
    ID_ORDER,
    ID_CALL,                      // + suit
    ID_ALONE    = ID_CALL + 4,
    ID_HINT,
    ID_MENU,
    ID_CONTINUE,
    ID_TRICKS,
    ID_SCRIM,
    ID_CLOSE,
    ID_NEWGAME,
    ID_PLAYERS,
    ID_RULES,
    ID_SOUND,
    ID_SPEED,
    ID_HELP,
    ID_STATS,
    ID_QUIT,
    ID_SEAT,                      // + seat
    ID_CHAR     = ID_SEAT + 4,    // + character
    ID_STICK    = ID_CHAR + 16,
    ID_TARGET,                    // + 0, 1, 2 for 5, 7, 10
    ID_HELPBACK = ID_TARGET + 3,
    ID_HELPNEXT,
    ID_HOWTO,
    ID_DEALIN,
    ID_TABLE,
    ID_PANEL,                     // a panel's body: swallows taps
};

struct Hit {
    Rect r;
    i32  id;
};

struct Sprite {
    Place from;
    Place to;
    i64   t0;      // start of the move
    i32   dur;     // ms
    b32   placed;  // has a place at all
};

enum : i32 {
    MAXHITS  = 256,
    NHELP    = 5,
};

struct App {
    Os       *os;
    Arena     perm;
    Arena     frame;
    Arena     ftmp;          // the frame's temporaries, past its mesh
    Arena     ai;
    Arena     scratch;
    Options   opt;
    Font     *font;
    Clip     *clips;

    Saved     sv;
    Game     *g;
    i64       now;

    // Presentation
    View      view;
    Layout    lay;
    b32       laid;          // lay holds the current view's layout
    Sprite    spr[NCARDS];
    i32       delay[NCARDS]; // ms before a card's next move starts
    i32       movedur[NCARDS];
    i64       animend;       // last moving sprite's end
    b32       pendingdeal;   // gather every card to the deck first
    i64       fontwake;      // a pending rebake's time
    i64       drawnat;       // when the last frame was drawn

    // Flow
    Think     think;
    i64       decideat;      // when a computer's move may be committed
    i64       pauseuntil;    // when a pause phase ends on its own
    b32       frozen;        // --until reached: hold everything
    Think     hint;
    b32       hinting;

    // Input
    Hit       hits[MAXHITS];
    i32       nhits;
    i32       hover;
    i32       pressed;
    b32       alone;         // the Alone toggle for your bid
    i32       overlay;
    i32       helppage;
    i32       seattab;       // the seat being picked in Players
    Str       flash;
    i64       flashuntil;
    u8        flashbuf[64];

    // For the platform
    i32       cursor;        // 0 arrow, 1 pointing hand
    b32       quit;
    b32       redraw;
    WinGeom   win;
};

static Character const *seatchar(App *a, i32 seat)
{
    i32 i = a->sv.table[seat];
    return characters + (i>=0 && i<NCHARACTERS ? i : 0);
}

static Str seatlabel(App *a, i32 seat)
{
    return seat==SOUTH ? Str("You") : seatchar(a, seat)->name;
}

static f32 speedfactor(App *a)
{
    if (a->opt.autoplay || a->opt.headless) {
        return 0.05f;
    }
    static f32 const f[] = {1.5f, 1.0f, 0.6f};
    return f[a->sv.set.speed];
}

static void sound(App *a, i32 sfx)
{
    if (a->sv.set.sound && !a->opt.headless) {
        os_play(a->os, sfx);
    }
}

static void save(App *a)
{
    if (a->opt.headless) {
        return;  // shots and icons leave the real save alone
    }
    Arena t = a->scratch;
    a->sv.win = a->win;
    os_save(a->os, t, savetext(&t, &a->sv));
}

static void setflash(App *a, Str s)
{
    i32 n = (i32)minf((f32)s.len, (f32)sizeof(a->flashbuf));
    copybytes(a->flashbuf, s.data, n);
    a->flash      = {a->flashbuf, n};
    a->flashuntil = a->now + 1800;
    a->redraw     = 1;
}

static void dealanimation(App *a);

static void startgame(App *a)
{
    Game *g = a->g;
    u64 seed = hashmix(g->seed ? g->seed : a->opt.seed, (u64)a->now + 77);
    newgame(g, a->sv.set.rules, seed);
    a->sv.hasgame = 1;
    for (i32 s = 1; s < 4; s++) {
        a->sv.table[s] = a->sv.set.seats[s];
    }
    a->sv.table[0] = -1;
    a->think   = {};
    a->hinting = 0;
    a->alone   = 0;
    dealanimation(a);
    sound(a, SFX_SHUFFLE);
    save(a);
    a->redraw = 1;
}

// Keep the statistics as a hand or a game ends.
static void tally(App *a, Game *prev)
{
    Game  *g  = a->g;
    Stats *st = &a->sv.stats;
    if (prev->phase==PH_TRICK && g->phase==PH_SCORE) {
        b32 ours  = teamof(g->maker) == 0;
        i32 k     = g->tricks[g->maker] + g->tricks[partner(g->maker)];
        st->hands++;
        st->made     += ours;
        st->marches  += ours && k==5;
        st->euchred  += ours && k<3;
        st->euchres  += !ours && k<3;
        st->loners   += g->maker==SOUTH && g->alone;
        st->lonewins += g->maker==SOUTH && g->alone && k==5;
    }
    if (prev->phase==PH_SCORE && g->phase==PH_OVER) {
        st->games++;
        st->wins += gamewinner(g) == 0;
    }
}

static void commit(App *a, Move m)
{
    Game *g    = a->g;
    Game  prev = *g;
    if (g->phase==PH_OVER && m.kind==MV_NEXT) {
        startgame(a);
        return;
    }
    if (!legal(g, m)) {
        return;
    }
    apply(g, m);
    tally(a, &prev);

    switch (m.kind) {
    case MV_PASS:
        sound(a, SFX_KNOCK);
        if (g->phase == PH_BID2) {
            sound(a, SFX_FLIP);
        }
        break;
    case MV_ORDER:
    case MV_CALL:    sound(a, SFX_CALL); break;
    case MV_DISCARD: sound(a, SFX_DEAL); break;
    case MV_PLAY:    sound(a, SFX_PLAY); break;
    case MV_NEXT:
        if (prev.phase == PH_TRICK) {
            sound(a, SFX_SWEEP);
            i32 per = trickcards(&prev);
            for (i32 i = 0; i < per; i++) {
                i32 c = prev.pcard[trickbase(&prev) + i];
                a->movedur[c] = 380;
            }
        }
        break;
    }
    if (g->phase == PH_SCORE) {
        if (g->pointsto == 0) {
            sound(a, SFX_POINT);
        } else if (teamof(g->maker) == 0) {
            sound(a, SFX_EUCHRE);
        }
    }
    if (g->phase == PH_OVER) {
        sound(a, gamewinner(g)==0 ? SFX_WIN : SFX_LOSE);
    }
    if (g->handno != prev.handno) {
        dealanimation(a);
        sound(a, SFX_SHUFFLE);
    }
    a->think      = {};
    a->hinting    = 0;
    a->alone      = 0;
    a->pauseuntil = 0;
    a->decideat   = 0;
    a->redraw     = 1;
    save(a);
}

// Whether the seat to act is played by the computer right now.
static b32 automatic(App *a, i32 seat)
{
    if (seat != SOUTH) {
        return 1;
    }
    return a->opt.autoplay || (a->opt.until>=0 && !a->frozen);
}

static b32 animating(App *a)
{
    return a->now < a->animend;
}

// The --until phase is reached when the game sits in it waiting on you.
static b32 reached(App *a)
{
    Game *g = a->g;
    if (a->opt.until<0 || g->phase!=a->opt.until) {
        return 0;
    }
    return g->turn==SOUTH || g->turn==-1;
}

// Advance the game as far as it may go now; returns ms until it next
// needs a look, or -1.
static i32 step(App *a)
{
    Game *g = a->g;
    if (a->frozen || (a->overlay && a->overlay!=OV_LASTTRICK)) {
        return -1;
    }
    if (reached(a)) {
        a->frozen = 1;
        return -1;
    }
    if (!a->opt.headless && (animating(a) || a->redraw)) {
        return 0;
    }
    f32 sf = speedfactor(a);

    // Pause phases end on their own, except the summary and game over
    if (g->turn < 0) {
        i32 hold = 0;
        switch (g->phase) {
        case PH_TRICK:  hold = 1100; break;
        case PH_PASSED: hold = 1700; break;
        case PH_SCORE:
        case PH_OVER:
            if (!a->opt.autoplay && !(a->opt.until>=0)) return -1;
            hold = 1500;
            break;
        }
        if (!a->pauseuntil) {
            a->pauseuntil = a->now + (i64)((f32)hold*sf);
        }
        if (a->now >= a->pauseuntil) {
            commit(a, {MV_NEXT, 0, 0});
            return 0;
        }
        return (i32)(a->pauseuntil - a->now);
    }

    i32 seat = g->turn;
    if (!automatic(a, seat)) {
        if (a->hinting) {
            if (!thinkstep(&a->hint, 40000)) {
                return 8;
            }
            a->redraw = 1;
            return 0;
        }
        return -1;
    }
    Character const *ch = seat==SOUTH ? characters : seatchar(a, seat);
    if (!a->think.active) {
        Arena ai = a->ai;  // one job at a time holds it until done
        thinkstart(&a->think, g, seat, ch, &ai);
        u64 r = a->think.rng ^ 0x55;
        f32 jitter = 0.8f + 0.4f*randf(&r);
        i32 pace = ch->pace;
        if (g->phase==PH_PLAY && popcount(followable(g, g->hand[seat]))==1) {
            pace /= 2;  // nothing to think about
        }
        a->decideat = a->now + (i64)((f32)pace*jitter*sf);
        a->redraw = 1;  // "thinking" shows
    }
    if (!thinkstep(&a->think, 40000)) {
        return 8;
    }
    if (a->now < a->decideat) {
        return (i32)(a->decideat - a->now);
    }
    commit(a, a->think.result);
    return 0;
}

static void starthint(App *a)
{
    Game *g = a->g;
    if (g->turn!=SOUTH || a->hinting || g->phase>=PH_TRICK) {
        return;
    }
    Arena ai = a->ai;
    thinkstart(&a->hint, g, SOUTH, characters, &ai);
    a->hinting = 1;
    a->redraw  = 1;
}

// The move a hint suggests, once ready.
static b32 hintready(App *a, Move *m)
{
    if (!a->hinting || !a->hint.done) {
        return 0;
    }
    *m = a->hint.result;
    return 1;
}

static void closeoverlay(App *a)
{
    if (a->overlay == OV_WELCOME) {
        a->sv.set.welcomed = 1;
        save(a);
    }
    a->overlay = OV_NONE;
    a->redraw  = 1;
}

static i32 hitat(App *a, f32 x, f32 y)
{
    for (i32 i = a->nhits-1; i >= 0; i--) {
        if (inrect(a->hits[i].r, x, y)) {
            return a->hits[i].id;
        }
    }
    return ID_NONE;
}

static void bid(App *a, Move m)
{
    m.alone = a->alone;
    if (legal(a->g, m) && a->g->turn==SOUTH && !automatic(a, SOUTH)) {
        commit(a, m);
    }
}

static void tapcard(App *a, i32 c)
{
    Game *g = a->g;
    if (g->phase == PH_TRICK) {
        commit(a, {MV_NEXT, 0, 0});
        return;
    }
    if (g->turn!=SOUTH || automatic(a, SOUTH) || !has(g->hand[SOUTH], c)) {
        return;
    }
    if (g->phase == PH_DISCARD) {
        commit(a, {MV_DISCARD, c, 0});
    } else if (g->phase == PH_PLAY) {
        if (has(followable(g, g->hand[SOUTH]), c)) {
            commit(a, {MV_PLAY, c, 0});
        } else {
            Arena t = a->scratch;
            Str   s = concat(&t, "You must follow ", suitsym(ledsuit(g)));
            setflash(a, s);
        }
    }
}

static void click(App *a, i32 id)
{
    Game *g = a->g;
    a->redraw = 1;
    if (id>=ID_CARD && id<ID_CARD+NCARDS) {
        tapcard(a, id - ID_CARD);
        return;
    }
    if (id>=ID_CALL && id<ID_CALL+4) {
        bid(a, {MV_CALL, id - ID_CALL, 0});
        return;
    }
    if (id>=ID_SEAT && id<ID_SEAT+4) {
        a->seattab = id - ID_SEAT;
        return;
    }
    if (id>=ID_CHAR && id<ID_CHAR+NCHARACTERS) {
        a->sv.set.seats[a->seattab] = id - ID_CHAR;
        save(a);
        return;
    }
    if (id>=ID_TARGET && id<ID_TARGET+3) {
        static i32 const targets[] = {5, 7, 10};
        a->sv.set.rules.target = targets[id - ID_TARGET];
        save(a);
        return;
    }
    switch (id) {
    case ID_PASS:     bid(a, {MV_PASS, 0, 0}); break;
    case ID_ORDER:    bid(a, {MV_ORDER, 0, 0}); break;
    case ID_ALONE:    a->alone = !a->alone; break;
    case ID_HINT:     starthint(a); break;
    case ID_MENU:     a->overlay = a->overlay==OV_MENU ? OV_NONE : OV_MENU; break;
    case ID_TABLE:
        if (g->phase == PH_TRICK) commit(a, {MV_NEXT, 0, 0});
        break;
    case ID_CONTINUE:
        if (g->phase==PH_SCORE || g->phase==PH_OVER) {
            if (g->phase == PH_OVER) {
                startgame(a);
            } else {
                commit(a, {MV_NEXT, 0, 0});
            }
        }
        break;
    case ID_TRICKS:
        if (g->ntricks>0 && g->phase>=PH_PLAY && g->phase!=PH_PASSED) {
            a->overlay = OV_LASTTRICK;
        }
        break;
    case ID_SCRIM:
    case ID_CLOSE:    closeoverlay(a); break;
    case ID_NEWGAME:  a->overlay = OV_NONE; startgame(a); break;
    case ID_PLAYERS:  a->overlay = OV_PLAYERS; a->seattab = NORTH; break;
    case ID_RULES:    a->overlay = OV_RULES; break;
    case ID_STATS:    a->overlay = OV_STATS; break;
    case ID_HELP:     a->overlay = OV_HELP; a->helppage = 0; break;
    case ID_HOWTO:
        a->sv.set.welcomed = 1;
        save(a);
        a->overlay  = OV_HELP;
        a->helppage = 0;
        break;
    case ID_DEALIN:   closeoverlay(a); break;
    case ID_SOUND:
        a->sv.set.sound = !a->sv.set.sound;
        save(a);
        break;
    case ID_SPEED:
        a->sv.set.speed = (a->sv.set.speed + 1) % 3;
        save(a);
        break;
    case ID_STICK:
        a->sv.set.rules.stick = !a->sv.set.rules.stick;
        save(a);
        break;
    case ID_HELPBACK: if (a->helppage > 0) a->helppage--; break;
    case ID_HELPNEXT:
        if (a->helppage+1 < NHELP) {
            a->helppage++;
        } else {
            closeoverlay(a);
        }
        break;
    case ID_QUIT:     a->quit = 1; break;
    }
}

static void key(App *a, i32 k)
{
    Game *g = a->g;
    a->redraw = 1;
    if (k == KEY_ESC) {
        if (a->overlay) {
            closeoverlay(a);
        } else {
            a->overlay = OV_MENU;
        }
        return;
    }
    if (a->overlay == OV_HELP) {
        if (k==KEY_LEFT) click(a, ID_HELPBACK);
        if (k==KEY_RIGHT || k==KEY_ENTER || k==' ') click(a, ID_HELPNEXT);
        return;
    }
    if (a->overlay) {
        if (k==KEY_ENTER || k==' ') closeoverlay(a);
        return;
    }
    switch (k) {
    case 'm': click(a, ID_SOUND); return;
    case '?': click(a, ID_HINT); return;
    case KEY_ENTER:
    case ' ':
        if (g->phase==PH_SCORE || g->phase==PH_OVER) click(a, ID_CONTINUE);
        if (g->phase == PH_TRICK) click(a, ID_TABLE);
        return;
    }
    if (g->turn != SOUTH) {
        return;
    }
    if (k>='1' && k<='6') {
        i32 cs[6];
        i32 n = sortcards(g->hand[SOUTH], g->trump, cs);
        if (k-'1' < n) tapcard(a, cs[k-'1']);
        return;
    }
    switch (k) {
    case 'p': click(a, ID_PASS); break;
    case 'o': click(a, ID_ORDER); break;
    case 'a': click(a, ID_ALONE); break;
    case 'c': click(a, ID_CALL+CLUBS); break;
    case 's': click(a, ID_CALL+SPADES); break;
    case 'd': click(a, ID_CALL+DIAMONDS); break;
    case 'h': click(a, ID_CALL+HEARTS); break;
    }
}

static b32 clickable(i32 id)
{
    return id!=ID_NONE && id!=ID_SCRIM && id!=ID_PANEL && id!=ID_TABLE;
}

static void appevent(App *a, Input in)
{
    switch (in.kind) {
    case IN_DOWN:
        a->pressed = hitat(a, in.x, in.y);
        if (a->pressed==ID_TABLE || a->pressed==ID_SCRIM) {
            click(a, a->pressed);  // at once: a tap anywhere hurries
            a->pressed = ID_NONE;
        }
        break;
    case IN_UP: {
        i32 id = hitat(a, in.x, in.y);
        if (id!=ID_NONE && id==a->pressed) {
            click(a, id);
        }
        a->pressed = ID_NONE;
        if (in.touch && a->hover) {
            a->hover  = ID_NONE;
            a->redraw = 1;
        }
    } break;
    case IN_MOVE: {
        i32 id = in.touch ? ID_NONE : hitat(a, in.x, in.y);
        if (id != a->hover) {
            a->hover  = id;
            a->redraw = 1;
        }
        a->cursor = clickable(id);
    } break;
    case IN_LEAVE:
        if (a->hover) {
            a->hover  = ID_NONE;
            a->redraw = 1;
        }
        break;
    case IN_KEY:
        key(a, in.key);
        break;
    case IN_FOCUS:
        save(a);
        break;
    case IN_WINDOW:
        a->win = in.win;
        break;
    }
}

static void appquit(App *a)
{
    save(a);
}

static i32 parsephase(Str s)
{
    static Str names[] = {"bid1", "bid2", "discard", "play", "trick", "passed", "score", "over"};
    for (i32 i = 0; i < countof(names); i++) {
        if (s == names[i]) return i;
    }
    return -1;
}

static App *appnew(Os *os, Arena mem, Options opt)
{
    iz    cap  = mem.end - mem.beg;
    Arena perm = {mem.beg, mem.beg + cap/8*3};
    App  *a    = alloc<App>(&perm);
    a->os    = os;
    a->opt   = opt;
    a->frame   = {perm.end, perm.end + cap/8*2};
    a->ai      = {a->frame.end, a->frame.end + cap/8*2};
    a->scratch = {a->ai.end, mem.end};
    a->font  = newfont(&perm);
    a->clips = synthclips(&perm);
    a->perm  = perm;
    a->g     = &a->sv.game;
    a->now   = 0;

    Arena t = a->scratch;
    Str   s = opt.headless && !opt.seeded ? Str{} : os_load(os, &t);
    a->sv  = s.data ? loadtext(s) : Saved{};
    a->win = a->sv.win;
    if (opt.seeded || !a->sv.hasgame) {
        a->g->seed = opt.seed;
        startgame(a);
    }
    if (!a->sv.set.welcomed && !opt.headless && !opt.autoplay) {
        a->overlay = OV_WELCOME;
    }
    static Str const panels[] = {"menu", "players", "rules", "stats", "help", "welcome"};
    static i32 const ovs[]    = {OV_MENU, OV_PLAYERS, OV_RULES, OV_STATS, OV_HELP, OV_WELCOME};
    for (i32 i = 0; i < countof(panels); i++) {
        if (opt.open == panels[i]) {
            a->overlay = ovs[i];
            a->seattab = NORTH;
        }
    }
    a->redraw = 1;
    return a;
}

static i32 appupdate(App *a, i64 now)
{
    a->now  = now;
    i32 w   = step(a);
    i32 min = w;
    if (a->flash.len) {
        if (now >= a->flashuntil) {
            a->flash  = {};
            a->redraw = 1;
        } else {
            i32 f = (i32)(a->flashuntil - now);
            min = min<0 || f<min ? f : min;
        }
    }
    if (a->fontwake) {
        if (now >= a->fontwake) {
            a->fontwake = 0;
            a->redraw   = 1;
        } else {
            i32 f = (i32)(a->fontwake - now);
            min = min<0 || f<min ? f : min;
        }
    }
    // The last frame drawn mid-motion isn't the resting one: draw again
    if (a->drawnat < a->animend) {
        a->redraw = 1;
    }
    if (animating(a) || a->redraw) {
        return 0;
    }
    return min;
}

// After a frame: ms until the next is needed, folding in a font rebake
// the frame just asked for.
static i32 appwait(App *a, i32 wait)
{
    if (a->redraw) {
        return 0;
    }
    if (a->fontwake) {
        i32 f = (i32)(a->fontwake - a->now);
        f = f<1 ? 1 : f;
        wait = wait<0 || f<wait ? f : wait;
    }
    return wait;
}

// Whether a headless shot may be taken: nothing moving or pending.
static b32 appsettled(App *a)
{
    if (!a->opt.headless && (animating(a) || a->redraw || a->fontwake)) {
        return 0;
    }
    if (a->opt.until >= 0) {
        return a->frozen;
    }
    Game *g = a->g;
    return a->overlay || (g->turn==SOUTH && !automatic(a, SOUTH)) ||
           g->phase==PH_SCORE || g->phase==PH_OVER;
}
