// Geometry: an antialiased triangle mesh over one texture
//
// A frame is a single list of triangles over the font atlas, whose
// corner holds a block of white texels for untextured fills, so the
// platform draws it with one call. Vertex matches SDL_Vertex. Shapes
// are star-shaped polygons fanned from their centroid, with a fringe one
// pixel wide fading to transparent outside the edge for antialiasing.
// Every point passes through the Draw's affine transform first, so a
// whole card can be moved, turned and squeezed (a flip) as one.

struct Color {
    f32 r, g, b, a;
};

struct Vertex {
    f32   x, y;
    Color c;
    f32   u, v;
};
static_assert(sizeof(Vertex) == 32);

struct Pt {
    f32 x, y;
};

struct Rect {
    f32 x, y, w, h;
};

struct Mesh {
    Vertex *v;
    iz      len;
    iz      cap;
    f32     wu, wv;  // a white texel
};

// p' = (a*x + c*y + tx, b*x + d*y + ty)
struct Xform {
    f32 a = 1, b = 0, c = 0, d = 1;
    f32 tx = 0, ty = 0;
};

struct Font;

struct Draw {
    Mesh  *m;
    Font  *font;
    Xform  t;
    f32    alpha = 1;   // multiplies every color's alpha
    f32    pxscale;     // pixels per unit for text sizes; 0: from t
    Color  red;         // suit symbols in text: hearts and diamonds
    Color  black;       // clubs and spades; zero alpha: the text color
};

static Color rgb(i32 r, i32 g, i32 b, i32 a = 255)
{
    return {(f32)r/255, (f32)g/255, (f32)b/255, (f32)a/255};
}

static Color rgbhex(u32 x, f32 a = 1)
{
    return {(f32)(x>>16 & 255)/255, (f32)(x>>8 & 255)/255, (f32)(x & 255)/255, a};
}

static Color withalpha(Color c, f32 a)
{
    c.a *= a;
    return c;
}

static Color mix(Color x, Color y, f32 t)
{
    return {lerpf(x.r, y.r, t), lerpf(x.g, y.g, t), lerpf(x.b, y.b, t), lerpf(x.a, y.a, t)};
}

static b32 inrect(Rect r, f32 x, f32 y)
{
    return x>=r.x && y>=r.y && x<r.x+r.w && y<r.y+r.h;
}

static Rect inset(Rect r, f32 d)
{
    return {r.x+d, r.y+d, r.w-2*d, r.h-2*d};
}

static Rect insetxy(Rect r, f32 dx, f32 dy)
{
    return {r.x+dx, r.y+dy, r.w-2*dx, r.h-2*dy};
}

static Pt center(Rect r)
{
    return {r.x + r.w/2, r.y + r.h/2};
}

static Rect centered(Pt c, f32 w, f32 h)
{
    return {c.x - w/2, c.y - h/2, w, h};
}

static b32 overlaps(Rect a, Rect b)
{
    return a.x<b.x+b.w && b.x<a.x+a.w && a.y<b.y+b.h && b.y<a.y+a.h;
}


// Transforms

static Pt xf(Xform t, Pt p)
{
    return {t.a*p.x + t.c*p.y + t.tx, t.b*p.x + t.d*p.y + t.ty};
}

// t then u: the result applies t first.
static Xform compose(Xform u, Xform t)
{
    Xform r;
    r.a  = u.a*t.a  + u.c*t.b;
    r.b  = u.b*t.a  + u.d*t.b;
    r.c  = u.a*t.c  + u.c*t.d;
    r.d  = u.b*t.c  + u.d*t.d;
    r.tx = u.a*t.tx + u.c*t.ty + u.tx;
    r.ty = u.b*t.tx + u.d*t.ty + u.ty;
    return r;
}

// Local coordinates centered on the origin placed at (x, y), turned by
// angle radians, and scaled by (sx, sy) before turning.
static Xform placexf(f32 x, f32 y, f32 angle, f32 sx, f32 sy)
{
    f32 cs = cosf_(angle), sn = sinf_(angle);
    Xform t;
    t.a  =  cs*sx;
    t.b  =  sn*sx;
    t.c  = -sn*sy;
    t.d  =  cs*sy;
    t.tx = x;
    t.ty = y;
    return t;
}

