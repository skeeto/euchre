// Sound: effects synthesized at startup, mono at SAMPLE_RATE
//
// The platform mixes them; the core only says which to start.

enum : i32 {
    SFX_SHUFFLE,  // a riffle, at each deal
    SFX_DEAL,     // one card off the deck
    SFX_PLAY,     // a card snapped onto the table
    SFX_FLIP,     // the turned card turned down
    SFX_KNOCK,    // a pass, a knuckle on the table
    SFX_CALL,     // trump is made
    SFX_SWEEP,    // a trick gathered
    SFX_POINT,    // your side scores
    SFX_EUCHRE,   // your side is euchred
    SFX_WIN,      // game won
    SFX_LOSE,     // game lost
    SFX_CLICK,    // a button
    SFX_COUNT,
};

enum : i32 {
    SAMPLE_RATE = 44100,
};

struct Clip {
    f32 *samples;
    iz   len;
};

static Clip newclip(Arena *a, f32 seconds)
{
    Clip c = {};
    c.len     = (iz)(seconds * SAMPLE_RATE);
    c.samples = alloc<f32>(a, c.len);
    return c;
}

static f32 noise(u64 *s)
{
    return randf(s)*2 - 1;
}

// A burst of filtered noise: the sound of cards.
static void rustle(Clip *c, f32 at, f32 dur, f32 amp, f32 decay, f32 bright, u64 *rng)
{
    iz  start = (iz)(at * SAMPLE_RATE);
    iz  n     = (iz)(dur * SAMPLE_RATE);
    f32 lp    = 0;
    f32 hp    = 0;
    for (iz i = 0; i<n && start+i<c->len; i++) {
        f32 t   = (f32)i / SAMPLE_RATE;
        f32 env = expf_(-t*decay) * minf(1, t*2000);
        f32 x   = noise(rng);
        lp += bright*(x - lp);
        hp  = lp - hp*0.2f;
        c->samples[start+i] += hp*env*amp;
    }
}

// A tone with a few harmonics under a bell-like decay.
static void tone(Clip *c, f32 at, f32 freq, f32 dur, f32 amp, f32 decay)
{
    iz start = (iz)(at * SAMPLE_RATE);
    iz n     = (iz)(dur * SAMPLE_RATE);
    for (iz i = 0; i<n && start+i<c->len; i++) {
        f32 t   = (f32)i / SAMPLE_RATE;
        f32 env = expf_(-t*decay) * minf(1, t*400);
        f32 rem = dur - t;
        if (rem < 0.02f) env *= rem/0.02f;
        f32 ph  = 2*PI*freq*t;
        f32 s   = sinf_(ph) + 0.4f*sinf_(2*ph) + 0.15f*sinf_(3*ph);
        c->samples[start+i] += s*env*amp;
    }
}

// A low thump: a sine sweeping down.
static void thump(Clip *c, f32 at, f32 f0, f32 f1, f32 dur, f32 amp)
{
    iz  start = (iz)(at * SAMPLE_RATE);
    iz  n     = (iz)(dur * SAMPLE_RATE);
    f32 ph    = 0;
    for (iz i = 0; i<n && start+i<c->len; i++) {
        f32 t = (f32)i / SAMPLE_RATE;
        f32 f = f0 + (f1 - f0)*(t/dur);
        ph += 2*PI*f/SAMPLE_RATE;
        f32 env = expf_(-t*(5/dur)) * minf(1, t*3000);
        c->samples[start+i] += sinf_(ph)*env*amp;
    }
}

static Clip *synthclips(Arena *a)
{
    Clip *cs  = alloc<Clip>(a, SFX_COUNT);
    u64   rng = 0x5eed;

    cs[SFX_SHUFFLE] = newclip(a, 0.75f);
    for (i32 k = 0; k < 18; k++) {
        f32 at = 0.02f + 0.036f*(f32)k + 0.006f*randf(&rng);
        rustle(cs+SFX_SHUFFLE, at, 0.03f, 0.35f*(1 - 0.4f*(f32)k/18), 140, 0.5f, &rng);
    }

    cs[SFX_DEAL] = newclip(a, 0.08f);
    rustle(cs+SFX_DEAL, 0, 0.08f, 0.30f, 60, 0.35f, &rng);

    cs[SFX_PLAY] = newclip(a, 0.12f);
    rustle(cs+SFX_PLAY, 0, 0.05f, 0.45f, 110, 0.7f, &rng);
    thump(cs+SFX_PLAY, 0, 220, 120, 0.1f, 0.25f);

    cs[SFX_FLIP] = newclip(a, 0.14f);
    rustle(cs+SFX_FLIP, 0, 0.14f, 0.30f, 30, 0.25f, &rng);

    cs[SFX_KNOCK] = newclip(a, 0.30f);
    thump(cs+SFX_KNOCK, 0.00f, 170, 90, 0.09f, 0.55f);
    rustle(cs+SFX_KNOCK, 0.00f, 0.02f, 0.10f, 200, 0.9f, &rng);
    thump(cs+SFX_KNOCK, 0.13f, 170, 90, 0.09f, 0.45f);
    rustle(cs+SFX_KNOCK, 0.13f, 0.02f, 0.08f, 200, 0.9f, &rng);

    cs[SFX_CALL] = newclip(a, 0.7f);
    tone(cs+SFX_CALL, 0.00f, 659.25f, 0.6f, 0.18f, 5);
    tone(cs+SFX_CALL, 0.08f, 987.77f, 0.6f, 0.15f, 5);

    cs[SFX_SWEEP] = newclip(a, 0.3f);
    for (i32 k = 0; k < 4; k++) {
        rustle(cs+SFX_SWEEP, 0.03f*(f32)k, 0.12f, 0.18f, 40, 0.3f, &rng);
    }

    cs[SFX_POINT] = newclip(a, 0.6f);
    tone(cs+SFX_POINT, 0.00f, 783.99f, 0.25f, 0.16f, 9);
    tone(cs+SFX_POINT, 0.10f, 1046.5f, 0.45f, 0.16f, 6);

    cs[SFX_EUCHRE] = newclip(a, 0.9f);
    tone(cs+SFX_EUCHRE, 0.00f, 392.00f, 0.3f, 0.18f, 6);
    tone(cs+SFX_EUCHRE, 0.18f, 311.13f, 0.7f, 0.18f, 4);

    cs[SFX_WIN] = newclip(a, 2.0f);
    static f32 const arp[] = {523.25f, 659.25f, 783.99f};
    for (i32 k = 0; k < 3; k++) {
        tone(cs+SFX_WIN, 0.12f*(f32)k, arp[k], 0.18f, 0.20f, 9);
    }
    static f32 const chord[] = {523.25f, 659.25f, 783.99f, 1046.5f};
    for (i32 k = 0; k < 4; k++) {
        tone(cs+SFX_WIN, 0.36f, chord[k], 1.6f, 0.10f, 1.9f);
    }

    cs[SFX_LOSE] = newclip(a, 1.4f);
    static f32 const fall[] = {493.88f, 440.00f, 392.00f, 329.63f};
    for (i32 k = 0; k < 4; k++) {
        tone(cs+SFX_LOSE, 0.16f*(f32)k, fall[k], k==3 ? 0.9f : 0.2f, 0.16f, k==3 ? 3.0f : 8.0f);
    }

    cs[SFX_CLICK] = newclip(a, 0.05f);
    tone(cs+SFX_CLICK, 0, 1200, 0.04f, 0.10f, 90);
    return cs;
}
