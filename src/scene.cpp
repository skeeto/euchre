// Scene: where each card belongs, the sprites easing there, and the
// table drawn around them: badges, scores, prompts and bid buttons

static void addhit(App *a, Rect r, i32 id)
{
    if (a->nhits < MAXHITS) {
        a->hits[a->nhits++] = {r, id};
    }
}

// A stable order for a hidden hand, that leaks nothing about the cards.
static i32 hiddenorder(Game *g, u32 m, i32 *out)
{
    i32 n = 0;
    u32 key[NCARDS];
    for (; m; m &= m-1) {
        i32 c = lowest(m);
        u32 k = (u32)hashmix((u64)g->handno*977 + (u64)g->seed, (u64)c);
        i32 i = n++;
        for (; i>0 && key[i-1]>k; i--) {
            key[i] = key[i-1];
            out[i] = out[i-1];
        }
        key[i] = k;
        out[i] = c;
    }
    return n;
}

static i32 handorder(Game *g, i32 seat, i32 *out)
{
    if (seat == SOUTH) {
        return sortcards(g->hand[seat], g->trump, out);
    }
    return hiddenorder(g, g->hand[seat], out);
}

static b32 yourturn(App *a)
{
    Game *g = a->g;
    return g->turn==SOUTH && !automatic(a, SOUTH) && g->phase<PH_TRICK;
}

static Place deckplace(App *a)
{
    Layout *L = &a->lay;
    Game   *g = a->g;
    Pt      h = L->hand[g->dealer];
    Pt      k = L->kitty;
    Place   p = {};
    p.x     = lerpf(k.x, h.x, 0.55f);
    p.y     = lerpf(k.y, h.y, 0.55f);
    p.w     = L->bw;
    p.rot   = L->rot[g->dealer];
    p.alpha = 1;
    return p;
}

struct Target {
    Place p;
    i32   z;
};

static Target placeof(App *a, i32 c)
{
    Game   *g = a->g;
    Layout *L = &a->lay;
    Target  t = {};
    t.p.alpha = 1;

    for (i32 s = 0; s < 4; s++) {
        if (!has(g->hand[s], c)) {
            continue;
        }
        i32 cs[6];
        i32 n = handorder(g, s, cs);
        i32 i = 0;
        for (; cs[i] != c; i++) {}
        f32 off = ((f32)i - (f32)(n-1)/2) * L->step[s];
        Pt  h   = L->hand[s];
        f32 r   = L->rot[s];
        // Along the hand's axis, which turns with it
        t.p.x   = h.x + off*cosf_(r);
        t.p.y   = h.y + off*sinf_(r);
        t.p.rot = r;
        t.p.w   = s==SOUTH ? L->hw : L->bw;
        t.p.face = s==SOUTH ? 1 : 0;
        t.z     = (s==SOUTH ? 400 : 100) + i;
        if (s == sittingout(g)) {
            t.p.alpha = 0.45f;
        }
        if (s==SOUTH && g->phase<PH_TRICK) {
            b32 mine = yourturn(a);
            if (mine && g->phase==PH_PLAY && !has(followable(g, g->hand[SOUTH]), c)) {
                t.p.dim = 1;
            } else if (mine && (g->phase==PH_PLAY || g->phase==PH_DISCARD) &&
                       a->hover==ID_CARD+c) {
                t.p.y -= L->hlift;
            }
            Move hm;
            if (hintready(a, &hm) && (hm.kind==MV_PLAY || hm.kind==MV_DISCARD) && hm.arg==c) {
                t.p.glow = 1;
                t.p.y -= L->hlift*0.5f;
            }
        }
        return t;
    }

    if (has(g->kitty, c)) {
        b32 bidding = g->phase==PH_BID1 || g->phase==PH_BID2 || g->phase==PH_PASSED;
        if (bidding) {
            t.p.x   = L->kitty.x;
            t.p.y   = L->kitty.y;
            t.p.w   = L->cw;
            if (c == g->upcard) {
                t.p.face = g->phase==PH_BID1;
                t.z = 60;
            } else {
                // the rest peek out underneath
                i32 k = popcount(g->kitty & (bit(c)-1) & ~bit(g->upcard));
                t.p.x  -= (f32)(3-k)*L->cw*0.035f;
                t.p.y  -= (f32)(3-k)*L->cw*0.035f;
                t.z     = 50 + k;
            }
            return t;
        }
        // Set aside by the dealer once trump is made, out of the way
        t.p = deckplace(a);
        t.p.w    *= 0.7f;
        t.p.alpha = 0;
        t.z       = 40;
        return t;
    }

    // Played: on the table this trick, else gathered by its taker
    i32 at = 0;
    for (; at<g->nplays && g->pcard[at]!=c; at++) {}
    if (at == g->nplays) {
        t.p = deckplace(a);  // unreachable with a valid game
        return t;
    }
    i32 seat  = g->pseat[at];
    i32 per   = trickcards(g);
    i32 trick = at / per;
    b32 live  = trick == g->ntricks;  // this trick, or resting
    u64 jit   = hashmix((u64)c, (u64)g->handno);
    if (live) {
        Pt p    = L->trick[seat];
        t.p.x   = p.x;
        t.p.y   = p.y;
        t.p.w   = L->cw;
        t.p.rot = ((f32)(jit & 255)/255 - 0.5f) * 0.12f;
        t.p.face = 1;
        t.z     = 200 + at - trick*per;
        if (g->phase==PH_TRICK && seat==g->winner) {
            t.p.glow = 1;
        }
        return t;
    }
    // Gathered toward the trick's taker, fading out
    i32 taker = takerof(g, trick*per);
    Pt  b     = center(L->badge[taker]);
    t.p.x     = b.x;
    t.p.y     = b.y;
    t.p.w     = L->cw*0.45f;
    t.p.face  = 1;
    t.p.alpha = 0;
    t.z       = 150;
    return t;
}

