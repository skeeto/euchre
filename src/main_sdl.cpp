// SDL3 platform layer: window, input, audio, saving, and frame pacing
// $ cmake -B build && cmake --build build && ./build/euchre
// $ emcmake cmake -B build-web && cmake --build build-web
//
// The same layer serves the desktop and, under Emscripten, the web,
// where the canvas fills the page, the game saves to localStorage, and
// the page's CSS supplies a phone's safe-area insets.
//
// SDL's main callbacks drive the program, as the web requires.
// The core hands back one triangle list per frame over its font atlas,
// drawn with a single SDL_RenderGeometry. Frames are drawn only when
// something changed or moves: appupdate says how long nothing will
// happen, and the loop then blocks on events, with a timer to wake it
// at that deadline, rather than spinning.
//
// Developer options:
//   --save FILE      keep the game in FILE instead of the preferences
//   --seed N         seed a fresh game
//   --autoplay       a computer plays your seat too, at speed
//   --shot WxH[@S] FILE
//                    render headlessly to a BMP once the table settles,
//                    with --until PHASE (bid1 bid2 discard play trick
//                    passed score over) and --open PANEL (menu players
//                    rules stats help welcome)
//   --icon FILE      write the Windows icon (assets/euchre.ico)
//   --png SIZE[m] FILE
//                    write a PNG icon for the web (web/icon-*.png), "m"
//                    for a maskable one
//
// This is free and unencumbered software released into the public domain.
#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#ifdef __EMSCRIPTEN__
#  include <emscripten.h>
#else
   // Only for --png, a developer tool
#  define STB_IMAGE_WRITE_IMPLEMENTATION
#  define STB_IMAGE_WRITE_STATIC
#  define STBI_WRITE_NO_STDIO
#  define STBIW_MALLOC(n)      SDL_malloc(n)
#  define STBIW_REALLOC(p, n)  SDL_realloc(p, n)
#  define STBIW_FREE(p)        SDL_free(p)
#  define STBIW_MEMMOVE(d, s, n) SDL_memmove(d, s, n)
#  define STBIW_ASSERT(c)      SDL_assert(c)
#  include "stb_image_write.h"
#endif
#include "euchre.cpp"

enum : i32 {
    MAXVOICES = 16,
    MEMSIZE   = 1 << 27,
};

struct Voice {
    i32 sfx;
    iz  pos;
};

struct Os {
    SDL_Window       *window;
    SDL_Renderer     *renderer;
    SDL_Texture      *atlas;
    i32               atlasversion;
    SDL_Cursor       *arrow;
    SDL_Cursor       *hand;
    i32               cursor;
    SDL_AudioStream  *stream;
    Clip             *clips;
    Voice             voices[MAXVOICES];
    i32               nvoices;
    float             mix[4096];
    char             *savepath;
    SDL_TimerID       timer;
    b32               blocking;
    App              *app;
};

[[noreturn]] static void os_oom()
{
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Euchre", "Out of memory", 0);
    SDL_Quit();
    __builtin_trap();
}

#ifdef __EMSCRIPTEN__
// On the web the game lives in localStorage, under one key. A setItem
// replaces the whole value, so a save is atomic.
EM_JS(int, web_loadlen, (), {
    var v = null;
    try { v = localStorage.getItem("euchre"); } catch (e) {}
    if (v === null) return -1;
    Module.euchreLoaded = new TextEncoder().encode(v);
    return Module.euchreLoaded.length;
});

EM_JS(void, web_loadcopy, (unsigned char *dst), {
    HEAPU8.set(Module.euchreLoaded, dst);
    Module.euchreLoaded = null;
});

EM_JS(int, web_save, (unsigned char const *p, int len), {
    try {
        var s = new TextDecoder().decode(HEAPU8.subarray(p, p+len));
        localStorage.setItem("euchre", s);
        return 1;
    } catch (e) {
        return 0;
    }
});

// A safe-area inset in CSS pixels, which the page's style exposes
EM_JS(double, web_inset, (int side), {
    var n = ["top", "right", "bottom", "left"][side];
    var v = getComputedStyle(document.documentElement).getPropertyValue("--sai-" + n);
    return parseFloat(v) || 0;
});

static Str os_load(Os *, Arena *a)
{
    i32 len = web_loadlen();
    if (len < 0) {
        return {};
    }
    Str r = {allocbytes(a, len), len};
    web_loadcopy(r.data);
    return r;
}

static b32 os_save(Os *, Arena, Str state)
{
    return web_save(state.data, (int)state.len);
}

