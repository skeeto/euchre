// Layout: where everything goes, for the window as it is
//
// Two arrangements, picked by the safe area's shape. Each is designed in
// units on a virtual canvas, 1000 units on its short side, whose long
// side stretches with the window, so the table keeps its proportions
// from a phone held upright to an ultrawide monitor. Everything is then
// converted to pixels. Text has a floor in points, from the view's
// scale, so it stays legible on a small window.

struct View {
    f32 w, h;        // pixels
    f32 inset[4];    // safe-area insets: top, right, bottom, left
    f32 scale;       // pixels per point
};

struct Layout {
    b32  tall;
    f32  k;          // pixels per unit
    Rect safe;
    Rect full;
    f32  W, H;       // the virtual canvas in units

    f32  cw;         // a card on the table, width in pixels (height 1.4x)
    f32  hw;         // a card in your hand
    f32  bw;         // a card in another's hand
    f32  hlift;      // how far a pointed-at card rises

    Pt   trick[4];   // card centers in the trick, by seat
    Pt   kitty;
    Pt   hand[4];    // hand centers, by seat
    f32  step[4];    // spacing along each hand
    f32  rot[4];     // each hand's card angle
    Rect badge[4];
    Rect score;
    Rect trumpbox;
    Rect menu;
    Rect hint;
    Rect bidbar;     // bid buttons go here
    f32  prompty;    // top of the prompt line while bidding
    f32  playpromptY;
    Rect panel;      // the area overlays center in

    f32  tsmall, tbody, tlabel, ttitle, tbig;  // text cap heights, pixels
    f32  pad;
    f32  radius;
    f32  btnh;       // button height
};

static f32 const CARDASPECT = 1.4f;

struct Units {
    Layout *L;
};

static Rect urect(Layout *L, f32 x, f32 y, f32 w, f32 h)
{
    return {L->safe.x + x*L->k, L->safe.y + y*L->k, w*L->k, h*L->k};
}

static Pt upt(Layout *L, f32 x, f32 y)
{
    return {L->safe.x + x*L->k, L->safe.y + y*L->k};
}