static Place spritenow(Sprite *s, i64 now)
{
    if (now >= s->t0 + s->dur) {
        return s->to;
    }
    if (now <= s->t0) {
        return s->from;
    }
    f32   u = easeout((f32)(now - s->t0) / (f32)s->dur);
    Place a = s->from, b = s->to, r;
    r.x     = lerpf(a.x, b.x, u);
    r.y     = lerpf(a.y, b.y, u);
    r.w     = lerpf(a.w, b.w, u);
    r.rot   = lerpf(a.rot, b.rot, u);
    r.face  = lerpf(a.face, b.face, ease((f32)(now - s->t0) / (f32)s->dur));
    r.alpha = lerpf(a.alpha, b.alpha, u);
    r.dim   = lerpf(a.dim, b.dim, u);
    r.glow  = lerpf(a.glow, b.glow, u);
    return r;
}

static b32 sameplace(Place a, Place b)
{
    return absf(a.x-b.x)<0.5f && absf(a.y-b.y)<0.5f && absf(a.w-b.w)<0.5f &&
           absf(a.rot-b.rot)<0.001f && absf(a.face-b.face)<0.01f &&
           absf(a.alpha-b.alpha)<0.01f && absf(a.dim-b.dim)<0.01f &&
           absf(a.glow-b.glow)<0.01f;
}

// Deal order: packets of three and two from the dealer's left, then the
// kitty.
static void dealanimation(App *a)
{
    Game *g = a->g;
    a->pendingdeal = 1;
    for (i32 c = 0; c < NCARDS; c++) {
        a->delay[c]   = 0;
        a->movedur[c] = 300;
    }
    static i32 const first[4]  = {3, 2, 3, 2};
    i32 packet = 0;
    for (i32 round = 0; round < 2; round++) {
        for (i32 i = 0; i < 4; i++) {
            i32 s = (g->dealer + 1 + i) & 3;
            i32 cs[6];
            i32 n  = handorder(g, s, cs);
            i32 lo = round ? first[i] : 0;
            i32 hi = round ? n : first[i];
            for (i32 j = lo; j < hi; j++) {
                a->delay[cs[j]] = 150 + packet*120 + (j-lo)*40;
            }
            packet++;
        }
    }
    i32 k = 0;
    for (u32 m = g->kitty; m; m &= m-1) {
        a->delay[lowest(m)] = 150 + packet*120 + k++*40;
    }
}

