// Text: an embedded Inter Bold, baked into the atlas at the sizes in use
//
// Sizes are cap heights in pixels, snapped to buckets 7% apart, and text
// is drawn at exactly the bucket's size, so glyphs land unscaled and
// crisp at any density. Every size a frame asks for is noted; a size not
// yet baked is drawn this once from the nearest one, and fontupdate then
// rebakes the atlas with all the sizes the frame used. The platform
// uploads the atlas again when its version changes.
//
// The atlas is white RGBA with coverage in alpha, so vertex colors tint
// it, and its top rows are a block of opaque white for untextured fills.
// Bytes 1 to 4 in text are suit symbols, drawn as shapes.

#if __GNUC__
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wconversion"
#  pragma GCC diagnostic ignored "-Wsign-compare"
#  pragma GCC diagnostic ignored "-Wunused-function"
#  pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#  pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#  pragma GCC diagnostic ignored "-Wpedantic"
#  ifdef __clang__
#    pragma clang diagnostic ignored "-Wc23-extensions"
#    pragma clang diagnostic ignored "-Wc++26-extensions"
#  else
#    pragma GCC diagnostic ignored "-Wc++26-extensions"
#  endif
#endif

static void *stbttalloc(void *arena, uz size)
{
    Arena *a = (Arena *)arena;
    return alloc<u64>(a, (iz)(size + 7)/8);
}

#ifndef NULL
#  define NULL 0
#endif
#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_ifloor(x)     ((int)__builtin_floor(x))
#define STBTT_iceil(x)      ((int)__builtin_ceil(x))
#define STBTT_sqrt(x)       __builtin_sqrt(x)
#define STBTT_pow(x, y)     __builtin_pow(x, y)
#define STBTT_fmod(x, y)    __builtin_fmod(x, y)
#define STBTT_cos(x)        __builtin_cos(x)
#define STBTT_acos(x)       __builtin_acos(x)
#define STBTT_fabs(x)       __builtin_fabs(x)
#define STBTT_malloc(x, u)  stbttalloc(u, x)
#define STBTT_free(x, u)    ((void)(x), (void)(u))
#define STBTT_assert(x)     assert(x)
#define STBTT_strlen(x)     __builtin_strlen(x)
#define STBTT_memcpy        __builtin_memcpy
#define STBTT_memset        __builtin_memset
#define memcpy __builtin_memcpy  // one call site skips STBTT_memcpy
#include "stb_truetype.h"
#undef memcpy

static u8 const interttf[] = {
    #embed "../assets/Inter-Bold-subset.ttf"
};

#if __GNUC__
#  pragma GCC diagnostic pop
#endif

enum : i32 {
    ATLAS_W    = 2048,
    ATLAS_H    = 1024,
    WHITE_ROWS = 4,
    NBUCKETS   = 64,
    MAXBAKE    = 80,   // largest cap height baked; larger text scales up
};

struct Font {
    stbtt_fontinfo   info;
    f32              capratio;  // cap height per pixel height
    u8              *rgba;      // ATLAS_W x ATLAS_H
    i32              version;   // bumped at every bake
    b32              baked[NBUCKETS];
    stbtt_packedchar chars[NBUCKETS][95];
    b32              used[NBUCKETS];  // asked for this frame
    b32              missing;         // a size used this frame isn't baked
    i64              bakedat;         // when, for pacing rebakes
};

// Cap height of bucket k: 6 px growing 7% a step.
static f32 bucketpx(i32 k)
{
    f32 px = 6;
    for (i32 i = 0; i < k; i++) {
        px *= 1.07f;
    }
    return px;
}

static i32 bucketof(f32 px)
{
    i32 k = 0;
    for (f32 b = 6; k<NBUCKETS-1 && b*1.035f<px; b *= 1.07f) {
        k++;
    }
    return k;
}

