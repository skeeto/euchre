// Saving: settings, statistics and the game in progress, as text
//
// One line per field, "key value...", in any order, a checksum line
// last over everything before it. Unknown keys are skipped, so older
// builds read newer files. A file failing its checksum is ignored
// whole; a game failing validgame is dropped, keeping the rest, so a
// damaged save can cost a game but never crash or wedge the program.

struct Settings {
    Rules rules;            // for the next game
    i32   speed    = 1;     // 0 relaxed, 1 normal, 2 brisk
    b32   sound    = 1;
    b32   welcomed = 0;     // the first-launch welcome was dismissed
    i32   seats[4] = {-1, DEFAULT_LEFT, DEFAULT_PARTNER, DEFAULT_RIGHT};
};

struct Stats {
    i32 games;     // finished
    i32 wins;
    i32 hands;     // played out, not thrown in
    i32 made;      // hands your team made trump
    i32 marches;   // your team took all five as makers
    i32 euchred;   // your team was euchred
    i32 euchres;   // your team euchred the others
    i32 loners;    // you went alone
    i32 lonewins;  // and took all five
};

struct WinGeom {
    i32 x, y, w, h;
    b32 max;
};

struct Saved {
    Settings set;
    Stats    stats;
    WinGeom  win;
    b32      hasgame;
    Game     game;
    i32      table[4];  // the characters seated for this game
};

static void printsuit(Buf *b, i32 s)
{
    putbyte(b, s==NOSUIT ? '-' : (u8)"CSDH"[s]);
}

static void printcards(Buf *b, u32 m)
{
    for (; m; m &= m-1) {
        putbyte(b, ' ');
        printcard(b, lowest(m));
    }
}

static void printkv(Buf *b, Str key, i64 v)
{
    print(b, key);
    putbyte(b, ' ');
    print(b, v);
    putbyte(b, '\n');
}

static Str savetext(Arena *a, Saved *s)
{
    Buf b(a, 1<<11);
    print(&b, "euchre 1\n");
    printkv(&b, "target", s->set.rules.target);
    printkv(&b, "stick", s->set.rules.stick);
    printkv(&b, "speed", s->set.speed);
    printkv(&b, "sound", s->set.sound);
    printkv(&b, "welcomed", s->set.welcomed);
    print(&b, "seats");
    for (i32 i = 1; i < 4; i++) {
        putbyte(&b, ' ');
        print(&b, (i64)s->set.seats[i]);
    }
    print(&b, "\nstats");
    i32 *st = &s->stats.games;
    for (i32 i = 0; i < (i32)(sizeof(Stats)/sizeof(i32)); i++) {
        putbyte(&b, ' ');
        print(&b, (i64)st[i]);
    }
    print(&b, "\nwindow ");
    print(&b, (i64)s->win.x); putbyte(&b, ' ');
    print(&b, (i64)s->win.y); putbyte(&b, ' ');
    print(&b, (i64)s->win.w); putbyte(&b, ' ');
    print(&b, (i64)s->win.h); putbyte(&b, ' ');
    print(&b, (i64)s->win.max);
    putbyte(&b, '\n');

    if (s->hasgame) {
        Game *g = &s->game;
        print(&b, "table");
        for (i32 i = 1; i < 4; i++) {
            putbyte(&b, ' ');
            print(&b, (i64)s->table[i]);
        }
        putbyte(&b, '\n');
        print(&b, "grules ");
        print(&b, (i64)g->rules.target);
        putbyte(&b, ' ');
        print(&b, (i64)g->rules.stick);
        print(&b, "\nseed ");
        printhex(&b, g->seed, 16);
        putbyte(&b, '\n');
        printkv(&b, "phase", g->phase);
        printkv(&b, "dealer", g->dealer);
        printkv(&b, "turn", g->turn);
        printkv(&b, "handno", g->handno);
        print(&b, "score ");
        print(&b, (i64)g->score[0]);
        putbyte(&b, ' ');
        print(&b, (i64)g->score[1]);
        putbyte(&b, '\n');
        for (i32 i = 0; i < 4; i++) {
            print(&b, "hand");
            print(&b, (i64)i);
            printcards(&b, g->hand[i]);
            putbyte(&b, '\n');
        }
        print(&b, "kitty");
        printcards(&b, g->kitty);
        print(&b, "\nupcard ");
        printcard(&b, g->upcard);
        print(&b, "\ntrump ");
        printsuit(&b, g->trump);
        putbyte(&b, '\n');
        printkv(&b, "maker", g->maker);
        printkv(&b, "alone", g->alone);
        print(&b, "discard ");
        if (g->discard == NOCARD) {
            putbyte(&b, '-');
        } else {
            printcard(&b, g->discard);
        }
        print(&b, "\nbids");
        for (i32 i = 0; i < g->nbids; i++) {
            putbyte(&b, ' ');
            printsuit(&b, g->bids[i]);
        }
        print(&b, "\nplays");
        for (i32 i = 0; i < g->nplays; i++) {
            putbyte(&b, ' ');
            putbyte(&b, (u8)('0' + g->pseat[i]));
            printcard(&b, g->pcard[i]);
        }
        putbyte(&b, '\n');
        printkv(&b, "ntricks", g->ntricks);
        print(&b, "tricks");
        for (i32 i = 0; i < 4; i++) {
            putbyte(&b, ' ');
            print(&b, (i64)g->tricks[i]);
        }
        putbyte(&b, '\n');
        printkv(&b, "winner", g->winner);
        print(&b, "points ");
        print(&b, (i64)g->points);
        putbyte(&b, ' ');
        print(&b, (i64)g->pointsto);
        putbyte(&b, '\n');
    }

    u64 h = hash(finish(&b));
    print(&b, "check ");
    printhex(&b, h, 16);
    putbyte(&b, '\n');
    return finish(&b);
}