static void updatesprites(App *a, b32 snap)
{
    i64 now = a->now;
    if (a->opt.headless) {
        a->pendingdeal = 0;  // shots show things where they rest
        snap = 1;
    }
    if (a->pendingdeal) {
        Place deck = deckplace(a);
        for (i32 c = 0; c < NCARDS; c++) {
            Sprite *s = a->spr + c;
            Place   p = deck;
            p.y -= (f32)c*0.15f;
            s->from   = s->to = p;
            s->t0     = 0;
            s->dur    = 0;
            s->placed = 1;
        }
        a->pendingdeal = 0;
        snap = 0;
    }
    a->animend = 0;
    for (i32 c = 0; c < NCARDS; c++) {
        Sprite *s = a->spr + c;
        Place   p = placeof(a, c).p;
        if (snap || !s->placed) {
            s->from   = s->to = p;
            s->t0     = 0;
            s->dur    = 0;
            s->placed = 1;
            a->delay[c] = 0;
        } else if (!sameplace(p, s->to)) {
            Place cur = spritenow(s, now);
            b32 small = absf(p.x-s->to.x)<a->lay.hlift*1.5f && absf(p.y-s->to.y)<a->lay.hlift*1.5f &&
                        absf(p.face-s->to.face)<0.01f;
            s->from = cur;
            s->to   = p;
            s->t0   = now + a->delay[c];
            s->dur  = a->movedur[c] ? a->movedur[c] : small ? 120 : 300;
            if (absf(p.face - cur.face) > 0.5f && !a->movedur[c]) {
                s->dur = 360;
            }
            a->delay[c]   = 0;
            a->movedur[c] = 0;
        }
        if (s->t0 + s->dur > a->animend) {
            a->animend = s->t0 + s->dur;
        }
    }
}


// Table furniture

static Str bidtext(App *a, i32 i, Arena *t)
{
    Game *g    = a->g;
    i32   seat = (g->dealer + 1 + i) & 3;
    i32   b    = g->bids[i];
    if (b == NOSUIT) {
        return "Pass";
    }
    Str s;
    if (i < 4) {
        s = seat==g->dealer ? Str("I'll take it") : Str("Pick it up");
    } else {
        s = concat(t, suitsym(b), concat(t, " ", suitname(b)));
    }
    if (g->alone && seat==g->maker) {
        s = concat(t, s, ", alone!");
    }
    return s;
}

// The seat's latest bid this hand, or -1.
static i32 lastbid(Game *g, i32 seat)
{
    i32 r = -1;
    for (i32 i = 0; i < g->nbids; i++) {
        if (((g->dealer + 1 + i) & 3) == seat) r = i;
    }
    return r;
}

static void bubble(Draw *d, App *a, i32 seat, Str s)
{
    Layout *L  = &a->lay;
    Rect    b  = L->badge[seat];
    f32     px = L->tbody;
    f32     w  = textwidth(d->font, px, s) + 2*L->pad;
    f32     h  = px + 1.4f*L->pad;
    Rect    r;
    i32     side;  // where the bubble sits: 0 above, 1 right, 2 below, 3 left
    if (L->tall) {
        static i32 const sides[] = {0, 0, 2, 0};
        side = sides[seat];
        if (seat == NORTH) side = 3;
    } else {
        static i32 const sides[] = {0, 1, 1, 3};
        side = sides[seat];
    }
    f32 gap = L->pad*0.8f;
    switch (side) {
    case 0:  r = {b.x + L->pad, b.y - gap - h, w, h}; break;
    case 1:  r = {b.x + b.w + gap, b.y + (b.h-h)/2, w, h}; break;
    case 2:  r = {b.x + L->pad, b.y + b.h + gap, w, h}; break;
    default: r = {b.x - gap - w, b.y + (b.h-h)/2, w, h}; break;
    }
    if (seat==NORTH && L->tall) {
        // left of the partner's badge would cover the cards: go below
        r = {b.x + b.w - w, b.y + b.h + gap, w, h};
        side = 2;
    }
    r.x = clampf(r.x, L->safe.x + 4, L->safe.x + L->safe.w - w - 4);
    Color bg = rgb(250, 248, 240, 245);
    fillround(d, {r.x, r.y + h*0.08f, r.w, r.h}, h*0.35f, rgb(0, 0, 0, 60));
    fillround(d, r, h*0.35f, bg);
    f32 tl = h*0.28f;
    Pt  tail[3];
    switch (side) {
    case 0:  tail[0] = {r.x+tl, r.y+h-1}; tail[1] = {r.x+2.4f*tl, r.y+h-1}; tail[2] = {r.x+tl, r.y+h+tl}; break;
    case 1:  tail[0] = {r.x+1, r.y+h/2-tl/2}; tail[1] = {r.x+1, r.y+h/2+tl/2}; tail[2] = {r.x-tl, r.y+h/2}; break;
    case 2:  tail[0] = {r.x+tl, r.y+1}; tail[1] = {r.x+2.4f*tl, r.y+1}; tail[2] = {r.x+tl, r.y-tl}; break;
    default: tail[0] = {r.x+w-1, r.y+h/2-tl/2}; tail[1] = {r.x+w-1, r.y+h/2+tl/2}; tail[2] = {r.x+w+tl, r.y+h/2}; break;
    }
    fillpoly(d, tail, 3, bg);
    Draw k = *d;
    k.red   = SUITRED;
    k.black = SUITBLACK;
    textin(&k, r, px, rgb(30, 30, 36), s);
}