// Bake the given buckets; ones that don't fit are left unbaked.
static void bake(Font *f, b32 *want, Arena scratch)
{
    fillbytes(f->baked, 0, sizeof(f->baked));
    u8 *alpha = allocbytes(&scratch, (iz)ATLAS_W*ATLAS_H);
    fillbytes(alpha, 0, (iz)ATLAS_W*ATLAS_H);

    stbtt_pack_context pc;
    stbtt_PackBegin(&pc, alpha + ATLAS_W*WHITE_ROWS, ATLAS_W,
                    ATLAS_H-WHITE_ROWS, ATLAS_W, 1, &scratch);
    for (i32 k = 0; k < NBUCKETS; k++) {
        if (!want[k] || bucketpx(k)>MAXBAKE) {
            continue;
        }
        Arena t = scratch;
        pc.user_allocator_context = &t;
        f32 height = bucketpx(k) / f->capratio;
        if (stbtt_PackFontRange(&pc, interttf, 0, height, 32, 95, f->chars[k])) {
            f->baked[k] = 1;
        }
    }
    stbtt_PackEnd(&pc);

    for (iz i = 0; i < (iz)ATLAS_W*ATLAS_H; i++) {
        u8 a = i<ATLAS_W*WHITE_ROWS ? 255 : alpha[i];
        f->rgba[4*i+0] = 255;
        f->rgba[4*i+1] = 255;
        f->rgba[4*i+2] = 255;
        f->rgba[4*i+3] = a;
    }
    // Glyph rects shifted down past the white rows
    for (i32 k = 0; k < NBUCKETS; k++) {
        if (f->baked[k]) {
            for (i32 c = 0; c < 95; c++) {
                f->chars[k][c].y0 = (unsigned short)(f->chars[k][c].y0 + WHITE_ROWS);
                f->chars[k][c].y1 = (unsigned short)(f->chars[k][c].y1 + WHITE_ROWS);
            }
        }
    }
    f->version++;
}

static Font *newfont(Arena *perm)
{
    Font *f = alloc<Font>(perm);
    f->rgba = allocbytes(perm, (iz)ATLAS_W*ATLAS_H*4);
    stbtt_InitFont(&f->info, interttf, 0);
    f32 sc = stbtt_ScaleForPixelHeight(&f->info, 100);
    i32 x0, y0, x1, y1;
    f->capratio = 0.72f;
    if (stbtt_GetCodepointBox(&f->info, 'H', &x0, &y0, &x1, &y1) && y1>0) {
        f->capratio = (f32)y1 * sc / 100;
    }
    return f;
}

// White texel coordinates for untextured fills.
static void whiteuv(f32 *u, f32 *v)
{
    *u = 2.0f / ATLAS_W;
    *v = 2.0f / ATLAS_H;
}

// The baked bucket nearest k, or -1 if none.
static i32 nearestbaked(Font *f, i32 k)
{
    for (i32 d = 0; d < NBUCKETS; d++) {
        if (k-d>=0 && f->baked[k-d]) return k-d;
        if (k+d<NBUCKETS && f->baked[k+d]) return k+d;
    }
    return -1;
}

struct Fit {
    i32 k;      // bucket to draw from, -1 for none
    f32 scale;  // from its size to the size wanted
    f32 px;     // the size drawn
};

static Fit fitsize(Font *f, f32 px)
{
    Fit r = {};
    i32 k = bucketof(px);
    f->used[k] = 1;
    r.px = bucketpx(k);
    if (r.px > MAXBAKE) {
        // Larger than baked: the largest bake, scaled up.
        i32 top = 0;
        for (i32 i = 0; i < NBUCKETS && bucketpx(i)<=MAXBAKE; i++) {
            top = i;
        }
        f->used[top] = 1;
        f->used[k] = 0;
        k = top;
        r.px = px;
    }
    if (!f->baked[k]) {
        f->missing = 1;
        k = nearestbaked(f, k);
    }
    r.k = k;
    r.scale = k>=0 ? r.px/bucketpx(k) : 1;
    return r;
}

static f32 suitadvance(f32 px)
{
    return px * 1.05f;
}

// Width of text at cap height px.
static f32 textwidth(Font *f, f32 px, Str s)
{
    Fit fit = fitsize(f, px);
    f32 w   = 0;
    for (iz i = 0; i < s.len; i++) {
        i32 c = s[i];
        if (c>=1 && c<=4) {
            w += suitadvance(fit.px);
        } else if (fit.k >= 0) {
            c = c<32 || c>126 ? '?' : c;
            w += f->chars[fit.k][c-32].xadvance * fit.scale;
        }
    }
    return w;
}

// The size actually drawn for a request of px.
static f32 textpx(Font *f, f32 px)
{
    return fitsize(f, px).px;
}

// Width of text in a Draw's local units.
static f32 textwidthd(Draw *d, f32 px, Str s)
{
    f32 ps = xfscale(d->t);
    return textwidth(d->font, px*ps, s) / ps;
}

// Cap height actually drawn, in a Draw's local units.
static f32 textpxd(Draw *d, f32 px)
{
    f32 ps = xfscale(d->t);
    return textpx(d->font, px*ps) / ps;
}

