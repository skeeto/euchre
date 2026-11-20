// Panels: the hand's summary, game over, and the overlays (menu,
// players, rules, stats, help, welcome, last trick); then the frame

static Color const SHEET = {0.05f, 0.17f, 0.12f, 0.97f};

static void scrim(Draw *d, App *a, b32 closes)
{
    fillrect(d, a->lay.full, rgb(0, 0, 0, 140));
    addhit(a, a->lay.full, closes ? ID_SCRIM : ID_PANEL);
}

// A sheet of w by h units centered in the safe area, kept inside it.
static Rect sheet(Draw *d, App *a, f32 w, f32 h)
{
    Layout *L = &a->lay;
    f32 pw = minf(w*L->k, L->safe.w - 2*L->pad);
    f32 ph = minf(h*L->k, L->safe.h - 2*L->pad);
    Rect r = centered(center(L->safe), pw, ph);
    fillround(d, inset(r, -2), L->radius*1.6f, rgb(255, 255, 255, 40));
    fillround(d, r, L->radius*1.5f, SHEET);
    addhit(a, r, ID_PANEL);
    return r;
}

static void sbutton(Draw *d, App *a, Rect r, Str label, i32 id)
{
    Draw k = *d;
    k.red = TEXTRED;
    button(&k, r, a->lay.radius, a->lay.tlabel, label, a->hover==id, 1);
    addhit(a, r, id);
}

// Wrapped paragraphs from y down; returns the y after them.
static f32 paragraphs(Draw *d, App *a, Rect r, f32 y, f32 px, Str body)
{
    Str  lines[32];
    i32  n = wraptext(d->font, px, r.w, body, lines, countof(lines));
    Draw k = *d;
    k.red  = TEXTRED;
    for (i32 i = 0; i < n; i++) {
        if (!lines[i].len) {
            y += px*0.8f;
            continue;
        }
        text(&k, r.x, y, px, INK, lines[i]);
        y += px*1.65f;
    }
    return y;
}

static void summary(Draw *d, App *a)
{
    Layout *L = &a->lay;
    Game   *g = a->g;
    Arena   t = a->ftmp;
    b32     over = g->phase == PH_OVER;
    scrim(d, a, 0);
    Rect r  = sheet(d, a, L->tall ? 900 : 640, L->tall ? 980 : 600);
    Rect in = inset(r, L->pad*1.6f);
    Draw k  = *d;
    k.red   = TEXTRED;

    i32 k5 = g->tricks[g->maker] + g->tricks[partner(g->maker)];
    b32 ours = teamof(g->maker) == 0;
    Str title;
    if (over) {
        title = gamewinner(g)==0 ? Str("You win!") : Str("They win");
    } else if (k5 < 3) {
        title = ours ? Str("Euchred!") : Str("You euchred them!");
    } else if (k5 == 5) {
        title = g->alone ? Str("A lone march!") : Str("March!");
    } else {
        title = ours ? Str("Made it") : Str("They made it");
    }
    f32 y = in.y;
    textc(&k, r.x + r.w/2, y, L->ttitle*1.2f, GOLD, title);
    y += L->ttitle*1.2f + L->pad*1.6f;

    Str line1 = concat(&t, seatlabel(a, g->maker), g->maker==SOUTH ? Str(" called ") : Str(" called "));
    line1 = concat(&t, line1, suitsym(g->trump));
    line1 = concat(&t, line1, concat(&t, " ", suitname(g->trump)));
    if (g->alone) line1 = concat(&t, line1, ", alone");
    textc(&k, r.x + r.w/2, y, L->tbody, INKDIM, line1);
    y += L->tbody*2;

    Buf b(&t, 64);
    print(&b, "Tricks: us ");
    print(&b, (i64)(g->tricks[0] + g->tricks[2]));
    print(&b, ", them ");
    print(&b, (i64)(g->tricks[1] + g->tricks[3]));
    textc(&k, r.x + r.w/2, y, L->tbody, INKDIM, finish(&b));
    y += L->tbody*2.2f;

    Buf p(&t, 32);
    print(&p, "+");
    print(&p, (i64)g->points);
    print(&p, g->pointsto==0 ? Str(" for us") : Str(" for them"));
    textc(&k, r.x + r.w/2, y, L->tlabel, g->pointsto==0 ? US : THEM, finish(&p));
    y += L->tlabel*2.4f;

    // The score, large
    Buf s0(&t, 8), s1(&t, 8);
    print(&s0, (i64)g->score[0]);
    print(&s1, (i64)g->score[1]);
    // Each label centered under its number, "to" on their baseline
    f32 cx  = r.x + r.w/2;
    Str n0  = finish(&s0);
    Str n1  = finish(&s1);
    f32 gap = L->pad*1.5f;
    f32 c0  = cx - gap - textwidthd(&k, L->tbig, n0)/2;
    f32 c1  = cx + gap + textwidthd(&k, L->tbig, n1)/2;
    textr(&k, cx - gap, y, L->tbig, US, n0);
    textc(&k, cx, y + L->tbig - L->tlabel, L->tlabel, INKDIM, "to");
    text(&k, cx + gap, y, L->tbig, THEM, n1);
    y += L->tbig + L->pad;
    textc(&k, c0, y, L->tsmall, INKDIM, "Us");
    textc(&k, c1, y, L->tsmall, INKDIM, "Them");

    if (over) {
        Buf w(&t, 64);
        print(&w, "Games won: ");
        print(&w, (i64)a->sv.stats.wins);
        print(&w, " of ");
        print(&w, (i64)a->sv.stats.games);
        textc(&k, cx, y + L->tsmall*2.4f, L->tbody, INKDIM, finish(&w));
    }

    f32  bw = minf(in.w, 360*L->k);
    Rect br = {cx - bw/2, in.y + in.h - L->btnh, bw, L->btnh};
    sbutton(d, a, br, over ? Str("New game") : Str("Next hand"), ID_CONTINUE);
}

