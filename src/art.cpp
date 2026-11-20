// Art: cards, the felt, badges, buttons and panels, all drawn as shapes
//
// A card is drawn in local units, 100 wide and 140 tall about its
// center, through a transform that puts it anywhere at any size and
// angle; squeezing its width through zero is the flip between its face
// and its back.

static Color const FELT_IN    = {0.10f, 0.42f, 0.27f, 1};
static Color const FELT_OUT   = {0.03f, 0.22f, 0.14f, 1};
static Color const INK        = {0.96f, 0.95f, 0.91f, 1};
static Color const INKDIM     = {0.80f, 0.86f, 0.82f, 1};
static Color const GOLD       = {1.00f, 0.83f, 0.38f, 1};
static Color const PANEL      = {0.02f, 0.10f, 0.07f, 0.62f};
static Color const BUTTON     = {0.16f, 0.50f, 0.36f, 1};
static Color const BUTTONHOT  = {0.22f, 0.62f, 0.45f, 1};
static Color const BUTTONOFF  = {0.12f, 0.28f, 0.22f, 1};
static Color const FACE       = {0.985f, 0.975f, 0.95f, 1};
static Color const EDGE       = {0.62f, 0.60f, 0.56f, 1};
static Color const BACK       = {0.13f, 0.20f, 0.45f, 1};
static Color const BACKLINE   = {0.24f, 0.34f, 0.66f, 1};
static Color const TEXTRED    = {1.00f, 0.42f, 0.42f, 1};  // red suits on dark
static Color const US         = {1.00f, 0.83f, 0.38f, 1};
static Color const THEM       = {0.55f, 0.78f, 1.00f, 1};

// A card's place on screen.
struct Place {
    f32 x, y;    // center, pixels
    f32 w;       // width, pixels
    f32 rot;     // radians
    f32 face;    // 1 face up, 0 face down, between: mid-flip
    f32 alpha;
    f32 dim;     // darkened, 0 to 1
    f32 glow;    // highlighted, 0 to 1
};

static void felt(Draw *d, Rect r)
{
    Pt    c  = center(r);
    Pt    p[4] = {{r.x, r.y}, {r.x+r.w, r.y}, {r.x+r.w, r.y+r.h}, {r.x, r.y+r.h}};
    // A fan from the middle: lighter in the center, darker at the rim
    for (i32 i = 0; i < 4; i++) {
        Pt a = p[i], b = p[(i+1)%4];
        Pt m = {(a.x+b.x)/2, (a.y+b.y)/2};
        tri(d, c, FELT_IN, a, FELT_OUT, m, mix(FELT_IN, FELT_OUT, 0.75f));
        tri(d, c, FELT_IN, m, mix(FELT_IN, FELT_OUT, 0.75f), b, FELT_OUT);
    }
}

static Draw withxf(Draw *d, Xform t)
{
    Draw r = *d;
    r.t = compose(d->t, t);
    return r;
}

static void pip(Draw *d, i32 suit, f32 x, f32 y, f32 size, b32 flip)
{
    if (!flip) {
        drawsuit(d, suit, x, y, size, suitcolor(suit));
        return;
    }
    Draw f = withxf(d, placexf(x, y, PI, 1, 1));
    drawsuit(&f, suit, 0, 0, size, suitcolor(suit));
}

// One corner's index: the rank over a small suit.
static void cornerindex(Draw *d, i32 c)
{
    // The index column runs from 4 to 21, clear of the edge and of the
    // pips and court frame, which start at 25. A "10" is condensed a
    // little to fit.
    Str r   = rankname(rankof(c));
    f32 px  = 16;
    f32 cx  = 12.5f;
    Color k = suitcolor(suitof(c));
    f32 w   = textwidthd(d, px, r);
    f32 sx  = w>16.5f ? 16.5f/w : 1;
    Draw t  = withxf(d, placexf(cx, 0, 0, sx, 1));
    textc(&t, 0, 7, px, k, r);
    drawsuit(d, suitof(c), cx, 7 + 16 + 9, 11, k);
}

static void cardface(Draw *d, i32 c, i32 trump)
{
    i32 s = suitof(c);
    i32 r = rankof(c);
    fillround(d, {-50, -70, 100, 140}, 8, EDGE);
    fillround(d, {-49, -69, 98, 138}, 7, FACE);

    Draw lc = withxf(d, placexf(-50, -70, 0, 1, 1));
    cornerindex(&lc, c);
    Draw rc = withxf(d, placexf(50, 70, PI, 1, 1));
    cornerindex(&rc, c);

    Color k = suitcolor(s);
    switch (r) {
    case NINE: {
        f32 ys[4] = {-44, -15, 15, 44};
        for (i32 i = 0; i < 4; i++) {
            pip(d, s, -17, ys[i], 16, i>=2);
            pip(d, s,  17, ys[i], 16, i>=2);
        }
        pip(d, s, 0, 0, 16, 0);
    } break;
    case TEN: {
        f32 ys[4] = {-46, -16, 16, 46};
        for (i32 i = 0; i < 4; i++) {
            pip(d, s, -17, ys[i], 16, i>=2);
            pip(d, s,  17, ys[i], 16, i>=2);
        }
        pip(d, s, 0, -31, 16, 0);
        pip(d, s, 0,  31, 16, 1);
    } break;
    case ACE:
        pip(d, s, 0, 2, 52, 0);
        break;
    default: {
        // Courts: a framed panel with the letter and the suit
        Color tint = s>=2 ? Color{0.99f, 0.90f, 0.88f, 1} : Color{0.90f, 0.92f, 0.97f, 1};
        fillround(d, {-25, -48, 50, 96}, 5, k);
        fillround(d, {-23, -46, 46, 92}, 4, tint);
        Str l = rankname(r);
        textc(d, 0, -34, 32, k, l);
        drawsuit(d, s, 0, 20, 28, k);
        // a crown's points for the king and queen, a band for the jack
        if (r == KING || r == QUEEN) {
            for (i32 i = -1; i <= 1; i++) {
                Pt tri3[3] = {{(f32)i*10 - 5, -40}, {(f32)i*10 + 5, -40}, {(f32)i*10, -47}};
                if (r == KING) fillpoly(d, tri3, 3, withalpha(k, 0.5f));
            }
        }
        if (r == JACK) {
            fillrect(d, {-17, 37, 34, 3}, withalpha(k, 0.5f));
        }
    } break;
    }

    // The left bower carries trump's mark
    if (isleft(c, trump)) {
        fillcircle(d, {34, -55}, 11, rgb(40, 40, 50));
        fillcircle(d, {34, -55}, 9.5f, rgb(255, 214, 92));
        drawsuit(d, trump, 34, -55, 11, suitcolor(trump));
    }
}

