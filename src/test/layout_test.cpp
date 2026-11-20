// Layout tests: across window shapes and densities, everything inside
// the safe area, nothing overlapping, and sizes legible and tappable

static b32 inside(Rect outer, Rect r)
{
    f32 e = 0.5f;
    return r.x>=outer.x-e && r.y>=outer.y-e &&
           r.x+r.w<=outer.x+outer.w+e && r.y+r.h<=outer.y+outer.h+e;
}

static Rect cardrect(Pt c, f32 w, b32 sideways)
{
    f32 h = w*CARDASPECT;
    return sideways ? centered(c, h, w) : centered(c, w, h);
}

static void test_layout(Test *t)
{
    Arena a = t->perm;
    struct { f32 w, h, scale; } sizes[] = {
        {1280, 720, 1}, {1920, 1080, 1}, {3440, 1440, 1}, {1024, 768, 1},
        {800, 800, 1}, {768, 1024, 1}, {1170, 2532, 3}, {360, 640, 1},
        {1080, 1920, 3}, {320, 480, 1}, {480, 420, 1}, {2560, 1600, 2},
        {600, 900, 1}, {900, 600, 1}, {700, 760, 1}, {1000, 1000, 2},
    };
    for (i32 i = 0; i < countof(sizes); i++) {
        View v = {sizes[i].w, sizes[i].h, {}, sizes[i].scale};
        if (sizes[i].scale == 3) {
            v.inset[0] = 141;  // a phone's notch and home bar
            v.inset[2] = 102;
        }
        Layout L = layoutfor(v);
        Buf name(&a, 64);
        print(&name, (i64)v.w);
        putbyte(&name, 'x');
        print(&name, (i64)v.h);
        putbyte(&name, ' ');
        Str prefix = finish(&name);

        // Everything fixed must sit inside the safe area, without overlaps
        Rect fixed[16];
        Str  what[16];
        i32  n = 0;
        for (i32 s = 0; s < 4; s++) {
            fixed[n] = L.badge[s]; what[n++] = concat(&a, "badge", seatname(s));
        }
        fixed[n] = L.score;    what[n++] = "score";
        fixed[n] = L.trumpbox; what[n++] = "trump";
        fixed[n] = L.menu;     what[n++] = "menu";
        fixed[n] = L.hint;     what[n++] = "hint";
        fixed[n] = L.bidbar;   what[n++] = "bidbar";
        for (i32 s = 0; s < 4; s++) {
            fixed[n] = cardrect(L.trick[s], L.cw, 0);
            what[n++] = concat(&a, "trick", seatname(s));
        }
        // Your hand: six cards, raised
        f32  hspan = L.step[SOUTH]*5 + L.hw;
        Rect hand  = {L.hand[SOUTH].x - hspan/2, L.hand[SOUTH].y - L.hw*CARDASPECT/2 - L.hlift,
                      hspan, L.hw*CARDASPECT + L.hlift};
        fixed[n] = hand; what[n++] = "hand";

        b32 ok = 1;
        Str bad = {};
        for (i32 j = 0; j < n; j++) {
            if (!inside(L.safe, fixed[j])) {
                ok = 0;
                bad = concat(&a, "outside ", what[j]);
            }
            for (i32 k = j+1; k < n; k++) {
                b32 allowed = (what[j]=="bidbar" && startswith(what[k], "trick")) ||
                              (what[k]=="bidbar" && startswith(what[j], "trick"));
                if (!allowed && overlaps(fixed[j], fixed[k])) {
                    ok = 0;
                    bad = concat(&a, concat(&a, what[j], " overlaps "), what[k]);
                }
            }
        }
        // Other hands: inside, and clear of the trick and the panels
        for (i32 s = 1; s < 4; s++) {
            b32  side = s != NORTH;
            f32  span = L.step[s]*4 + L.bw;
            Rect r    = side ? centered(L.hand[s], L.bw*CARDASPECT, span)
                             : centered(L.hand[s], span, L.bw*CARDASPECT);
            if (!inside(L.safe, r)) {
                ok = 0;
                bad = concat(&a, "outside hand", seatname(s));
            }
            for (i32 j = 0; j < n; j++) {
                if (overlaps(r, fixed[j])) {
                    ok = 0;
                    bad = concat(&a, concat(&a, "hand", seatname(s)), concat(&a, " overlaps ", what[j]));
                }
            }
        }
        expect(t, concat(&a, prefix, "fits"), ok ? Str("") : bad, "");

        // Legible text, tappable buttons
        b32 big = L.tsmall>=9*v.scale && L.tbody>=11*v.scale &&
                  L.btnh>=24*v.scale && L.menu.w>=24*v.scale;
        expect(t, concat(&a, prefix, "sizes"), yes(big), "yes");
    }
}