static void badge(Draw *d, App *a, i32 seat)
{
    Layout *L  = &a->lay;
    Game   *g  = a->g;
    Rect    r  = L->badge[seat];
    Draw    bd = *d;
    if (seat == sittingout(g)) {
        bd.alpha *= 0.5f;
    }
    b32 active = g->turn==seat && g->phase<PH_TRICK;
    if (active) {
        strokeround(&bd, inset(r, -L->pad*0.3f), L->radius*1.3f, L->pad*0.25f, GOLD);
    }
    panel(&bd, r, L->radius);
    f32 ar = r.h*0.36f;
    Pt  ac = {r.x + r.h*0.5f, r.y + r.h*0.5f};
    if (seat == SOUTH) {
        fillcircle(&bd, ac, ar, rgbhex(0xffffff, 0.85f));
        fillcircle(&bd, ac, ar*0.88f, rgb(60, 70, 80));
        textin(&bd, centered(ac, ar*2, ar*2), ar*0.8f, INK, "You");
    } else {
        avatar(&bd, ac, ar, seatchar(a, seat));
    }
    // Chips at the right, centered: the dealer's, then the maker's
    f32 cr    = r.h*0.2f;
    f32 right = r.x + r.w - L->pad*0.4f;
    Pt  cp    = {right - cr, r.y + r.h/2};
    if (seat == g->dealer) {
        chip(&bd, cp, cr, "D", rgb(245, 245, 240), rgb(30, 30, 30));
        right = cp.x - cr*1.3f;
        cp.x -= cr*2.3f;
    }
    b32 maker = seat==g->maker && g->trump!=NOSUIT;
    if (maker) {
        fillcircle(&bd, cp, cr, rgb(0, 0, 0, 90));
        fillcircle(&bd, cp, cr*0.92f, GOLD);
        drawsuit(&bd, g->trump, cp.x, cp.y, cr*1.05f, suitcolor(g->trump));
        right = cp.x - cr*1.3f;
    }

    f32 tx = r.x + r.h*0.92f;
    f32 tw = right - tx;
    Str name = seat==SOUTH ? Str("You") : seatchar(a, seat)->name;
    textleft(&bd, {tx, r.y + r.h*0.14f, tw, r.h*0.42f}, minf(L->tlabel, r.h*0.3f), INK, name);
    Str   sub;
    Color subc = INKDIM;
    if (seat == sittingout(g)) {
        sub = "Sitting out";
    } else if (maker && g->alone) {
        sub  = "Alone!";
        subc = GOLD;
    } else if (active && automatic(a, seat) && a->think.active) {
        sub = "Thinking...";
    } else if (seat == SOUTH) {
        sub = a->opt.autoplay ? Str("Autoplay") : Str("South");
    } else {
        sub = levelname(seatchar(a, seat)->level);
    }
    textleft(&bd, {tx, r.y + r.h*0.58f, tw, r.h*0.3f}, minf(L->tsmall, r.h*0.22f), subc, sub);
}