static void menupanel(Draw *d, App *a)
{
    Layout *L = &a->lay;
    scrim(d, a, 1);
    static Str const speeds[] = {"Speed: relaxed", "Speed: normal", "Speed: brisk"};
    Str labels[] = {
        "Resume", "New game", "Players", "Rules",
        a->sv.set.sound ? Str("Sound: on") : Str("Sound: off"),
        speeds[a->sv.set.speed], "How to play", "Stats", "Quit",
    };
    i32 ids[] = {
        ID_CLOSE, ID_NEWGAME, ID_PLAYERS, ID_RULES, ID_SOUND, ID_SPEED,
        ID_HELP, ID_STATS, ID_QUIT,
    };
    i32  n   = countof(ids);
    f32  bh  = L->btnh*0.85f;
    f32  gap = L->pad*0.6f;
    f32  hu  = ((bh + gap)*(f32)n + L->pad*3) / L->k;
    Rect r   = sheet(d, a, L->tall ? 700 : 460, hu);
    Rect in  = inset(r, L->pad*1.5f);
    bh = (in.h - gap*(f32)(n-1)) / (f32)n;
    for (i32 i = 0; i < n; i++) {
        sbutton(d, a, {in.x, in.y + (bh+gap)*(f32)i, in.w, bh}, labels[i], ids[i]);
    }
}