static b32 isidentity(Xform t)
{
    return t.a==1 && t.b==0 && t.c==0 && t.d==1;
}

// Pixels per local unit, roughly, for picking curve detail.
static f32 xfscale(Xform t)
{
    return sqrtf_(absf(t.a*t.d - t.b*t.c));
}


// Mesh output

static Mesh newmesh(Arena *a, iz cap)
{
    Mesh m = {};
    m.v   = allocraw<Vertex>(a, cap);
    m.cap = cap;
    return m;
}

static void vert(Draw *d, Pt p, Color c)
{
    Mesh *m = d->m;
    if (m->len < m->cap) {
        m->v[m->len++] = {p.x, p.y, {c.r, c.g, c.b, c.a*d->alpha}, m->wu, m->wv};
    }
}

static void tri(Draw *d, Pt a, Color ca, Pt b, Color cb, Pt c, Color cc)
{
    if (d->m->len+3 > d->m->cap) {
        return;  // full: drop whole triangles only
    }
    vert(d, a, ca);
    vert(d, b, cb);
    vert(d, c, cc);
}

// A textured quad, corners given clockwise from the top-left, already
// in screen space.
static void quaduv(Draw *d, Pt *p, f32 u0, f32 v0, f32 u1, f32 v1, Color c)
{
    Mesh *m = d->m;
    if (m->len+6 > m->cap) {
        return;
    }
    Color k = {c.r, c.g, c.b, c.a*d->alpha};
    Vertex q[4] = {
        {p[0].x, p[0].y, k, u0, v0},
        {p[1].x, p[1].y, k, u1, v0},
        {p[2].x, p[2].y, k, u1, v1},
        {p[3].x, p[3].y, k, u0, v1},
    };
    m->v[m->len++] = q[0];
    m->v[m->len++] = q[1];
    m->v[m->len++] = q[2];
    m->v[m->len++] = q[0];
    m->v[m->len++] = q[2];
    m->v[m->len++] = q[3];
}

enum : i32 {
    MAXPOLY = 160,
};

// Fill a star-shaped polygon, given in local coordinates, with an
// antialiased edge. Each point may have its own color (cs), else c.
static void fillpolyc(Draw *d, Pt *local, i32 n, Color c, Color *cs)
{
    if (n<3 || n>MAXPOLY) {
        return;
    }
    Pt  p[MAXPOLY];
    Pt  o[MAXPOLY];
    Pt  mid  = {};
    f32 area = 0;
    for (i32 i = 0; i < n; i++) {
        p[i] = xf(d->t, local[i]);
        mid.x += p[i].x;
        mid.y += p[i].y;
    }
    mid.x /= (f32)n;
    mid.y /= (f32)n;
    for (i32 i = 0; i < n; i++) {
        Pt a = p[i], b = p[(i+1)%n];
        area += a.x*b.y - b.x*a.y;
    }
    f32 sign = area<0 ? -1.0f : 1.0f;

    // Offset each point outward along the bisector of its edges' normals.
    for (i32 i = 0; i < n; i++) {
        Pt  a  = p[(i+n-1)%n], b = p[i], c2 = p[(i+1)%n];
        f32 e1x = b.x-a.x,  e1y = b.y-a.y;
        f32 e2x = c2.x-b.x, e2y = c2.y-b.y;
        f32 l1 = sqrtf_(e1x*e1x + e1y*e1y);
        f32 l2 = sqrtf_(e2x*e2x + e2y*e2y);
        f32 n1x = 0, n1y = 0, n2x = 0, n2y = 0;
        if (l1 > 1e-6f) { n1x =  e1y/l1*sign; n1y = -e1x/l1*sign; }
        if (l2 > 1e-6f) { n2x =  e2y/l2*sign; n2y = -e2x/l2*sign; }
        f32 mx = n1x+n2x, my = n1y+n2y;
        f32 ml = sqrtf_(mx*mx + my*my);
        if (ml < 1e-6f) {
            o[i] = b;
            continue;
        }
        mx /= ml;
        my /= ml;
        f32 dot = mx*n1x + my*n1y;
        if (l1 <= 1e-6f) dot = mx*n2x + my*n2y;
        f32 k = dot>0.25f ? 1/dot : 4.0f;
        o[i] = {b.x + mx*k, b.y + my*k};
    }

    Color cm = c;
    if (cs) {
        cm = {};
        for (i32 i = 0; i < n; i++) {
            cm.r += cs[i].r; cm.g += cs[i].g; cm.b += cs[i].b; cm.a += cs[i].a;
        }
        cm.r /= (f32)n; cm.g /= (f32)n; cm.b /= (f32)n; cm.a /= (f32)n;
    }
    for (i32 i = 0; i < n; i++) {
        i32   j  = (i+1) % n;
        Color ci = cs ? cs[i] : c;
        Color cj = cs ? cs[j] : c;
        Color fi = withalpha(ci, 0);
        Color fj = withalpha(cj, 0);
        tri(d, mid, cm, p[i], ci, p[j], cj);
        tri(d, p[i], ci, p[j], cj, o[j], fj);
        tri(d, p[i], ci, o[j], fj, o[i], fi);
    }
}