static void scorepanel(Draw *d, App *a)
{
    Layout *L = &a->lay;
    Game   *g = a->g;
    Rect    r = L->score;
    panel(d, r, L->radius);
    Rect in   = insetxy(r, L->pad, L->pad*0.6f);
    f32  rowh = in.h/2;
    Str  names[2] = {"Us", "Them"};
    Color cols[2] = {US, THEM};
    Arena t = a->ftmp;
    for (i32 team = 0; team < 2; team++) {
        Rect row = {in.x, in.y + rowh*(f32)team, in.w, rowh};
        textleft(d, {row.x, row.y, row.w*0.35f, row.h}, L->tlabel, cols[team], names[team]);
        Buf b(&t, 8);
        print(&b, (i64)g->score[team]);
        textleft(d, {row.x + row.w*0.40f, row.y, row.w*0.14f, row.h}, L->ttitle*0.8f, INK, finish(&b));
        // Pips toward the target
        i32 n   = g->rules.target;
        f32 x0  = row.x + row.w*0.54f;
        f32 pw  = (row.x + row.w - x0) / (f32)n;
        f32 pr  = minf(pw*0.36f, row.h*0.16f);
        for (i32 i = 0; i < n; i++) {
            Pt  c  = {x0 + pw*((f32)i + 0.5f), row.y + row.h/2};
            b32 on = i < g->score[team];
            fillcircle(d, c, pr, on ? cols[team] : rgb(255, 255, 255, 40));
        }
    }
}

static void trumppanel(Draw *d, App *a)
{
    Layout *L = &a->lay;
    Game   *g = a->g;
    Rect    r = L->trumpbox;
    Arena   t = a->ftmp;
    panel(d, r, L->radius);
    addhit(a, r, ID_TRICKS);
    Rect in = insetxy(r, L->pad, L->pad*0.6f);
    Draw k  = *d;
    k.red   = TEXTRED;
    if (g->trump == NOSUIT) {
        b32 down = g->phase != PH_BID1;
        Str s    = concat(&t, down ? Str("Turned down ") : Str("Turned up "),
                          cardlabel(&t, g->upcard));
        textleft(&k, {in.x, in.y, in.w, in.h*0.5f}, minf(L->tlabel, in.h*0.3f), INK, s);
        Str s2 = concat(&t, "Dealer: ", seatlabel(a, g->dealer));
        textleft(&k, {in.x, in.y + in.h*0.5f, in.w, in.h*0.5f}, minf(L->tsmall, in.h*0.22f), INKDIM, s2);
        return;
    }
    f32 sz = in.h*0.62f;
    fillcircle(&k, {in.x + sz*0.5f, in.y + in.h*0.36f}, sz*0.55f, rgb(250, 248, 240));
    drawsuit(&k, g->trump, in.x + sz*0.5f, in.y + in.h*0.36f, sz*0.62f, suitcolor(g->trump));
    f32  tx = in.x + sz*1.25f;
    Str  who = concat(&t, seatlabel(a, g->maker), g->maker==SOUTH ? Str(" called it") : Str(" called"));
    if (g->alone) {
        who = concat(&t, who, ", alone");
    }
    textleft(&k, {tx, in.y, in.x + in.w - tx, in.h*0.38f}, minf(L->tbody, in.h*0.28f),
             INK, suitname(g->trump));
    textleft(&k, {tx, in.y + in.h*0.38f, in.x + in.w - tx, in.h*0.3f},
             minf(L->tsmall, in.h*0.2f), INKDIM, who);
    // Tricks taken this hand, by team
    i32 tk[2] = {g->tricks[0] + g->tricks[2], g->tricks[1] + g->tricks[3]};
    f32 y  = in.y + in.h*0.83f;
    f32 pr = in.h*0.075f;
    f32 x  = tx;
    for (i32 team = 0; team < 2; team++) {
        Color col = team ? THEM : US;
        for (i32 i = 0; i < tk[team]; i++) {
            fillcircle(&k, {x + pr, y}, pr, col);
            x += pr*2.6f;
        }
        x += pr*2;
    }
}