static void playerspanel(Draw *d, App *a)
{
    Layout *L = &a->lay;
    Arena   t = a->ftmp;
    scrim(d, a, 1);
    Rect r  = sheet(d, a, L->tall ? 960 : 1100, L->tall ? 1500 : 900);
    Rect in = inset(r, L->pad*1.5f);
    f32  y  = in.y;
    textc(d, r.x + r.w/2, y, L->ttitle, GOLD, "Players");
    y += L->ttitle + L->pad*1.2f;

    // A tab per seat
    i32 seats[3] = {WEST, NORTH, EAST};
    Str roles[3] = {"Left", "Partner", "Right"};
    f32 gap = L->pad*0.6f;
    f32 tw  = (in.w - 2*gap)/3;
    f32 th  = L->btnh*0.9f;
    for (i32 i = 0; i < 3; i++) {
        i32  s   = seats[i];
        Rect tr  = {in.x + (tw+gap)*(f32)i, y, tw, th};
        b32  sel = a->seattab == s;
        Character const *ch = characters + a->sv.set.seats[s];
        fillround(d, tr, L->radius, sel ? BUTTONHOT : BUTTONOFF);
        if (sel) strokeround(d, inset(tr, -3), L->radius*1.2f, 3, GOLD);
        textin(d, {tr.x, tr.y + th*0.06f, tr.w, th*0.4f}, minf(L->tsmall, th*0.2f), INKDIM, roles[i]);
        textin(d, {tr.x, tr.y + th*0.42f, tr.w, th*0.5f}, minf(L->tlabel, th*0.3f), INK, ch->name);
        addhit(a, tr, ID_SEAT + s);
    }
    y += th + L->pad*1.2f;

    // The roster
    i32  cols  = L->tall ? 2 : 4;
    i32  rows  = (NCHARACTERS + cols - 1) / cols;
    f32  foot  = L->btnh + L->tsmall*2.5f + L->pad;
    f32  cw    = (in.w - gap*(f32)(cols-1)) / (f32)cols;
    f32  ch    = (in.y + in.h - foot - y - gap*(f32)(rows-1)) / (f32)rows;
    i32  cur   = a->sv.set.seats[a->seattab];
    for (i32 i = 0; i < NCHARACTERS; i++) {
        Character const *c = characters + i;
        Rect cr  = {in.x + (cw+gap)*(f32)(i%cols), y + (ch+gap)*(f32)(i/cols), cw, ch};
        b32  sel = i == cur;
        b32  hot = a->hover == ID_CHAR+i;
        // Taken by another seat: still pickable, but shown as seated
        b32  taken = 0;
        for (i32 s = 1; s < 4; s++) {
            taken |= s!=a->seattab && a->sv.set.seats[s]==i;
        }
        fillround(d, cr, L->radius, hot ? rgb(255, 255, 255, 36) : rgb(255, 255, 255, 18));
        if (sel) strokeround(d, inset(cr, -3), L->radius*1.2f, 4, GOLD);
        f32 ar = minf(ch*0.22f, cw*0.14f);
        Pt  ac = {cr.x + L->pad + ar, cr.y + L->pad + ar};
        avatar(d, ac, ar, c);
        f32 tx = ac.x + ar + L->pad*0.8f;
        textleft(d, {tx, cr.y + L->pad*0.6f, cr.x + cr.w - tx - L->pad*0.5f, ar}, L->tlabel, INK, c->name);
        textleft(d, {tx, cr.y + L->pad*0.6f + ar, cr.x + cr.w - tx - L->pad*0.5f, ar},
                 L->tsmall, rgbhex(c->color), levelname(c->level));
        Rect body = {cr.x + L->pad, ac.y + ar + L->pad*0.7f, cr.w - 2*L->pad, 0};
        Str  blurb = c->blurb;
        i32  dot   = 0;
        for (; dot<blurb.len && blurb[dot]!='.'; dot++) {}
        blurb = trimleft(cuthead(blurb, dot < blurb.len ? dot+1 : dot));
        if (taken) {
            blurb = concat(&t, blurb, " (seated)");
        }
        paragraphs(d, a, body, body.y, L->tsmall, blurb);
        addhit(a, cr, ID_CHAR + i);
    }

    f32 by = in.y + in.h - L->btnh;
    textc(d, r.x + r.w/2, by - L->tsmall*2.2f, L->tsmall, INKDIM,
          "Changes take effect with the next game.");
    f32 bw = (in.w - gap)/2;
    sbutton(d, a, {in.x, by, bw, L->btnh}, "New game now", ID_NEWGAME);
    sbutton(d, a, {in.x + bw + gap, by, bw, L->btnh}, "Done", ID_CLOSE);
}