// A convex polygon without the antialiased fringe, a third the
// triangles: for fine texture whose edges nobody inspects.
static void fillpolyflat(Draw *d, Pt *local, i32 n, Color c)
{
    Pt a = xf(d->t, local[0]);
    Pt b = xf(d->t, local[1]);
    for (i32 i = 2; i < n; i++) {
        Pt e = xf(d->t, local[i]);
        tri(d, a, c, b, c, e, c);
        b = e;
    }
}

static void fillpoly(Draw *d, Pt *local, i32 n, Color c)
{
    fillpolyc(d, local, n, c, 0);
}

// Number of segments for a quarter circle of radius r (local units).
// Few enough to keep each chord within a third of a pixel of the arc.
static i32 arcsegs(Draw *d, f32 r)
{
    f32 px = r * xfscale(d->t);
    if (px < 1) {
        return 2;
    }
    f32 c = 1 - 0.33f/px;
    f32 h = sqrtf_(2*(1 - c));  // about the half angle of one chord
    i32 n = (i32)((PI/4)/h + 0.999f);
    return n<2 ? 2 : n>12 ? 12 : n;
}

// Points of a rounded rectangle, clockwise from the top-left corner.
static i32 roundpts(Draw *d, Rect r, f32 rad, Pt *out)
{
    rad = minf(rad, minf(r.w, r.h)/2);
    i32 seg = arcsegs(d, rad);
    struct { f32 x, y, a0; } corners[4] = {
        {r.x + rad,       r.y + rad,       PI},
        {r.x + r.w - rad, r.y + rad,       PI*1.5f},
        {r.x + r.w - rad, r.y + r.h - rad, 0},
        {r.x + rad,       r.y + r.h - rad, PI*0.5f},
    };
    i32 n = 0;
    for (i32 k = 0; k < 4; k++) {
        for (i32 s = 0; s <= seg; s++) {
            f32 a = corners[k].a0 + (PI/2)*(f32)s/(f32)seg;
            out[n++] = {corners[k].x + rad*cosf_(a), corners[k].y + rad*sinf_(a)};
        }
    }
    return n;
}

static void fillround(Draw *d, Rect r, f32 rad, Color c)
{
    Pt  pts[MAXPOLY];
    i32 n = roundpts(d, r, rad, pts);
    fillpoly(d, pts, n, c);
}

// A rounded rectangle shaded from top to bottom.
static void fillroundgrad(Draw *d, Rect r, f32 rad, Color top, Color bottom)
{
    Pt    pts[MAXPOLY];
    Color cs[MAXPOLY];
    i32   n = roundpts(d, r, rad, pts);
    for (i32 i = 0; i < n; i++) {
        cs[i] = mix(top, bottom, clampf((pts[i].y - r.y)/r.h, 0, 1));
    }
    fillpolyc(d, pts, n, top, cs);
}