static void cardback(Draw *d)
{
    fillround(d, {-50, -70, 100, 140}, 8, EDGE);
    fillround(d, {-49, -69, 98, 138}, 7, FACE);
    fillround(d, {-43, -63, 86, 126}, 5, BACK);
    // A lattice of small diamonds
    for (i32 row = 0; row < 9; row++) {
        for (i32 col = 0; col < 6; col++) {
            f32 x = -35 + 14*(f32)col + (row&1 ? 7 : 0);
            f32 y = -56 + 14*(f32)row;
            if (x > 37) continue;
            Pt q[4] = {{x, y-5}, {x+4, y}, {x, y+5}, {x-4, y}};
            fillpoly(d, q, 4, BACKLINE);
        }
    }
    fillround(d, {-14, -18, 28, 36}, 5, BACK);
    drawsuit(d, HEARTS, 0, -6, 13, rgb(220, 70, 80));
    drawsuit(d, SPADES, 0, 9, 13, rgb(230, 232, 240));
}

// Draw a card at a place; trump marks the left bower.
static void drawcard(Draw *d, i32 c, Place p, i32 trump)
{
    if (p.alpha <= 0.01f) {
        return;
    }
    f32  flip = cosf_(PI*(1 - clampf(p.face, 0, 1)));  // -1 back .. 1 face
    f32  sx   = absf(flip);
    f32  s    = p.w / 100;
    if (sx < 0.02f) {
        return;
    }
    Draw cd = withxf(d, placexf(p.x, p.y, p.rot, s*sx, s));
    cd.alpha   = d->alpha * p.alpha;
    cd.pxscale = s * xfscale(d->t);
    if (p.glow > 0.01f) {
        Draw g = withxf(d, placexf(p.x, p.y, p.rot, s, s));
        g.alpha = cd.alpha * p.glow;
        strokeround(&g, {-56, -76, 112, 152}, 13, 6, GOLD);
    }
    // A soft shadow under the card
    {
        Draw sh = cd;
        sh.alpha *= 0.25f;
        fillround(&sh, {-49, -66, 102, 140}, 9, rgb(0, 0, 0));
    }
    if (flip > 0) {
        cardface(&cd, c, trump);
    } else {
        cardback(&cd);
    }
    if (p.dim > 0.01f) {
        fillround(&cd, {-50, -70, 100, 140}, 8, rgb(10, 30, 20, (i32)(150*p.dim)));
    }
}

static void panel(Draw *d, Rect r, f32 rad)
{
    fillround(d, r, rad, PANEL);
}

static void button(Draw *d, Rect r, f32 rad, f32 px, Str label, b32 hot, b32 on)
{
    Color top = on ? (hot ? BUTTONHOT : BUTTON) : BUTTONOFF;
    fillround(d, {r.x, r.y + r.h*0.06f, r.w, r.h}, rad, rgb(0, 0, 0, 70));
    fillroundgrad(d, r, rad, mix(top, rgb(255, 255, 255), 0.10f), mix(top, rgb(0, 0, 0), 0.10f));
    textin(d, r, px, on ? INK : withalpha(INK, 0.45f), label);
}

// A small round chip with a letter, as the dealer's button.
static void chip(Draw *d, Pt c, f32 r, Str label, Color bg, Color fg)
{
    fillcircle(d, c, r, rgb(0, 0, 0, 90));
    fillcircle(d, c, r*0.92f, bg);
    textin(d, centered(c, r*2, r*2), r*1.0f, fg, label);
}

static void avatar(Draw *d, Pt c, f32 r, Character const *ch)
{
    fillcircle(d, c, r, rgbhex(0xffffff, 0.85f));
    fillcircle(d, c, r*0.88f, rgbhex(ch->color));
    Str initial = takehead(ch->name, 1);
    textin(d, centered(c, r*2, r*2), r*0.95f, INK, initial);
}

// The program's icon: two bowers fanned on the felt.
static void drawicon(Draw *d, f32 size)
{
    Rect r = {0, 0, size, size};
    fillround(d, inset(r, size*0.04f), size*0.2f, FELT_OUT);
    fillround(d, inset(r, size*0.08f), size*0.17f, FELT_IN);
    Place a = {size*0.40f, size*0.52f, size*0.42f, -0.25f, 1, 1, 0, 0};
    Place b = {size*0.60f, size*0.50f, size*0.42f,  0.22f, 1, 1, 0, 0};
    drawcard(d, mkcard(DIAMONDS, JACK), a, HEARTS);
    drawcard(d, mkcard(HEARTS, JACK), b, HEARTS);
}