static void rulespanel(Draw *d, App *a)
{
    Layout *L = &a->lay;
    scrim(d, a, 1);
    Rect r  = sheet(d, a, L->tall ? 900 : 760, L->tall ? 1000 : 620);
    Rect in = inset(r, L->pad*1.5f);
    f32  y  = in.y;
    textc(d, r.x + r.w/2, y, L->ttitle, GOLD, "Rules");
    y += L->ttitle + L->pad*1.5f;

    Rules *ru = &a->sv.set.rules;
    f32 bh = L->btnh*0.85f;
    f32 bw = minf(in.w*0.32f, 220*L->k);
    textleft(d, {in.x, y, in.w - bw, bh}, L->tlabel, INK, "Stick the dealer");
    Rect tg = {in.x + in.w - bw, y, bw, bh};
    sbutton(d, a, tg, ru->stick ? Str("On") : Str("Off"), ID_STICK);
    y += bh + L->pad*0.6f;
    y = paragraphs(d, a, in, y, L->tsmall,
                   "If everyone passes twice, the dealer must name a suit. "
                   "Off: the hand is thrown in and the deal passes.");
    y += L->pad;

    textleft(d, {in.x, y, in.w*0.4f, bh}, L->tlabel, INK, "Game to");
    static i32 const targets[] = {5, 7, 10};
    static Str const names[]   = {"5", "7", "10"};
    f32 sw  = minf((in.w*0.6f - 2*L->pad*0.5f)/3, 130*L->k);
    f32 sx  = in.x + in.w - 3*sw - 2*L->pad*0.5f;
    for (i32 i = 0; i < 3; i++) {
        Rect sr  = {sx + (sw + L->pad*0.5f)*(f32)i, y, sw, bh};
        b32  sel = ru->target == targets[i];
        if (sel) strokeround(d, inset(sr, -3), L->radius*1.2f, 3, GOLD);
        button(d, sr, L->radius, L->tlabel, names[i], a->hover==ID_TARGET+i, sel);
        addhit(a, sr, ID_TARGET + i);
    }
    y += bh + L->pad;
    y = paragraphs(d, a, in, y, L->tsmall,
                   "Points needed to win. Rule changes take effect with the next game.");

    f32 gap = L->pad*0.6f;
    f32 by  = in.y + in.h - L->btnh;
    f32 hw  = (in.w - gap)/2;
    sbutton(d, a, {in.x, by, hw, L->btnh}, "New game now", ID_NEWGAME);
    sbutton(d, a, {in.x + hw + gap, by, hw, L->btnh}, "Done", ID_CLOSE);
}

static void statspanel(Draw *d, App *a)
{
    Layout *L  = &a->lay;
    Arena   t  = a->ftmp;
    Stats  *st = &a->sv.stats;
    scrim(d, a, 1);
    Rect r  = sheet(d, a, L->tall ? 860 : 600, L->tall ? 1150 : 820);
    Rect in = inset(r, L->pad*1.5f);
    f32  y  = in.y;
    textc(d, r.x + r.w/2, y, L->ttitle, GOLD, "Stats");
    y += L->ttitle + L->pad*1.5f;
    struct { Str label; i32 v; } rows[] = {
        {"Games played", st->games},
        {"Games won", st->wins},
        {"Hands played", st->hands},
        {"Hands your team made", st->made},
        {"Marches", st->marches},
        {"Your team euchred", st->euchred},
        {"Euchres you dealt", st->euchres},
        {"Your loners", st->loners},
        {"Loners you made", st->lonewins},
    };
    f32 rh = (in.h - (y - in.y) - L->btnh - L->pad) / (f32)countof(rows);
    rh = minf(rh, L->tbody*2.4f);
    for (i32 i = 0; i < countof(rows); i++) {
        Rect row = {in.x, y + rh*(f32)i, in.w, rh};
        textleft(d, {row.x, row.y, row.w*0.75f, row.h}, L->tbody, INKDIM, rows[i].label);
        Buf b(&t, 16);
        print(&b, (i64)rows[i].v);
        textr(d, row.x + row.w, row.y + (row.h - textpx(d->font, L->tbody))/2, L->tbody, INK, finish(&b));
    }
    sbutton(d, a, {in.x, in.y + in.h - L->btnh, in.w, L->btnh}, "Done", ID_CLOSE);
}

struct HelpPage {
    Str title;
    Str body;
};

static HelpPage const helppages[NHELP] = {
    {"The table",
     "You sit at the bottom. The player across from you is your partner, and "
     "the players to your left and right are the other team. The first team "
     "to 10 points wins.\n\nOnly 24 cards are used: the 9, 10, jack, queen, "
     "king and ace of each suit. Everyone gets five, and the next card is "
     "turned up."},
    {"Trump and the bowers",
     "One suit is trump each hand, and any trump beats any other card. The "
     "jack of trump, the right bower, is the highest card. The other jack of "
     "the same color, the left bower, is next, and it counts as a trump, not "
     "as its own suit. Then come the ace, king, queen, 10 and 9."},
    {"Making trump",
     "From the dealer's left, each player may order up the turned card: its "
     "suit becomes trump, and the dealer takes it and discards. If all pass, "
     "the card is turned down, and each may name another suit. With stick the "
     "dealer, the dealer must name one.\n\nWhoever makes trump may go alone: "
     "their partner sits out, for a bigger reward."},
    {"Playing",
     "The dealer's left leads. Follow the suit led if you can; otherwise play "
     "any card. The highest trump wins the trick, or if none was played, the "
     "highest card of the suit led. The winner leads next. Cards you can't "
     "play are dimmed: just tap the one you want."},
    {"Scoring",
     "The team that made trump needs three of the five tricks. Three or four "
     "score 1 point; all five, a march, score 2, or 4 if alone. Taking fewer "
     "than three is a euchre: the other team scores 2.\n\nThe Hint button "
     "asks an expert what it would do."},
};