static void fillrect(Draw *d, Rect r, Color c)
{
    Pt pts[4] = {{r.x, r.y}, {r.x+r.w, r.y}, {r.x+r.w, r.y+r.h}, {r.x, r.y+r.h}};
    fillpoly(d, pts, 4, c);
}

static void fillcircle(Draw *d, Pt c, f32 r, Color col)
{
    i32 n = 4*arcsegs(d, r);
    Pt  pts[MAXPOLY];
    for (i32 i = 0; i < n; i++) {
        f32 a = 2*PI*(f32)i/(f32)n;
        pts[i] = {c.x + r*cosf_(a), c.y + r*sinf_(a)};
    }
    fillpoly(d, pts, n, col);
}

// A band between two concentric rounded rectangles: a frame or a glow.
static void strokeround(Draw *d, Rect r, f32 rad, f32 w, Color c)
{
    Pt  outer[MAXPOLY], inner[MAXPOLY];
    Rect ri = inset(r, w);
    f32 radi = maxf(rad - w, 0.01f);
    // Matching point counts: the same segment count on both contours.
    i32 seg = arcsegs(d, rad);
    i32 n = 0;
    struct { f32 x, y, xi, yi, a0; } cn[4] = {
        {r.x + rad,       r.y + rad,       ri.x + radi,        ri.y + radi,        PI},
        {r.x + r.w - rad, r.y + rad,       ri.x + ri.w - radi, ri.y + radi,        PI*1.5f},
        {r.x + r.w - rad, r.y + r.h - rad, ri.x + ri.w - radi, ri.y + ri.h - radi, 0},
        {r.x + rad,       r.y + r.h - rad, ri.x + radi,        ri.y + ri.h - radi, PI*0.5f},
    };
    for (i32 k = 0; k < 4; k++) {
        for (i32 s = 0; s <= seg; s++) {
            f32 a = cn[k].a0 + (PI/2)*(f32)s/(f32)seg;
            outer[n] = xf(d->t, {cn[k].x  + rad*cosf_(a),  cn[k].y  + rad*sinf_(a)});
            inner[n] = xf(d->t, {cn[k].xi + radi*cosf_(a), cn[k].yi + radi*sinf_(a)});
            n++;
        }
    }
    // Fringes: outward from the outer contour, inward from the inner one,
    // each along the direction between the two contours.
    Color f = withalpha(c, 0);
    for (i32 i = 0; i < n; i++) {
        i32 j = (i+1) % n;
        Pt  a = outer[i], b = outer[j], e = inner[j], g = inner[i];
        tri(d, a, c, b, c, e, c);
        tri(d, a, c, e, c, g, c);
        Pt ao = a, bo = b, gi = g, ei = e;
        f32 dx = a.x-g.x, dy = a.y-g.y, l = sqrtf_(dx*dx + dy*dy);
        if (l > 1e-6f) { ao.x += dx/l; ao.y += dy/l; gi.x -= dx/l; gi.y -= dy/l; }
        dx = b.x-e.x; dy = b.y-e.y; l = sqrtf_(dx*dx + dy*dy);
        if (l > 1e-6f) { bo.x += dx/l; bo.y += dy/l; ei.x -= dx/l; ei.y -= dy/l; }
        tri(d, a, c, bo, f, b, c);
        tri(d, a, c, ao, f, bo, f);
        tri(d, g, c, e, c, ei, f);
        tri(d, g, c, ei, f, gi, f);
    }
}

static void line(Draw *d, Pt a, Pt b, f32 w, Color c)
{
    f32 dx = b.x-a.x, dy = b.y-a.y, l = sqrtf_(dx*dx + dy*dy);
    if (l < 1e-6f) {
        return;
    }
    f32 nx = -dy/l*w/2, ny = dx/l*w/2;
    Pt p[4] = {{a.x+nx, a.y+ny}, {b.x+nx, b.y+ny}, {b.x-nx, b.y-ny}, {a.x-nx, a.y-ny}};
    fillpoly(d, p, 4, c);
}


