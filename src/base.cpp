// Foundation: types, arenas, strings, buffers, numbers, randomness
// Derived from ssg's base.cpp (skeeto.github.com/_ssg).
// This is free and unencumbered software released into the public domain.

using u8   = unsigned char;
using i8   = signed char;
using b32  = decltype(0);
using i32  = decltype(0);
using u32  = decltype(0u);
using i64  = decltype(0ll);
using u64  = decltype(0ull);
using iz   = decltype((u8 *)0 - (u8 *)0);
using uz   = decltype(sizeof(0));
using byte = decltype('0');
using f32  = decltype(0.f);
using f64  = decltype(0.);

#define assert(c)   while (!(c)) __builtin_unreachable()
#define countof(a)  (iz)(sizeof(a) / sizeof(*(a)))

enum : i64 {
    I64MAX = (i64)((u64)-1 >> 1),
    I64MIN = -I64MAX - 1,
};

template<typename T>
void *operator new(uz, T *p) noexcept { return p; }

struct Arena {
    byte *beg = {};
    byte *end = {};
};

// Provided by the platform layer: report out-of-memory and exit.
[[noreturn]] static void os_oom();

template<typename T>
static T *alloc(Arena *a, iz count = 1)
{
    iz size = sizeof(T);
    iz pad  = -(uz)a->beg & (alignof(T) - 1);
    if (count >= (a->end - a->beg - pad)/size) {
        os_oom();
    }
    T *r = (T *)(a->beg + pad);
    a->beg += pad + count*size;
    for (iz i = 0; i < count; i++) {
        new (r+i) T();
    }
    return r;
}

// Like alloc<u8>, but does not zero the memory.
static u8 *allocbytes(Arena *a, iz count)
{
    if (count > a->end - a->beg) {
        os_oom();
    }
    u8 *r = (u8 *)a->beg;
    a->beg += count;
    return r;
}

// Like alloc, but neither zeroed nor constructed: for plain arrays about
// to be filled in full.
template<typename T>
static T *allocraw(Arena *a, iz count)
{
    iz size = sizeof(T);
    iz pad  = -(uz)a->beg & (alignof(T) - 1);
    if (count >= (a->end - a->beg - pad)/size) {
        os_oom();
    }
    T *r = (T *)(a->beg + pad);
    a->beg += pad + count*size;
    return r;
}

static void copybytes(void *dst, void *src, iz len)
{
    if (len) __builtin_memcpy(dst, src, (uz)len);
}

static void fillbytes(void *dst, u8 v, iz len)
{
    if (len) __builtin_memset(dst, v, (uz)len);
}


// Strings

struct Str {
    union {
        u8         *data = 0;
        char const *cdata;
    };
    iz len = 0;

    Str() = default;

    constexpr Str(u8 *data, iz len) : data{data}, len{len} {}

    template<iz N>
    constexpr Str(char const (&s)[N]) : cdata{s}, len{N-1} {}

    u8 &operator[](iz i)
    {
        assert(i >= 0 && i < len);
        return data[i];
    }

    bool operator==(Str s)
    {
        return len==s.len && (!len || !__builtin_memcmp(data, s.data, (uz)len));
    }
};

static Str span(u8 *beg, u8 *end)
{
    return {beg, end - beg};
}

static Str takehead(Str s, iz n)
{
    assert(n >= 0 && n <= s.len);
    s.len = n;
    return s;
}

static Str cuthead(Str s, iz n)
{
    assert(n >= 0 && n <= s.len);
    s.data += n;
    s.len  -= n;
    return s;
}

static b32 startswith(Str s, Str prefix)
{
    return s.len>=prefix.len && takehead(s, prefix.len)==prefix;
}

struct Cut {
    Str head;
    Str tail;
    b32 ok;
};

static Cut cut(Str s, u8 c)
{
    Cut r = {};
    iz  i = 0;
    for (; i < s.len && s[i] != c; i++) {}
    r.ok   = i < s.len;
    r.head = takehead(s, i);
    r.tail = cuthead(s, i+r.ok);
    return r;
}

static b32 whitespace(u8 c)
{
    return c==' ' || c=='\t' || c=='\n' || c=='\r' || c=='\f' || c=='\v';
}

static b32 digit(u8 c)
{
    return c>='0' && c<='9';
}

static Str trimleft(Str s)
{
    for (; s.len && whitespace(*s.data); s.data++, s.len--) {}
    return s;
}

static Str trimright(Str s)
{
    for (; s.len && whitespace(s.data[s.len-1]); s.len--) {}
    return s;
}

static Str trim(Str s)
{
    return trimleft(trimright(s));
}

// The next space-separated field of *s, consuming it.
static Str field(Str *s)
{
    Str t = trimleft(*s);
    iz  i = 0;
    for (; i < t.len && !whitespace(t[i]); i++) {}
    *s = cuthead(t, i);
    return takehead(t, i);
}

static Str clone(Arena *a, Str s)
{
    Str r = s;
    r.data = allocbytes(a, s.len);
    copybytes(r.data, s.data, s.len);
    return r;
}

// Concatenate, extending head in place when it sits at the arena's end.
static Str concat(Arena *a, Str head, Str tail)
{
    if (!head.data || (byte *)(head.data+head.len) != a->beg) {
        head = clone(a, head);
    }
    head.len += clone(a, tail).len;
    return head;
}