static void helpdiagram(Draw *d, App *a, Rect r, i32 page)
{
    i32 cards[8];
    i32 n = 0;
    i32 trump = NOSUIT;
    switch (page) {
    case 0:
        for (i32 k = 0; k < 6; k++) cards[n++] = mkcard(SPADES, k);
        break;
    case 1:
        trump = HEARTS;
        cards[n++] = mkcard(HEARTS, JACK);
        cards[n++] = mkcard(DIAMONDS, JACK);
        cards[n++] = mkcard(HEARTS, ACE);
        cards[n++] = mkcard(HEARTS, KING);
        cards[n++] = mkcard(HEARTS, QUEEN);
        cards[n++] = mkcard(HEARTS, TEN);
        cards[n++] = mkcard(HEARTS, NINE);
        break;
    case 2:
        cards[n++] = mkcard(CLUBS, NINE);
        cards[n++] = mkcard(HEARTS, ACE);
        cards[n++] = mkcard(CLUBS, JACK);
        break;
    case 3:
        trump = SPADES;
        cards[n++] = mkcard(DIAMONDS, KING);
        cards[n++] = mkcard(DIAMONDS, ACE);
        cards[n++] = mkcard(SPADES, NINE);
        cards[n++] = mkcard(DIAMONDS, NINE);
        break;
    default:
        trump = CLUBS;
        cards[n++] = mkcard(CLUBS, JACK);
        cards[n++] = mkcard(SPADES, JACK);
        cards[n++] = mkcard(CLUBS, ACE);
        cards[n++] = mkcard(HEARTS, ACE);
        cards[n++] = mkcard(DIAMONDS, ACE);
        break;
    }
    f32 w    = minf(r.w / ((f32)n*0.72f + 0.4f), r.h/CARDASPECT);
    f32 step = w*0.72f;
    f32 x0   = r.x + (r.w - (step*(f32)(n-1) + w))/2 + w/2;
    for (i32 i = 0; i < n; i++) {
        Place p = {x0 + step*(f32)i, r.y + r.h/2, w, 0, 1, 1, 0, 0};
        if (page==3 && i==2) p.glow = 1;  // the trump takes it
        drawcard(d, cards[i], p, trump);
    }
    (void)a;
}

static void helppanel(Draw *d, App *a)
{
    Layout *L = &a->lay;
    scrim(d, a, 1);
    Rect r  = sheet(d, a, L->tall ? 940 : 900, L->tall ? 1400 : 900);
    Rect in = inset(r, L->pad*1.5f);
    HelpPage const *p = helppages + a->helppage;
    f32 y = in.y;
    textc(d, r.x + r.w/2, y, L->ttitle, GOLD, p->title);
    y += L->ttitle + L->pad*1.2f;
    f32  dh = minf(in.h*0.3f, 230*L->k);
    helpdiagram(d, a, {in.x, y, in.w, dh}, a->helppage);
    y += dh + L->pad*1.2f;
    paragraphs(d, a, in, y, L->tbody, p->body);

    f32 gap = L->pad*0.6f;
    f32 by  = in.y + in.h - L->btnh;
    f32 bw  = (in.w - 2*gap)/3;
    if (a->helppage > 0) {
        sbutton(d, a, {in.x, by, bw, L->btnh}, "Back", ID_HELPBACK);
    }
    // page dots
    for (i32 i = 0; i < NHELP; i++) {
        f32 dr = L->pad*0.3f;
        Pt  c  = {in.x + in.w/2 + ((f32)i - (NHELP-1)/2.0f)*dr*3.2f, by + L->btnh/2};
        fillcircle(d, c, dr, i==a->helppage ? INK : rgb(255, 255, 255, 70));
    }
    b32 last = a->helppage+1 == NHELP;
    sbutton(d, a, {in.x + in.w - bw, by, bw, L->btnh}, last ? Str("Play") : Str("Next"), ID_HELPNEXT);
}