// Draw text with the top of its capitals at y, in local units.
static void text(Draw *d, f32 x, f32 y, f32 px, Color col, Str s)
{
    Font *f    = d->font;
    f32   ps   = xfscale(d->t);
    Fit   fit  = fitsize(f, px*ps);
    f32   sc   = fit.scale / ps;  // baked pixels to local units
    f32   cap  = fit.px / ps;
    b32   snap = isidentity(d->t) && fit.scale==1;
    if (snap) {
        x = roundf_(x);
        y = roundf_(y);
    }
    f32 base = y + cap;  // the baseline
    f32 pen  = x;
    for (iz i = 0; i < s.len; i++) {
        i32 c = s[i];
        if (c>=1 && c<=4) {
            i32   suit = c - 1;
            Color k    = suit>=2 ? d->red : d->black;
            if (k.a == 0) {
                k = col;
            }
            f32 adv = suitadvance(cap);
            drawsuit(d, suit, pen + adv/2, y + cap*0.5f, cap*1.02f,
                     withalpha(k, col.a));
            pen += adv;
            continue;
        }
        if (fit.k < 0) {
            continue;
        }
        c = c<32 || c>126 ? '?' : c;
        stbtt_packedchar *pc = &f->chars[fit.k][c-32];
        if (pc->x1 > pc->x0) {
            f32 x0 = pen + pc->xoff*sc;
            f32 y0 = base + pc->yoff*sc;
            f32 x1 = pen + pc->xoff2*sc;
            f32 y1 = base + pc->yoff2*sc;
            if (snap) {
                // Packed glyphs carry fractional offsets: keep each on
                // whole pixels so it samples its texels one to one.
                f32 w = x1 - x0, h = y1 - y0;
                x0 = roundf_(x0);
                y0 = roundf_(y0);
                x1 = x0 + w;
                y1 = y0 + h;
            }
            Pt q[4] = {
                xf(d->t, {x0, y0}), xf(d->t, {x1, y0}),
                xf(d->t, {x1, y1}), xf(d->t, {x0, y1}),
            };
            quaduv(d, q, (f32)pc->x0/ATLAS_W, (f32)pc->y0/ATLAS_H,
                   (f32)pc->x1/ATLAS_W, (f32)pc->y1/ATLAS_H, col);
        }
        pen += pc->xadvance * sc;
    }
}

static void textc(Draw *d, f32 cx, f32 y, f32 px, Color col, Str s)
{
    text(d, cx - textwidthd(d, px, s)/2, y, px, col, s);
}

static void textr(Draw *d, f32 rx, f32 y, f32 px, Color col, Str s)
{
    text(d, rx - textwidthd(d, px, s), y, px, col, s);
}

// Text centered in a rectangle both ways, shrunk to fit its width.
static void textin(Draw *d, Rect r, f32 px, Color col, Str s)
{
    f32 w = textwidthd(d, px, s);
    if (w > r.w*0.9f) {
        px *= r.w*0.9f/w;
    }
    f32 cap = textpxd(d, px);
    textc(d, r.x + r.w/2, r.y + (r.h - cap)/2, px, col, s);
}

// Text left-aligned and centered vertically in a rectangle, shrunk to
// fit its width.
static void textleft(Draw *d, Rect r, f32 px, Color col, Str s)
{
    f32 w = textwidthd(d, px, s);
    if (w > r.w) {
        px *= r.w/w;
    }
    f32 cap = textpxd(d, px);
    text(d, r.x, r.y + (r.h - cap)/2, px, col, s);
}

// Greedy word wrap at spaces into lines no wider than maxw.
static i32 wraptext(Font *f, f32 px, f32 maxw, Str s, Str *lines, i32 max)
{
    i32 n = 0;
    while (s.len && n<max) {
        s = trimleft(s);
        iz best = 0, i = 0;
        for (;;) {
            iz j = i;
            for (; j<s.len && s[j]!=' ' && s[j]!='\n'; j++) {}
            if (best && textwidth(f, px, takehead(s, j))>maxw) {
                break;
            }
            best = j;
            if (j==s.len || s[j]=='\n') {
                break;
            }
            i = j + 1;
        }
        if (!best && s.len) {
            break;
        }
        lines[n++] = takehead(s, best);
        s = cuthead(s, best);
        if (s.len && s[0]=='\n') {
            s = cuthead(s, 1);
        }
    }
    return n;
}

enum : i32 {
    FONT_OK,       // every size the frame used is baked
    FONT_REBAKED,  // the atlas changed: draw the frame again
    FONT_PENDING,  // a rebake is due after the returned delay
};

// After a frame: rebake the atlas to hold every size the frame used, at
// most every so often (a window being resized asks for new sizes on
// every frame), then start a fresh slate for the next frame.
static i32 fontupdate(Font *f, i64 now, Arena scratch)
{
    i32 r = FONT_OK;
    if (f->missing) {
        if (!f->version || now-f->bakedat>=150) {
            bake(f, f->used, scratch);
            f->bakedat = now;
            r = FONT_REBAKED;
        } else {
            r = FONT_PENDING;
        }
    }
    f->missing = 0;
    fillbytes(f->used, 0, sizeof(f->used));
    return r;
}