#else
static Str os_load(Os *os, Arena *a)
{
    uz    len  = 0;
    void *data = SDL_LoadFile(os->savepath, &len);
    if (!data) {
        return {};
    }
    Str r = clone(a, {(u8 *)data, (iz)len});
    SDL_free(data);
    if (!r.data) {
        r.data = (u8 *)a->beg;  // present, if empty
    }
    return r;
}

static b32 os_save(Os *os, Arena scratch, Str state)
{
    Str tmp = concat(&scratch, {(u8 *)os->savepath, (iz)SDL_strlen(os->savepath)}, ".tmp");
    tmp = concat(&scratch, tmp, Str{(u8 *)"", 1});
    char *path = (char *)tmp.data;
    return SDL_SaveFile(path, state.data, (uz)state.len) &&
           SDL_RenamePath(path, os->savepath);
}
#endif

static void os_play(Os *os, i32 sfx)
{
    if (!os->stream || !os->clips || sfx<0 || sfx>=SFX_COUNT) {
        return;
    }
    SDL_LockAudioStream(os->stream);
    if (os->nvoices < MAXVOICES) {
        os->voices[os->nvoices++] = {sfx, 0};
    }
    SDL_UnlockAudioStream(os->stream);
}

static void SDLCALL feed(void *ud, SDL_AudioStream *stream, int need, int)
{
    Os *os     = (Os *)ud;
    i32 frames = need / (i32)(2*sizeof(float));
    while (frames>0 && os->nvoices) {
        i32 n = frames < (i32)countof(os->mix)/2 ? frames : (i32)countof(os->mix)/2;
        for (i32 i = 0; i < 2*n; i++) {
            os->mix[i] = 0;
        }
        for (i32 v = 0; v < os->nvoices; v++) {
            Clip *c = os->clips + os->voices[v].sfx;
            iz    p = os->voices[v].pos;
            for (i32 i = 0; i<n && p<c->len; i++, p++) {
                os->mix[2*i+0] += c->samples[p];
                os->mix[2*i+1] += c->samples[p];
            }
            os->voices[v].pos = p;
        }
        for (i32 i = 0; i < 2*n; i++) {
            float x = os->mix[i];
            os->mix[i] = x / (1 + (x<0 ? -x : x)*0.6f);  // soft clip
        }
        i32 live = 0;
        for (i32 v = 0; v < os->nvoices; v++) {
            if (os->voices[v].pos < os->clips[os->voices[v].sfx].len) {
                os->voices[live++] = os->voices[v];
            }
        }
        os->nvoices = live;
        SDL_PutAudioStreamData(stream, os->mix, n*2*(i32)sizeof(float));
        frames -= n;
    }
}

static Str cstrarg(char *s)
{
    return {(u8 *)s, (iz)SDL_strlen(s)};
}

static Uint32 SDLCALL wake(void *, SDL_TimerID, Uint32)
{
    SDL_Event e = {};
    e.type = SDL_EVENT_USER;
    SDL_PushEvent(&e);
    return 0;
}