static void welcomepanel(Draw *d, App *a)
{
    Layout *L = &a->lay;
    scrim(d, a, 0);
    Rect r  = sheet(d, a, L->tall ? 900 : 760, L->tall ? 1150 : 720);
    Rect in = inset(r, L->pad*1.8f);
    f32  y  = in.y;
    textc(d, r.x + r.w/2, y, L->tbig, GOLD, "Euchre");
    y += L->tbig + L->pad*1.5f;
    f32 dh = minf(in.h*0.28f, 200*L->k);
    Place pa = {r.x + r.w/2 - dh*0.28f, y + dh/2, dh/CARDASPECT, -0.2f, 1, 1, 0, 0};
    Place pb = {r.x + r.w/2 + dh*0.28f, y + dh/2, dh/CARDASPECT,  0.2f, 1, 1, 0, 0};
    drawcard(d, mkcard(DIAMONDS, JACK), pa, HEARTS);
    drawcard(d, mkcard(HEARTS, JACK), pb, HEARTS);
    y += dh + L->pad*1.5f;
    paragraphs(d, a, in, y, L->tbody,
               "You and your partner across the table play against the two "
               "players at your sides. Make trump, take three tricks of five, "
               "and be the first team to 10 points. The game saves as you go: "
               "close it any time and pick up where you left off.");
    f32 gap = L->pad*0.6f;
    f32 by  = in.y + in.h - L->btnh;
    f32 bw  = (in.w - gap)/2;
    sbutton(d, a, {in.x, by, bw, L->btnh}, "How to play", ID_HOWTO);
    sbutton(d, a, {in.x + bw + gap, by, bw, L->btnh}, "Deal me in", ID_DEALIN);
}

static void lasttrickpanel(Draw *d, App *a)
{
    Layout *L = &a->lay;
    Game   *g = a->g;
    scrim(d, a, 1);
    addhit(a, L->full, ID_SCRIM);
    i32 per = trickcards(g);
    i32 b   = (g->ntricks - 1) * per;
    if (b < 0) {
        return;
    }
    Rect r  = sheet(d, a, L->tall ? 900 : 760, L->tall ? 560 : 440);
    addhit(a, r, ID_SCRIM);
    Rect in = inset(r, L->pad*1.4f);
    textc(d, r.x + r.w/2, in.y, L->tlabel, GOLD, "Last trick");
    f32 top = in.y + L->tlabel + L->pad;
    f32 h   = in.y + in.h - top - L->tsmall*2;
    f32 w   = minf(h/CARDASPECT, in.w/((f32)per*1.15f));
    f32 x0  = r.x + r.w/2 - (w*1.15f*(f32)(per-1))/2;
    i32 taker = takerof(g, b);
    for (i32 i = 0; i < per; i++) {
        Place p = {x0 + w*1.15f*(f32)i, top + w*CARDASPECT/2, w, 0, 1, 1, 0,
                   g->pseat[b+i]==taker ? 1.0f : 0.0f};
        drawcard(d, g->pcard[b+i], p, g->trump);
        textc(d, p.x, top + w*CARDASPECT + L->pad*0.5f, L->tsmall, INKDIM,
              seatlabel(a, g->pseat[b+i]));
    }
}

static Draw newdraw(App *a, Mesh *m)
{
    Draw d = {};
    d.m     = m;
    d.font  = a->font;
    d.alpha = 1;
    d.red   = SUITRED;
    d.black = {};
    whiteuv(&m->wu, &m->wv);
    return d;
}

static b32 sameview(View a, View b)
{
    return a.w==b.w && a.h==b.h && a.scale==b.scale && a.inset[0]==b.inset[0] &&
           a.inset[1]==b.inset[1] && a.inset[2]==b.inset[2] && a.inset[3]==b.inset[3];
}

// Before drawing: any rebake the last frame asked for, so this frame's
// glyphs and the atlas the platform uploads after it agree.
static void startframe(App *a)
{
    fontprepare(a->font, a->now, a->scratch);
}