// Parse up to n integers in [lo, hi]; ok only if exactly n.
static b32 parseints(Str s, i32 *out, i32 n, i64 lo, i64 hi)
{
    for (i32 i = 0; i < n; i++) {
        Int v = parseint(field(&s));
        if (!v.ok || v.val<lo || v.val>hi) {
            return 0;
        }
        out[i] = (i32)v.val;
    }
    return !trim(s).len;
}

static b32 parsesuitfield(Str f, i32 *out)
{
    if (f.len != 1) return 0;
    if (f[0] == '-') {
        *out = NOSUIT;
        return 1;
    }
    *out = parsesuit(f[0]);
    return *out != NOSUIT;
}

// Parse a save. Anything wrong in a setting leaves its default; a game
// that doesn't hold together is dropped.
static Saved loadtext(Str text)
{
    Saved r = {};
    Saved d = {};  // defaults, for fields that fail

    // The checksum covers everything before its line.
    iz at = -1;
    for (iz i = 0; i+6 <= text.len; i++) {
        if ((i==0 || text[i-1]=='\n') && startswith(cuthead(text, i), "check ")) {
            at = i;
        }
    }
    if (at < 0) {
        return d;
    }
    Str body = takehead(text, at);
    Str tail = trim(cuthead(text, at+6));
    Int sum  = parsehex(tail);
    if (!sum.ok || (u64)sum.val!=hash(body) || !startswith(body, "euchre 1\n")) {
        return d;
    }

    Game *g    = &r.game;
    b32   gameok = 1;
    b32   any    = 0;  // any game field at all
    g->discard = NOCARD;
    g->trump   = NOSUIT;
    for (Str rest = body; rest.len;) {
        Cut ln  = cut(rest, '\n');
        rest    = ln.tail;
        Str val = ln.head;
        Str key = field(&val);
        i32 v[8] = {};
        if (key == "target") {
            if (parseints(val, v, 1, 5, 10) && (v[0]==5 || v[0]==7 || v[0]==10)) {
                r.set.rules.target = v[0];
            }
        } else if (key == "stick") {
            if (parseints(val, v, 1, 0, 1)) r.set.rules.stick = v[0];
        } else if (key == "speed") {
            if (parseints(val, v, 1, 0, 2)) r.set.speed = v[0];
        } else if (key == "sound") {
            if (parseints(val, v, 1, 0, 1)) r.set.sound = v[0];
        } else if (key == "welcomed") {
            if (parseints(val, v, 1, 0, 1)) r.set.welcomed = v[0];
        } else if (key == "seats") {
            if (parseints(val, v, 3, 0, NCHARACTERS-1)) {
                for (i32 i = 0; i < 3; i++) r.set.seats[i+1] = v[i];
            }
        } else if (key == "stats") {
            i32 n = (i32)(sizeof(Stats)/sizeof(i32));
            i32 st[16];
            if (parseints(val, st, n, 0, 1000000000)) {
                copybytes(&r.stats, st, n*(iz)sizeof(i32));
            }
        } else if (key == "window") {
            if (parseints(val, v, 5, -100000, 100000) && v[2]>=0 && v[3]>=0 &&
                v[2]<=20000 && v[3]<=20000 && (v[4]==0 || v[4]==1)) {
                r.win = {v[0], v[1], v[2], v[3], v[4]};
            }
        } else if (key == "table") {
            any = 1;
            r.table[0] = -1;
            if (parseints(val, v, 3, 0, NCHARACTERS-1)) {
                for (i32 i = 0; i < 3; i++) r.table[i+1] = v[i];
            } else {
                gameok = 0;
            }
        } else if (key == "grules") {
            any = 1;
            gameok &= parseints(val, v, 2, 0, 10);
            g->rules.target = v[0];
            g->rules.stick  = v[1];
        } else if (key == "seed") {
            any = 1;
            Int x = parsehex(trim(val));
            gameok &= x.ok;
            g->seed = (u64)x.val;
        } else if (key=="phase" || key=="dealer" || key=="turn" || key=="handno" ||
                   key=="maker" || key=="alone" || key=="ntricks" || key=="winner") {
            any = 1;
            i32 x = 0;
            gameok &= parseints(val, &x, 1, -1, 1000000);
            if      (key == "phase")   g->phase   = x;
            else if (key == "dealer")  g->dealer  = x;
            else if (key == "turn")    g->turn    = x;
            else if (key == "handno")  g->handno  = x;
            else if (key == "maker")   g->maker   = x;
            else if (key == "alone")   g->alone   = x;
            else if (key == "ntricks") g->ntricks = x;
            else                       g->winner  = x;
        } else if (key == "score") {
            any = 1;
            gameok &= parseints(val, g->score, 2, 0, 100);
        } else if (key == "tricks") {
            any = 1;
            gameok &= parseints(val, g->tricks, 4, 0, 5);
        } else if (key == "points") {
            any = 1;
            gameok &= parseints(val, v, 2, 0, 4);
            g->points   = v[0];
            g->pointsto = v[1];
        } else if (startswith(key, "hand") && key.len==5) {
            any = 1;
            i32   s = key[4] - '0';
            Cards c = parsecards(val);
            gameok &= s>=0 && s<4 && c.ok;
            if (s>=0 && s<4) g->hand[s] = c.mask;
        } else if (key == "kitty") {
            any = 1;
            Cards c = parsecards(val);
            gameok &= c.ok;
            g->kitty = c.mask;
        } else if (key == "upcard") {
            any = 1;
            g->upcard = parsecard(trim(val));
            gameok &= g->upcard != NOCARD;
        } else if (key == "trump") {
            any = 1;
            gameok &= parsesuitfield(trim(val), &g->trump);
        } else if (key == "discard") {
            any = 1;
            Str f = trim(val);
            if (!(f == "-")) {
                g->discard = parsecard(f);
                gameok &= g->discard != NOCARD;
            }
        } else if (key == "bids") {
            any = 1;
            g->nbids = 0;
            for (Str f = field(&val); f.len; f = field(&val)) {
                if (g->nbids==8 || !parsesuitfield(f, g->bids+g->nbids)) {
                    gameok = 0;
                    break;
                }
                g->nbids++;
            }
        } else if (key == "plays") {
            any = 1;
            g->nplays = 0;
            for (Str f = field(&val); f.len; f = field(&val)) {
                i32 c = f.len==3 ? parsecard(cuthead(f, 1)) : NOCARD;
                if (g->nplays==20 || c==NOCARD || f[0]<'0' || f[0]>'3') {
                    gameok = 0;
                    break;
                }
                g->pseat[g->nplays] = f[0] - '0';
                g->pcard[g->nplays] = c;
                g->nplays++;
            }
        }
    }
    r.hasgame = any && gameok && validgame(g);
    if (!r.hasgame) {
        r.game = {};
    }
    return r;
}