static Layout layoutfor(View v)
{
    Layout L = {};
    L.full = {0, 0, v.w, v.h};
    L.safe = {v.inset[3], v.inset[0], v.w - v.inset[1] - v.inset[3], v.h - v.inset[0] - v.inset[2]};
    if (L.safe.w<1 || L.safe.h<1) {
        L.safe = L.full;
    }
    f32 aspect = L.safe.w / L.safe.h;
    L.tall = aspect < 0.9f;
    f32 W, H;
    if (!L.tall) {
        if (aspect >= 1.3f) {
            H = 1000; W = 1000*aspect;
        } else {
            W = 1300; H = W/aspect;
        }
    } else {
        if (aspect <= 0.6f) {
            W = 1000; H = W/aspect;
        } else {
            H = 1667; W = H*aspect;
        }
    }
    L.W = W;
    L.H = H;
    L.k = L.safe.w / W;
    f32 k   = L.k;
    f32 cx  = W/2;
    f32 pts = v.scale>0 ? v.scale : 1;

    if (!L.tall) {
        // Wide: opponents at the sides, your partner across the top.
        f32 extra = H - 1000;  // a squarish window's spare height
        f32 cy    = 450 + extra*0.45f;
        L.cw = 116*k;
        L.hw = 150*k;
        L.bw = 80*k;
        L.hlift = 24*k;
        L.trick[SOUTH] = upt(&L, cx, cy+92);
        L.trick[NORTH] = upt(&L, cx, cy-92);
        L.trick[WEST]  = upt(&L, cx-130, cy);
        L.trick[EAST]  = upt(&L, cx+130, cy);
        L.kitty = upt(&L, cx, cy);

        L.hand[NORTH] = upt(&L, cx, 85);
        L.step[NORTH] = 26*k;
        L.badge[NORTH] = urect(&L, cx-115, 152, 230, 76);

        f32 sidex = 20 + 112/2.0f;
        L.hand[WEST] = upt(&L, sidex, cy);
        L.hand[EAST] = upt(&L, W-sidex, cy);
        L.step[WEST] = L.step[EAST] = 26*k;
        L.rot[WEST]  = PI/2;
        L.rot[EAST]  = -PI/2;
        L.badge[WEST] = urect(&L, 16, cy+106, 230, 76);
        L.badge[EAST] = urect(&L, W-16-230, cy+106, 230, 76);

        f32 handtop = H - 24 - 210;
        L.hand[SOUTH] = upt(&L, cx, handtop + 105);
        L.step[SOUTH] = 112*k;
        L.badge[SOUTH] = urect(&L, cx-355-30-230, handtop+105-38, 230, 76);
        L.hint = urect(&L, cx+355+30, handtop+105-34, 150, 68);
        if (cx-355-30-230 < 16) {
            // Narrow: badge and hint ride above the hand's ends
            L.badge[SOUTH] = urect(&L, 16, handtop-24-76, 230, 76);
            L.hint = urect(&L, W-16-150, handtop-24-68, 150, 68);
        }

        L.score    = urect(&L, 16, 16, 300, 124);
        L.menu     = urect(&L, W-16-84, 16, 84, 84);
        L.trumpbox = urect(&L, W-16-84-12-300, 16, 300, 124);
        L.prompty  = (cy + 81 + 18)*k + L.safe.y;
        L.bidbar   = urect(&L, cx-400, cy+81+70, 800, 74);
        L.playpromptY = (cy + 92 + 81 + 14)*k + L.safe.y;

        L.tsmall = 18*k;
        L.tbody  = 22*k;
        L.tlabel = 26*k;
        L.ttitle = 38*k;
        L.tbig   = 60*k;
        L.pad    = 16*k;
        L.radius = 14*k;
        L.btnh   = 68*k;
        L.panel  = urect(&L, 20, 20, W-40, H-40);
    } else {
        // Tall: a header, your partner below it, the opponents at the
        // edges, and a big hand across the bottom.
        f32 handw   = 200;
        f32 handtop = H - 24 - handw*CARDASPECT;
        f32 top     = 152;  // below the header
        f32 cy      = top + (handtop - 230 - top)*0.55f + 40;
        f32 grow    = clampf((H - 1667)/500, 0, 1);  // a phone's spare height
        f32 tcw     = 150 + 20*grow;
        f32 dy      = 122 + 28*grow;
        L.cw = tcw*k;
        L.hw = handw*k;
        L.bw = 74*k;
        L.hlift = 30*k;
        L.trick[SOUTH] = upt(&L, cx, cy+dy);
        L.trick[NORTH] = upt(&L, cx, cy-dy);
        L.trick[WEST]  = upt(&L, cx-165-10*grow, cy);
        L.trick[EAST]  = upt(&L, cx+165+10*grow, cy);
        L.kitty = upt(&L, cx, cy);

        f32 ny = top + 20 + 74*CARDASPECT/2;
        L.hand[NORTH] = upt(&L, cx-90, ny);
        L.step[NORTH] = 22*k;
        L.badge[NORTH] = urect(&L, cx+10, ny-46, 290, 92);

        f32 sidex = 10 + 74*CARDASPECT/2;
        L.hand[WEST] = upt(&L, sidex, cy-30);
        L.hand[EAST] = upt(&L, W-sidex, cy-30);
        L.step[WEST] = L.step[EAST] = 22*k;
        L.rot[WEST]  = PI/2;
        L.rot[EAST]  = -PI/2;
        L.badge[WEST] = urect(&L, 8, cy+82, 230, 92);
        L.badge[EAST] = urect(&L, W-8-230, cy+82, 230, 92);

        L.hand[SOUTH] = upt(&L, cx, handtop + handw*CARDASPECT/2);
        L.step[SOUTH] = (W - 32 - handw)/5*k;
        L.badge[SOUTH] = urect(&L, 16, handtop-36-92, 290, 92);
        L.hint = urect(&L, W-16-170, handtop-36-84, 170, 84);

        L.score    = urect(&L, 12, 12, 420, 128);
        L.menu     = urect(&L, W-12-100, 12, 100, 100);
        L.trumpbox = urect(&L, 444, 12, W-12-100-12-444, 128);
        f32 half   = tcw*CARDASPECT/2;
        f32 below  = cy + dy + half;
        L.prompty  = (cy + half + 24)*k + L.safe.y;
        L.bidbar   = urect(&L, 24, cy+half+90, W-48, 100);
        L.playpromptY = (below + 18)*k + L.safe.y;

        L.tsmall = 26*k;
        L.tbody  = 32*k;
        L.tlabel = 36*k;
        L.tbig   = 84*k;
        L.ttitle = 50*k;
        L.pad    = 20*k;
        L.radius = 18*k;
        L.btnh   = 96*k;
        L.panel  = urect(&L, 16, 16, W-32, H-32);
    }

    // Floors in points, so a small window stays legible
    L.tsmall = maxf(L.tsmall, 9*pts);
    L.tbody  = maxf(L.tbody, 11*pts);
    L.tlabel = maxf(L.tlabel, 12*pts);
    L.ttitle = maxf(L.ttitle, 16*pts);
    L.tbig   = maxf(L.tbig, 24*pts);
    return L;
}