// Suit symbols, each in a box of the given height centered on (x, y)

static Pt const *heartunit()
{
    // Parametric heart, sampled once and normalized to height 1 about
    // the center of its bounding box (as solitaire's render.cpp).
    static Pt  pts[48];
    static b32 done;
    if (!done) {
        f32 minx = 1e9f, maxx = -1e9f, miny = 1e9f, maxy = -1e9f;
        for (i32 i = 0; i < 48; i++) {
            f32 t  = 2*PI*(f32)i/48;
            f32 s  = sinf_(t);
            f32 x  = 16*s*s*s;
            f32 y  = -(13*cosf_(t) - 5*cosf_(2*t) - 2*cosf_(3*t) - cosf_(4*t));
            pts[i] = {x, y};
            minx = minf(minx, x); maxx = maxf(maxx, x);
            miny = minf(miny, y); maxy = maxf(maxy, y);
        }
        f32 cx = (minx+maxx)/2, cy = (miny+maxy)/2, sc = 1/(maxy-miny);
        for (i32 i = 0; i < 48; i++) {
            pts[i] = {(pts[i].x-cx)*sc, (pts[i].y-cy)*sc};
        }
        done = 1;
    }
    return pts;
}

static Color const SUITRED   = {0.78f, 0.10f, 0.16f, 1};
static Color const SUITBLACK = {0.11f, 0.11f, 0.15f, 1};

static Color suitcolor(i32 s)
{
    return s>=2 ? SUITRED : SUITBLACK;
}

// Every so many of a heart's 48 points, for its size in pixels: a small
// pip needs far fewer than a large one to look round.
static i32 heartstep(Draw *d, f32 size)
{
    f32 px = size * xfscale(d->t);
    return px<14 ? 4 : px<26 ? 3 : px<60 ? 2 : 1;
}

static void drawsuit(Draw *d, i32 s, f32 x, f32 y, f32 size, Color c)
{
    Pt p[48];
    switch (s) {
    case 3: {  // hearts
        Pt const *u = heartunit();
        i32 step = heartstep(d, size), n = 0;
        for (i32 i = 0; i < 48; i += step) {
            p[n++] = {x + u[i].x*size*1.08f, y + u[i].y*size*0.95f};
        }
        fillpoly(d, p, n, c);
    } break;
    case 1: {  // spades: an inverted heart on a flared stem
        Pt const *u = heartunit();
        i32 step = heartstep(d, size), n = 0;
        for (i32 i = 0; i < 48; i += step) {
            p[n++] = {x + u[i].x*size*1.0f, y - 0.08f*size - u[i].y*size*0.78f};
        }
        fillpoly(d, p, n, c);
        Pt stem[4] = {
            {x - 0.05f*size, y + 0.05f*size}, {x + 0.05f*size, y + 0.05f*size},
            {x + 0.20f*size, y + 0.50f*size}, {x - 0.20f*size, y + 0.50f*size},
        };
        fillpoly(d, stem, 4, c);
    } break;
    case 2: {  // diamonds
        Pt q[4] = {
            {x, y - 0.50f*size}, {x + 0.38f*size, y},
            {x, y + 0.50f*size}, {x - 0.38f*size, y},
        };
        fillpoly(d, q, 4, c);
    } break;
    case 0: {  // clubs
        f32 r = 0.215f*size;
        fillcircle(d, {x, y - 0.25f*size}, r, c);
        fillcircle(d, {x - 0.245f*size, y + 0.06f*size}, r, c);
        fillcircle(d, {x + 0.245f*size, y + 0.06f*size}, r, c);
        fillcircle(d, {x, y + 0.02f*size}, 0.12f*size, c);
        Pt stem[4] = {
            {x - 0.05f*size, y}, {x + 0.05f*size, y},
            {x + 0.19f*size, y + 0.50f*size}, {x - 0.19f*size, y + 0.50f*size},
        };
        fillpoly(d, stem, 4, c);
    } break;
    }
}