static void menubutton(Draw *d, App *a)
{
    Layout *L = &a->lay;
    Rect    r = L->menu;
    b32     hot = a->hover == ID_MENU;
    fillround(d, r, L->radius, hot ? rgb(255, 255, 255, 50) : PANEL);
    f32 w = r.w*0.46f, h = r.h*0.07f;
    for (i32 i = -1; i <= 1; i++) {
        Rect bar = centered({r.x + r.w/2, r.y + r.h/2 + (f32)i*r.h*0.2f}, w, h);
        fillround(d, bar, h/2, INK);
    }
    addhit(a, r, ID_MENU);
}

// The line telling you what's wanted, or what's happening.
static Str prompt(App *a, Arena *t)
{
    Game *g = a->g;
    if (a->flash.len) {
        return a->flash;
    }
    if (g->phase == PH_PASSED) {
        return "Everyone passed. A new deal.";
    }
    if (g->phase == PH_TRICK) {
        return g->winner==SOUTH ? Str("You take the trick")
                                : concat(t, seatlabel(a, g->winner), " takes the trick");
    }
    if (!yourturn(a)) {
        return {};
    }
    i32 up = suitof(g->upcard);
    switch (g->phase) {
    case PH_BID1:
        return g->dealer==SOUTH ? concat(t, "Pick up the ", cardlabel(t, g->upcard))
                                : concat(t, concat(t, "Order up ", suitname(up)), "?");
    case PH_BID2:
        return g->rules.stick && g->dealer==SOUTH ? Str("Stuck! Name a suit")
                                                  : Str("Name a suit, or pass");
    case PH_DISCARD:
        return "Tap a card to discard";
    case PH_PLAY: {
        i32 led = ledsuit(g);
        if (led == NOSUIT) return "Your lead";
        if (g->hand[SOUTH] & suitmask(led, g->trump)) {
            return concat(t, "Follow ", suitsym(led));
        }
        return "You can't follow: play any card";
    }
    }
    return {};
}

static void bidbar(Draw *d, App *a)
{
    Layout *L = &a->lay;
    Game   *g = a->g;
    if (!yourturn(a) || (g->phase!=PH_BID1 && g->phase!=PH_BID2)) {
        return;
    }
    Arena t = a->ftmp;
    i32  ids[6];
    Str  labels[6];
    i32  n = 0;
    b32  canpass = legal(g, {MV_PASS, 0, 0});
    if (canpass) {
        ids[n] = ID_PASS; labels[n++] = "Pass";
    }
    if (g->phase == PH_BID1) {
        i32 up = suitof(g->upcard);
        Str verb = g->dealer==SOUTH ? Str("Pick up ") : Str("Order up ");
        ids[n] = ID_ORDER; labels[n++] = concat(&t, verb, suitsym(up));
    } else {
        for (i32 s = 0; s < 4; s++) {
            if (s != suitof(g->upcard)) {
                ids[n] = ID_CALL + s; labels[n++] = concat(&t, suitsym(s), concat(&t, " ", suitname(s)));
            }
        }
    }
    ids[n] = ID_ALONE; labels[n++] = a->alone ? Str("Alone: yes") : Str("Alone: no");

    Rect bar = L->bidbar;
    f32  gap = L->pad*0.6f;
    i32  rows = L->tall && n>4 ? 2 : 1;
    i32  per  = (n + rows - 1) / rows;
    f32  bh   = rows==2 ? (bar.h - gap)/2 : bar.h;
    f32  bw   = minf((bar.w - gap*(f32)(per-1)) / (f32)per, bh*4.2f);
    Move hm   = {};
    b32  hinted = hintready(a, &hm);
    for (i32 i = 0; i < n; i++) {
        i32  row = i / per;
        i32  col = i % per;
        i32  inrow = row==rows-1 ? n - per*row : per;
        f32  rw  = bw*(f32)inrow + gap*(f32)(inrow-1);
        Rect r   = {bar.x + (bar.w - rw)/2 + (bw+gap)*(f32)col, bar.y + (bh+gap)*(f32)row, bw, bh};
        b32  hot = a->hover == ids[i];
        Draw k   = *d;
        k.red    = TEXTRED;
        if (ids[i] == ID_ALONE && a->alone) {
            strokeround(&k, inset(r, -L->pad*0.25f), L->radius*1.2f, L->pad*0.2f, GOLD);
        }
        if (hinted) {
            b32 glow = (hm.kind==MV_PASS && ids[i]==ID_PASS) ||
                       (hm.kind==MV_ORDER && ids[i]==ID_ORDER) ||
                       (hm.kind==MV_CALL && ids[i]==ID_CALL+hm.arg) ||
                       (hm.alone && ids[i]==ID_ALONE && !a->alone);
            if (glow) {
                strokeround(&k, inset(r, -L->pad*0.35f), L->radius*1.3f, L->pad*0.3f, GOLD);
            }
        }
        button(&k, r, L->radius, L->tlabel, labels[i], hot, 1);
        addhit(a, r, ids[i]);
    }
}

