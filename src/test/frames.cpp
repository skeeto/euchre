// Frame cost: an autoplayed game drawn as the platform would at 60 Hz,
// reporting the time to build each frame, its size, and atlas rebakes,
// which a phone pays for on top in a texture upload.

static f64 nowms()
{
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (f64)ts.tv_sec*1e3 + (f64)ts.tv_nsec/1e6;
}

static i32 frames(Arena mem, f32 w, f32 h, f32 scale, i32 seconds)
{
    Os os = {};
    iz cap = (mem.end - mem.beg) / 2;
    os.mem = {mem.beg + cap, mem.end};
    Options opt  = {};
    opt.seed     = 7;
    opt.seeded   = 1;
    opt.autoplay = 1;
    App *app = appnew(&os, {mem.beg, mem.beg + cap}, opt);
    View v   = {w, h, {}, scale};

    i64 now = 1000;
    i32 n = 0, bakes = 0, moving = 0;
    f64 total = 0, worst = 0, bakems = 0, movems = 0;
    iz  verts = 0, mostverts = 0;
    i32 version = app->font->version;
    for (; now < 1000 + (i64)seconds*1000; now += 16) {
        i32 wait = appupdate(app, now);
        if (!app->redraw && wait!=0) {
            continue;
        }
        b32  anim = animating(app);
        f64  t0   = nowms();
        Mesh m    = apprender(app, v, now);
        f64  ms   = nowms() - t0;
        n++;
        total += ms;
        worst  = ms>worst ? ms : worst;
        verts += m.len;
        mostverts = m.len>mostverts ? m.len : mostverts;
        if (anim) {
            moving++;
            movems += ms;
        }
        if (app->font->version != version) {
            version = app->font->version;
            bakes++;
            bakems += ms;
        }
    }
    fprintf(stderr, "%.0fx%.0f@%.2f, %d s: %d frames (%d animating)\n", w, h, scale, seconds, n, moving);
    fprintf(stderr, "  build: %.3f ms mean, %.3f ms animating, %.3f ms worst\n",
            total/n, moving ? movems/moving : 0, worst);
    fprintf(stderr, "  vertices: %lld mean, %lld most\n", (long long)(verts/n), (long long)mostverts);
    fprintf(stderr, "  rebakes: %d, %.2f ms mean frame with one\n", bakes, bakes ? bakems/bakes : 0);
    return 0;
}