// After drawing: if text was missing a size, another frame once the
// rebake is due.
static void finishframe(App *a)
{
    if (fontupdate(a->font)) {
        i64 due = fontdue(a->font);
        if (due <= a->now) {
            a->redraw = 1;
        } else {
            a->fontwake = due;
        }
    }
}

static Mesh apprender(App *a, View v, i64 now)
{
    a->now    = now;
    a->redraw = 0;
    Arena f = a->frame;
    Mesh  m = newmesh(&f, (iz)1 << 19);
    a->ftmp = f;

    b32 snap = !a->laid || !sameview(v, a->view);
    if (snap) {
        a->lay  = layoutfor(v);
        a->view = v;
        a->laid = 1;
    }
    a->nhits = 0;
    updatesprites(a, snap);
    startframe(a);
    a->drawnat = a->now;

    Draw d = newdraw(a, &m);
    drawtable(&d, a);
    if (a->g->phase==PH_SCORE || a->g->phase==PH_OVER) {
        if (!animating(a) || a->overlay) {
            summary(&d, a);
        }
    }
    switch (a->overlay) {
    case OV_MENU:      menupanel(&d, a); break;
    case OV_PLAYERS:   playerspanel(&d, a); break;
    case OV_RULES:     rulespanel(&d, a); break;
    case OV_STATS:     statspanel(&d, a); break;
    case OV_HELP:      helppanel(&d, a); break;
    case OV_WELCOME:   welcomepanel(&d, a); break;
    case OV_LASTTRICK: lasttrickpanel(&d, a); break;
    }
    finishframe(a);
    if (snap) {
        a->redraw = 1;  // once more, for hover and sizes settled
        a->laid   = 1;
    }
    return m;
}

// The program's icon at size pixels.
static Mesh iconmesh(App *a, f32 size, i64 now)
{
    a->now  = now;
    Arena f = a->frame;
    Mesh  m = newmesh(&f, (iz)1 << 17);
    startframe(a);
    Draw  d = newdraw(a, &m);
    drawicon(&d, size);
    finishframe(a);
    return m;
}

// A Windows icon file from square RGBA images.
static Str makeico(Arena *a, i32 const *sizes, u8 **imgs, i32 n)
{
    Buf b(a, 1<<16);
    // header
    u8 h[6] = {0, 0, 1, 0, (u8)n, 0};
    print(&b, {h, 6});
    i32 offset = 6 + 16*n;
    for (i32 i = 0; i < n; i++) {
        i32 s    = sizes[i];
        i32 mask = ((s + 31)/32*4) * s;
        i32 len  = 40 + s*s*4 + mask;
        u8  e[16] = {
            (u8)(s>=256 ? 0 : s), (u8)(s>=256 ? 0 : s), 0, 0, 1, 0, 32, 0,
            (u8)len, (u8)(len>>8), (u8)(len>>16), (u8)(len>>24),
            (u8)offset, (u8)(offset>>8), (u8)(offset>>16), (u8)(offset>>24),
        };
        print(&b, {e, 16});
        offset += len;
    }
    for (i32 i = 0; i < n; i++) {
        i32 s    = sizes[i];
        i32 mask = ((s + 31)/32*4) * s;
        i32 img  = s*s*4;
        u8  bh[40] = {};
        i32 vals[] = {40, s, 2*s};
        for (i32 k = 0; k < 3; k++) {
            bh[4*k+0] = (u8)vals[k];
            bh[4*k+1] = (u8)(vals[k]>>8);
            bh[4*k+2] = (u8)(vals[k]>>16);
            bh[4*k+3] = (u8)(vals[k]>>24);
        }
        bh[12] = 1;   // planes
        bh[14] = 32;  // bits per pixel
        bh[20] = (u8)(img + mask);
        bh[21] = (u8)((img + mask)>>8);
        bh[22] = (u8)((img + mask)>>16);
        print(&b, {bh, 40});
        for (i32 y = s-1; y >= 0; y--) {
            for (i32 x = 0; x < s; x++) {
                u8 *p = imgs[i] + ((iz)y*s + x)*4;
                u8  q[4] = {p[2], p[1], p[0], p[3]};  // BGRA
                print(&b, {q, 4});
            }
        }
        for (i32 k = 0; k < mask; k++) {
            putbyte(&b, 0);
        }
    }
    return finish(&b);
}