static void hintbutton(Draw *d, App *a)
{
    Layout *L = &a->lay;
    if (!yourturn(a)) {
        return;
    }
    Str label = a->hinting && !a->hint.done ? Str("Thinking...") : Str("Hint");
    button(d, L->hint, L->radius, L->tlabel, label, a->hover==ID_HINT, 1);
    addhit(a, L->hint, ID_HINT);
}

struct Order {
    i32 card;
    i32 z;
};

static void drawtable(Draw *d, App *a)
{
    Layout *L = &a->lay;
    Game   *g = a->g;
    Arena   t = a->ftmp;
    felt(d, L->full);
    addhit(a, L->full, ID_TABLE);

    for (i32 s = 0; s < 4; s++) {
        badge(d, a, s);
    }
    scorepanel(d, a);
    trumppanel(d, a);
    menubutton(d, a);

    // Cards, back to front, moving ones on top
    Order ord[NCARDS];
    for (i32 c = 0; c < NCARDS; c++) {
        Sprite *s = a->spr + c;
        i32     z = placeof(a, c).z;
        if (a->now>=s->t0 && a->now<s->t0+s->dur) {
            z += 1000;
        }
        ord[c] = {c, z};
    }
    for (i32 i = 1; i < NCARDS; i++) {
        for (i32 j = i; j>0 && ord[j-1].z>ord[j].z; j--) {
            Order x = ord[j]; ord[j] = ord[j-1]; ord[j-1] = x;
        }
    }
    for (i32 i = 0; i < NCARDS; i++) {
        i32   c = ord[i].card;
        Place p = spritenow(a->spr + c, a->now);
        drawcard(d, c, p, g->trump);
        if (has(g->hand[SOUTH], c) && g->phase<PH_TRICK) {
            f32 w = p.w, h = p.w*CARDASPECT;
            addhit(a, {p.x - w/2, p.y - h/2, w, h}, ID_CARD + c);
        }
    }

    // Bids show while bidding, and the maker's until the first card
    b32 bidding = g->phase==PH_BID1 || g->phase==PH_BID2 || g->phase==PH_PASSED;
    if (bidding) {
        for (i32 s = 0; s < 4; s++) {
            i32 i = lastbid(g, s);
            if (i >= 0) bubble(d, a, s, bidtext(a, i, &t));
        }
    } else if (g->maker>=0 && g->nplays==0 && g->phase<PH_TRICK) {
        bubble(d, a, g->maker, bidtext(a, g->nbids-1, &t));
    }

    Str p = prompt(a, &t);
    if (p.len) {
        f32  y  = bidding || g->phase==PH_DISCARD ? L->prompty : L->playpromptY;
        f32  px = L->tlabel;
        f32  w  = textwidth(d->font, px, p) + 2*L->pad;
        Rect r  = {L->kitty.x - w/2, y - L->pad*0.4f, w, px + L->pad*0.8f};
        if (g->phase==PH_DISCARD || g->phase==PH_PLAY || g->phase==PH_TRICK) {
            panel(d, r, r.h/2);
        }
        Draw k = *d;
        k.red  = TEXTRED;
        textc(&k, L->kitty.x, y, px, a->flash.len ? rgb(255, 190, 120) : INK, p);
    }
    bidbar(d, a);
    hintbutton(d, a);
}