static u64 hash(Str s)
{
    u64 h = 0x100;
    for (iz i = 0; i < s.len; i++) {
        h ^= s[i];
        h *= 1111111111111111111u;
    }
    return h;
}

// Mix one more 64-bit value into a hash.
static u64 hashmix(u64 h, u64 v)
{
    h ^= v;
    h *= 1111111111111111111u;
    return h ^ h>>31;
}


// Output buffers: appending grows in the arena, in place when possible.

struct Buf {
    u8    *data = {};
    iz     len  = {};
    iz     cap  = {};
    Arena *a    = {};

    Buf() = default;
    Buf(Arena *a, iz cap = 1<<12) : data{allocbytes(a, cap)}, cap{cap}, a{a} {}
};

static void reserve(Buf *b, iz need)
{
    if (b->cap - b->len >= need) {
        return;
    }
    Arena *a = b->a;
    iz extend = b->cap > need ? b->cap : need;
    if ((byte *)(b->data+b->cap) == a->beg) {
        allocbytes(a, extend);
    } else {
        u8 *data = allocbytes(a, b->cap+extend);
        copybytes(data, b->data, b->len);
        b->data = data;
    }
    b->cap += extend;
}

static void print(Buf *b, Str s)
{
    reserve(b, s.len);
    copybytes(b->data+b->len, s.data, s.len);
    b->len += s.len;
}

static void putbyte(Buf *b, u8 c)
{
    reserve(b, 1);
    b->data[b->len++] = c;
}

static void print(Buf *b, i64 v)
{
    u8  tmp[32];
    u8 *end = tmp + countof(tmp);
    u8 *beg = end;
    i64 t   = v<0 ? v : -v;
    do {
        *--beg = (u8)('0' - t%10);
    } while (t /= 10);
    if (v < 0) {
        *--beg = '-';
    }
    print(b, span(beg, end));
}

static void printhex(Buf *b, u64 v, i32 digits)
{
    for (i32 i = digits-1; i >= 0; i--) {
        putbyte(b, "0123456789abcdef"[v>>(4*i) & 15]);
    }
}

static Str finish(Buf *b)
{
    return {b->data, b->len};
}


// Numbers

struct Int {
    i64 val;
    b32 ok;
};

// Parse a whole string as a decimal integer with an optional minus sign,
// failing on overflow.
static Int parseint(Str s)
{
    Int r   = {};
    b32 neg = s.len && s[0]=='-';
    iz  i   = neg;
    if (i == s.len) {
        return r;
    }
    i64 v = 0;  // accumulated negatively, which reaches I64MIN
    for (; i < s.len; i++) {
        if (!digit(s[i])) {
            return r;
        }
        i32 d = s[i] - '0';
        if (v < (I64MIN + d)/10) {
            return r;
        }
        v = v*10 - d;
    }
    if (!neg && v==I64MIN) {
        return r;
    }
    r.val = neg ? v : -v;
    r.ok  = 1;
    return r;
}

// Parse a whole string of 1 to 16 lowercase hex digits.
static Int parsehex(Str s)
{
    Int r = {};
    if (s.len<1 || s.len>16) {
        return r;
    }
    u64 v = 0;
    for (iz i = 0; i < s.len; i++) {
        u8 c = s[i];
        i32 d = digit(c) ? c-'0' : c>='a' && c<='f' ? c-'a'+10 : -1;
        if (d < 0) {
            return r;
        }
        v = v<<4 | (u64)d;
    }
    r.val = (i64)v;
    r.ok  = 1;
    return r;
}

static i32 mini(i32 a, i32 b) { return a<b ? a : b; }
static i32 maxi(i32 a, i32 b) { return a>b ? a : b; }
static f32 minf(f32 a, f32 b) { return a<b ? a : b; }
static f32 maxf(f32 a, f32 b) { return a>b ? a : b; }
static f32 clampf(f32 x, f32 lo, f32 hi) { return x<lo ? lo : x>hi ? hi : x; }
static f32 absf(f32 x) { return x<0 ? -x : x; }
static f32 lerpf(f32 a, f32 b, f32 t) { return a + (b - a)*t; }
static f32 sqrtf_(f32 x) { return __builtin_sqrtf(x); }
static f32 sinf_(f32 x) { return __builtin_sinf(x); }
static f32 cosf_(f32 x) { return __builtin_cosf(x); }
static f32 floorf_(f32 x) { return __builtin_floorf(x); }
static f32 expf_(f32 x) { return __builtin_expf(x); }
static f32 roundf_(f32 x) { return floorf_(x + 0.5f); }

static f32 const PI = 3.14159265358979f;

// Smooth 0..1 ease, clamping t.
static f32 ease(f32 t)
{
    t = clampf(t, 0, 1);
    return t*t*(3 - 2*t);
}

// Ease that overshoots slightly, for cards settling into place.
static f32 easeout(f32 t)
{
    t = clampf(t, 0, 1);
    f32 u = 1 - t;
    return 1 - u*u*u;
}


// Randomness: a truncated 64-bit LCG

static u32 rand32(u64 *s)
{
    *s = *s*0x3243f6a8885a308d + 1;
    return (u32)(*s >> 32);
}

// Uniform integer in [0, n), n > 0.
static i32 randint(u64 *s, i32 n)
{
    return (i32)(((u64)rand32(s) * (u32)n) >> 32);
}

// Uniform float in [0, 1).
static f32 randf(u64 *s)
{
    return (f32)(rand32(s) >> 8) / 16777216.0f;
}