static void uploadatlas(Os *os, App *app)
{
    Font *f = app->font;
    if (os->atlas && os->atlasversion==f->version) {
        return;
    }
    if (!os->atlas) {
        os->atlas = SDL_CreateTexture(os->renderer, SDL_PIXELFORMAT_ABGR8888,
                                      SDL_TEXTUREACCESS_STATIC, ATLAS_W, ATLAS_H);
        SDL_SetTextureBlendMode(os->atlas, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(os->atlas, SDL_SCALEMODE_LINEAR);
    }
    SDL_UpdateTexture(os->atlas, 0, f->rgba, ATLAS_W*4);
    os->atlasversion = f->version;
}

// Render one frame to the current target; returns the mesh drawn.
static void renderframe(Os *os, App *app, View v, i64 now)
{
    Mesh m = apprender(app, v, now);
    uploadatlas(os, app);
    SDL_SetRenderDrawColor(os->renderer, 0, 0, 0, 255);
    SDL_RenderClear(os->renderer);
    if (m.len) {
        SDL_RenderGeometry(os->renderer, os->atlas, (SDL_Vertex *)m.v,
                           (int)m.len, 0, 0);
    }
}

static View windowview(Os *os)
{
    View v = {};
    int  w = 0, h = 0;
    SDL_GetCurrentRenderOutputSize(os->renderer, &w, &h);
    v.w     = (f32)w;
    v.h     = (f32)h;
    v.scale = SDL_GetWindowDisplayScale(os->window);
    if (v.scale <= 0) {
        v.scale = 1;
    }
    #ifdef __EMSCRIPTEN__
    // SDL doesn't report a phone's notch and home bar on the web
    int pw = 0, ph = 0;
    SDL_GetWindowSize(os->window, &pw, &ph);
    f32 ratio = pw>0 ? v.w/(f32)pw : 1;
    for (i32 i = 0; i < 4; i++) {
        v.inset[i] = (f32)web_inset(i) * ratio;
    }
    #endif
    return v;
}

struct Shot {
    i32   w, h;
    f32   scale;
    char *path;
};

static Shot parseshot(char *size, char *path)
{
    Shot r = {};
    Str  s = cstrarg(size);
    Cut  x = cut(s, 'x');
    Cut  at = cut(x.tail, '@');
    Int  w = parseint(x.head);
    Int  h = parseint(at.head);
    r.scale = 1;
    if (at.ok) {
        Int sc = parseint(at.tail);
        r.scale = sc.ok ? (f32)sc.val : 1;
    }
    if (x.ok && w.ok && h.ok && w.val>0 && h.val>0 && w.val<=8192 && h.val<=8192) {
        r.w    = (i32)w.val;
        r.h    = (i32)h.val;
        r.path = path;
    }
    return r;
}

// Draw the settled table headlessly with the software renderer.
static b32 shot(Os *os, App *app, Shot s)
{
    SDL_Surface *surf = SDL_CreateSurface(s.w, s.h, SDL_PIXELFORMAT_ARGB8888);
    if (!surf) {
        return 0;
    }
    os->renderer = SDL_CreateSoftwareRenderer(surf);
    if (!os->renderer) {
        return 0;
    }
    View v  = {(f32)s.w, (f32)s.h, {}, s.scale};
    i64 now = 1000;
    // Headless, the game runs on without frames until it settles...
    for (i32 i = 0; i<200000 && !appsettled(app); i++) {
        appupdate(app, now);
        now += 40;
    }
    // ...then frames: a first to lay out and ask for text sizes, more
    // once the atlas holds them and the cards are in place
    for (i32 i = 0; i < 4; i++) {
        appupdate(app, now);
        renderframe(os, app, v, now);
        now += 200;
    }
    SDL_RenderPresent(os->renderer);
    b32 ok = SDL_SaveBMP(surf, s.path);
    SDL_DestroyRenderer(os->renderer);
    os->renderer = 0;
    SDL_DestroySurface(surf);
    return ok;
}

// Render the icon at the given size into RGBA pixels. The software
// renderer's triangles are hard edged, so draw at 4x and average down.
static u8 *rendericon(Os *os, App *app, i32 out, b32 maskable, Arena *a)
{
    i32 const    ss   = 4;
    i32          size = out * ss;
    SDL_Surface *surf = SDL_CreateSurface(size, size, SDL_PIXELFORMAT_ABGR8888);
    if (!surf) {
        return 0;
    }
    os->renderer = SDL_CreateSoftwareRenderer(surf);
    os->atlas    = 0;
    if (!os->renderer) {
        return 0;
    }
    SDL_SetRenderDrawBlendMode(os->renderer, SDL_BLENDMODE_NONE);
    for (i32 i = 0; i < 3; i++) {
        Mesh m = iconmesh(app, (f32)size, maskable, 1000 + 200*i);
        uploadatlas(os, app);
        SDL_SetRenderDrawColor(os->renderer, 0, 0, 0, 0);
        SDL_RenderClear(os->renderer);
        SDL_RenderGeometry(os->renderer, os->atlas, (SDL_Vertex *)m.v, (int)m.len, 0, 0);
    }
    SDL_RenderPresent(os->renderer);
    // Average each block, weighting color by alpha
    u8 *px = allocbytes(a, (iz)out*out*4);
    for (i32 y = 0; y < out; y++) {
        for (i32 x = 0; x < out; x++) {
            i32 sum[4] = {};
            for (i32 j = 0; j < ss; j++) {
                u8 *row = (u8 *)surf->pixels + (iz)(y*ss + j)*surf->pitch;
                for (i32 i = 0; i < ss; i++) {
                    u8 *p = row + (x*ss + i)*4;
                    sum[0] += p[0]*p[3];
                    sum[1] += p[1]*p[3];
                    sum[2] += p[2]*p[3];
                    sum[3] += p[3];
                }
            }
            u8 *q = px + ((iz)y*out + x)*4;
            for (i32 c = 0; c < 3; c++) {
                q[c] = (u8)(sum[3] ? (sum[c] + sum[3]/2)/sum[3] : 0);
            }
            q[3] = (u8)((sum[3] + ss*ss/2) / (ss*ss));
        }
    }
    SDL_DestroyTexture(os->atlas);
    os->atlas = 0;
    os->atlasversion = 0;
    SDL_DestroyRenderer(os->renderer);
    os->renderer = 0;
    SDL_DestroySurface(surf);
    return px;
}

static void seticon(Os *os, App *app)
{
    Arena t = app->scratch;
    i32   n = 64;
    u8   *px = rendericon(os, app, n, 0, &t);
    if (px) {
        SDL_Surface *s = SDL_CreateSurfaceFrom(n, n, SDL_PIXELFORMAT_ABGR8888, px, n*4);
        if (s) {
            SDL_SetWindowIcon(os->window, s);
            SDL_DestroySurface(s);
        }
    }
}

static b32 writeicon(Os *os, App *app, char *path)
{
    static i32 const sizes[] = {16, 24, 32, 48, 64, 256};
    Arena t = app->scratch;
    u8   *imgs[countof(sizes)];
    for (i32 i = 0; i < countof(sizes); i++) {
        imgs[i] = rendericon(os, app, sizes[i], 0, &t);
        if (!imgs[i]) {
            return 0;
        }
    }
    Str ico = makeico(&t, sizes, imgs, countof(sizes));
    return SDL_SaveFile(path, ico.data, (uz)ico.len);
}

// Write a PNG icon for the web; a trailing "m" on the size asks for a
// maskable one.
static b32 writepng(Os *os, App *app, char *size, char *path)
{
    #ifdef __EMSCRIPTEN__
    return 0;
    #else
    Str s  = cstrarg(size);
    b32 mk = s.len && s.data[s.len-1]=='m';
    Int n  = parseint(mk ? Str{s.data, s.len-1} : s);
    if (!n.ok || n.val<16 || n.val>1024) {
        return 0;
    }
    Arena t  = app->scratch;
    u8   *px = rendericon(os, app, (i32)n.val, mk, &t);
    if (!px) {
        return 0;
    }
    i32 sz  = (i32)n.val;
    int len = 0;
    u8 *png = stbi_write_png_to_mem(px, sz*4, sz, sz, 4, &len);
    b32 ok  = png && SDL_SaveFile(path, png, (uz)len);
    SDL_free(png);
    return ok;
    #endif
}

static void sendgeometry(Os *);

SDL_AppResult SDL_AppInit(void **appstate, int argc, char **argv)
{
    SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");
    SDL_SetAppMetadata("Euchre", APP_VERSION, "com.nullprogram.euchre");

    Os *os = (Os *)SDL_calloc(1, sizeof(Os));
    *appstate = os;

    Options opt  = {};
    Shot    shotat = {};
    char   *iconpath = 0;
    char   *pngsize  = 0;
    #ifdef __EMSCRIPTEN__
    opt.noquit = 1;
    #endif
    for (i32 i = 1; i < argc; i++) {
        Str a = cstrarg(argv[i]);
        b32 more = i+1 < argc;
        if (a=="--save" && more) {
            os->savepath = argv[++i];
        } else if (a=="--seed" && more) {
            Int n = parseint(cstrarg(argv[++i]));
            opt.seed    = (u64)n.val;
            opt.seeded  = n.ok;
        } else if (a == "--autoplay") {
            opt.autoplay = 1;
        } else if (a=="--shot" && i+2<argc) {
            shotat = parseshot(argv[i+1], argv[i+2]);
            i += 2;
        } else if (a=="--until" && more) {
            opt.until = parsephase(cstrarg(argv[++i]));
        } else if (a=="--open" && more) {
            opt.open = cstrarg(argv[++i]);
        } else if (a=="--icon" && more) {
            iconpath = argv[++i];
        } else if (a=="--png" && i+2<argc) {
            pngsize = argv[i+1];
            iconpath = argv[i+2];
            i += 2;
        } else {
            SDL_Log("euchre: unknown option: %s", argv[i]);
            return SDL_APP_FAILURE;
        }
    }
    opt.headless = shotat.path || iconpath;
    #ifdef __EMSCRIPTEN__
    b32 web = 1;
    #else
    b32 web = 0;
    #endif
    if (!os->savepath && !opt.headless && !web) {
        char *base = SDL_GetPrefPath("nullprogram", "Euchre");
        if (base) {
            SDL_asprintf(&os->savepath, "%ssave.txt", base);
            SDL_free(base);
        }
    }
    if (!os->savepath) {
        os->savepath = SDL_strdup("");  // nothing loads, saves fail quietly
    }

    if (!SDL_Init(opt.headless ? 0 : SDL_INIT_VIDEO|SDL_INIT_AUDIO)) {
        SDL_Log("euchre: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    byte *mem = (byte *)SDL_malloc(MEMSIZE);
    if (!mem) {
        os_oom();
    }
    if (!opt.seeded) {
        opt.seed = SDL_GetPerformanceCounter() ^ (u64)(uz)mem;
        opt.seed = hashmix(opt.seed, (u64)SDL_GetTicksNS());
    }
    App *app = appnew(os, {mem, mem+MEMSIZE}, opt);
    os->app   = app;
    os->clips = app->clips;

    if (pngsize) {
        return writepng(os, app, pngsize, iconpath) ? SDL_APP_SUCCESS : SDL_APP_FAILURE;
    }
    if (iconpath) {
        return writeicon(os, app, iconpath) ? SDL_APP_SUCCESS : SDL_APP_FAILURE;
    }
    if (shotat.path) {
        if (!shot(os, app, shotat)) {
            SDL_Log("euchre: shot failed: %s", SDL_GetError());
            return SDL_APP_FAILURE;
        }
        return SDL_APP_SUCCESS;
    }

    SDL_AudioSpec spec = {SDL_AUDIO_F32, 2, SAMPLE_RATE};
    os->stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, feed, os);
    if (os->stream) {
        float primer[32] = {};
        SDL_PutAudioStreamData(os->stream, primer, sizeof(primer));
        SDL_ResumeAudioStreamDevice(os->stream);
    }

    WinGeom g = app->win;
    i32 w = g.w>=MINWIN_W ? g.w : 1280;
    i32 h = g.h>=MINWIN_H ? g.h : 800;
    SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY |
                            SDL_WINDOW_HIDDEN;
    if (g.max) {
        flags |= SDL_WINDOW_MAXIMIZED;
    }
    if (web) {
        // The page is the window, sized by the browser
        flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY |
                SDL_WINDOW_FILL_DOCUMENT;
    }
    if (!SDL_CreateWindowAndRenderer("Euchre", w, h, flags, &os->window, &os->renderer)) {
        SDL_Log("euchre: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    if (!web) {
        SDL_SetWindowMinimumSize(os->window, MINWIN_W, MINWIN_H);
    }
    if (g.w>=MINWIN_W && !web) {
        // Only where some display still shows the window's top
        SDL_Rect want = {g.x, g.y, w, 40};
        i32      n    = 0;
        SDL_DisplayID *ds = SDL_GetDisplays(&n);
        for (i32 i = 0; ds && i < n; i++) {
            SDL_Rect b, x;
            if (SDL_GetDisplayUsableBounds(ds[i], &b) && SDL_GetRectIntersection(&want, &b, &x) &&
                x.w>=100 && x.h>=20) {
                SDL_SetWindowPosition(os->window, g.x, g.y);
                break;
            }
        }
        SDL_free(ds);
    }
    SDL_SetRenderVSync(os->renderer, 1);
    if (!web) {
        // The icon is drawn by the core with its own renderer
        SDL_Renderer *r = os->renderer;
        os->renderer = 0;
        seticon(os, app);
        os->renderer = r;
    }
    SDL_ShowWindow(os->window);
    sendgeometry(os);
    os->arrow = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_DEFAULT);
    os->hand  = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_POINTER);
    return SDL_APP_CONTINUE;
}

static void sendgeometry(Os *os)
{
    SDL_WindowFlags f = SDL_GetWindowFlags(os->window);
    Input in = {};
    in.kind = IN_WINDOW;
    in.win.max = (f & SDL_WINDOW_MAXIMIZED) != 0;
    if (f & (SDL_WINDOW_MINIMIZED|SDL_WINDOW_FULLSCREEN)) {
        return;
    }
    if (!in.win.max) {
        SDL_GetWindowPosition(os->window, &in.win.x, &in.win.y);
        SDL_GetWindowSize(os->window, &in.win.w, &in.win.h);
    } else {
        in.win.x = os->app->win.x;
        in.win.y = os->app->win.y;
        in.win.w = os->app->win.w;
        in.win.h = os->app->win.h;
    }
    appevent(os->app, in);
}

static i32 keycode(SDL_Keycode k)
{
    switch (k) {
    case SDLK_ESCAPE:    return KEY_ESC;
    case SDLK_RETURN:    return KEY_ENTER;
    case SDLK_KP_ENTER:  return KEY_ENTER;
    case SDLK_SPACE:     return ' ';
    case SDLK_LEFT:      return KEY_LEFT;
    case SDLK_RIGHT:     return KEY_RIGHT;
    case SDLK_SLASH:     return '?';
    }
    if (k>='a' && k<='z') return (i32)k;
    if (k>='0' && k<='9') return (i32)k;
    return 0;
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *e)
{
    Os  *os  = (Os *)appstate;
    App *app = os->app;
    if (!app || !os->window) {
        return SDL_APP_CONTINUE;
    }
    Input in = {};
    switch (e->type) {
    case SDL_EVENT_QUIT:
        return SDL_APP_SUCCESS;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (e->button.button != SDL_BUTTON_LEFT) {
            break;
        }
        SDL_ConvertEventToRenderCoordinates(os->renderer, e);
        in.kind = e->type==SDL_EVENT_MOUSE_BUTTON_DOWN ? IN_DOWN : IN_UP;
        in.x    = e->button.x;
        in.y    = e->button.y;
        in.touch = e->button.which == SDL_TOUCH_MOUSEID;
        appevent(app, in);
        break;
    case SDL_EVENT_MOUSE_MOTION:
        SDL_ConvertEventToRenderCoordinates(os->renderer, e);
        in.kind  = IN_MOVE;
        in.x     = e->motion.x;
        in.y     = e->motion.y;
        in.touch = e->motion.which == SDL_TOUCH_MOUSEID;
        appevent(app, in);
        break;
    case SDL_EVENT_WINDOW_MOUSE_LEAVE:
        in.kind = IN_LEAVE;
        appevent(app, in);
        break;
    case SDL_EVENT_KEY_DOWN:
        in.kind = IN_KEY;
        in.key  = keycode(e->key.key);
        if (in.key) {
            appevent(app, in);
        }
        break;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        in.kind = IN_FOCUS;
        appevent(app, in);
        break;
    case SDL_EVENT_WINDOW_MOVED:
    case SDL_EVENT_WINDOW_RESIZED:
    case SDL_EVENT_WINDOW_MAXIMIZED:
    case SDL_EVENT_WINDOW_RESTORED:
        sendgeometry(os);
        app->redraw = 1;
        break;
    case SDL_EVENT_WINDOW_EXPOSED:
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
    case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
    case SDL_EVENT_RENDER_TARGETS_RESET:
    case SDL_EVENT_RENDER_DEVICE_RESET:
        app->redraw = 1;
        if (e->type == SDL_EVENT_RENDER_DEVICE_RESET) {
            os->atlas = 0;  // destroyed with the device
        }
        break;
    }
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void *appstate)
{
    Os  *os  = (Os *)appstate;
    App *app = os->app;
    if (!app || !os->window) {
        return SDL_APP_SUCCESS;
    }
    i64 now  = (i64)SDL_GetTicks();
    i32 wait = appupdate(app, now);
    if (app->quit) {
        return SDL_APP_SUCCESS;
    }
    if (app->redraw || wait==0) {
        renderframe(os, app, windowview(os), now);
        SDL_RenderPresent(os->renderer);
        wait = appwait(app, wait);  // the frame may ask for another
    }
    if (app->cursor != os->cursor) {
        os->cursor = app->cursor;
        SDL_SetCursor(app->cursor ? os->hand : os->arrow);
    }

    if (os->timer) {
        SDL_RemoveTimer(os->timer);
        os->timer = 0;
    }
    b32 block = wait != 0;
    if (block != os->blocking) {
        os->blocking = block;
        SDL_SetHint(SDL_HINT_MAIN_CALLBACK_RATE, block ? "waitevent" : "0");
    }
    if (wait > 0) {
        os->timer = SDL_AddTimer((Uint32)wait, wake, 0);
    }
    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *appstate, SDL_AppResult)
{
    Os *os = (Os *)appstate;
    if (!os) {
        return;
    }
    if (os->app && os->window) {
        sendgeometry(os);
        appquit(os->app);
    }
    if (os->stream) {
        SDL_DestroyAudioStream(os->stream);
    }
    if (os->timer) {
        SDL_RemoveTimer(os->timer);
    }
}
