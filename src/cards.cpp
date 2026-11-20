// Cards: a 24-card pack, 9 through ace in four suits
//
// A card is suit*6 + rank. Ranks 0-5 are 9, 10, J, Q, K, A; suits 0-3
// are clubs, spades, diamonds, hearts, so the suit of the same color is
// s^1. A set of cards is a mask, bit c for card c.
//
// Under a trump suit, the jack of trump is the right bower, the highest
// card, and the jack of the same color, the left bower, is the second
// highest and belongs to the trump suit for every purpose: its effective
// suit is trump.

enum : i32 {
    CLUBS,
    SPADES,
    DIAMONDS,
    HEARTS,
    NOSUIT = -1,
};

enum : i32 {
    NINE,
    TEN,
    JACK,
    QUEEN,
    KING,
    ACE,
};

enum : i32 {
    NCARDS = 24,
    NOCARD = -1,
};

enum : u32 {
    ALLCARDS = (1u << NCARDS) - 1,
};

static i32 suitof(i32 c)          { return c / 6; }
static i32 rankof(i32 c)          { return c % 6; }
static i32 mkcard(i32 s, i32 r)   { return s*6 + r; }
static u32 bit(i32 c)             { return 1u << c; }
static i32 popcount(u32 m)        { return __builtin_popcount(m); }
static i32 lowest(u32 m)          { return __builtin_ctz(m); }
static b32 isred(i32 s)           { return s >= DIAMONDS; }
static b32 has(u32 m, i32 c)      { return (m >> c) & 1; }

static b32 isright(i32 c, i32 trump)
{
    return trump!=NOSUIT && c==mkcard(trump, JACK);
}

static b32 isleft(i32 c, i32 trump)
{
    return trump!=NOSUIT && c==mkcard(trump^1, JACK);
}

static i32 effsuit(i32 c, i32 trump)
{
    return isleft(c, trump) ? trump : suitof(c);
}

static b32 istrump(i32 c, i32 trump)
{
    return trump!=NOSUIT && effsuit(c, trump)==trump;
}

// Strength of a card in a trick: trump (9 to 16, the bowers on top)
// beats the suit led (1 to 6), and anything else is worth 0.
static i32 power(i32 c, i32 trump, i32 led)
{
    i32 s = effsuit(c, trump);
    if (s==trump && trump!=NOSUIT) {
        if (rankof(c) == JACK) {
            return suitof(c)==trump ? 16 : 15;
        }
        return 9 + rankof(c);
    }
    return s==led ? 1+rankof(c) : 0;
}

// Cards of effective suit s under trump (NOSUIT: printed suits).
static u32 suitmask(i32 s, i32 trump)
{
    u32 m = 0x3fu << (6*s);
    if (trump != NOSUIT) {
        u32 left = bit(mkcard(trump^1, JACK));
        if (s == trump) {
            m |= left;
        } else if (s == (trump^1)) {
            m &= ~left;
        }
    }
    return m;
}

static u32 trumpmask(i32 trump)
{
    return trump==NOSUIT ? 0 : suitmask(trump, trump);
}

static Str suitname(i32 s)
{
    static Str names[] = {"Clubs", "Spades", "Diamonds", "Hearts"};
    return s>=0 && s<4 ? names[s] : Str("No trump");
}

static Str rankname(i32 r)
{
    static Str names[] = {"9", "10", "J", "Q", "K", "A"};
    return names[r];
}

// Text bytes 1 to 4 are suit symbols (font.cpp draws them as shapes).
static Str suitsym(i32 s)
{
    static Str syms[] = {"\x01", "\x02", "\x03", "\x04"};
    return syms[s];
}

// A card as people read it, "10" and a suit symbol.
static Str cardlabel(Arena *a, i32 c)
{
    return concat(a, rankname(rankof(c)), suitsym(suitof(c)));
}

// Notation for saves and tests: rank then suit, as "JH" or "TD".
static void printcard(Buf *b, i32 c)
{
    putbyte(b, (u8)"9TJQKA"[rankof(c)]);
    putbyte(b, (u8)"CSDH"[suitof(c)]);
}

static i32 parsesuit(u8 c)
{
    switch (c) {
    case 'C': return CLUBS;
    case 'S': return SPADES;
    case 'D': return DIAMONDS;
    case 'H': return HEARTS;
    }
    return NOSUIT;
}

static i32 parsecard(Str s)  // NOCARD if malformed
{
    if (s.len != 2) {
        return NOCARD;
    }
    i32 r = -1;
    switch (s[0]) {
    case '9': r = NINE;  break;
    case 'T': r = TEN;   break;
    case 'J': r = JACK;  break;
    case 'Q': r = QUEEN; break;
    case 'K': r = KING;  break;
    case 'A': r = ACE;   break;
    }
    i32 suit = parsesuit(s[1]);
    return r<0 || suit<0 ? NOCARD : mkcard(suit, r);
}

// Parse a space-separated list of cards into a mask; ok is 0 on any
// malformed or repeated card.
struct Cards {
    u32 mask;
    b32 ok;
};

static Cards parsecards(Str s)
{
    Cards r = {};
    for (Str f = field(&s); f.len; f = field(&s)) {
        i32 c = parsecard(f);
        if (c==NOCARD || has(r.mask, c)) {
            return r;
        }
        r.mask |= bit(c);
    }
    r.ok = 1;
    return r;
}

// Order suits for display after trump, alternating colors: trump, the
// other color, trump's partner suit, then the last.
static void suitorder(i32 trump, i32 *order)
{
    if (trump == NOSUIT) {
        static i32 const plain[] = {SPADES, HEARTS, CLUBS, DIAMONDS};
        for (i32 i = 0; i < 4; i++) order[i] = plain[i];
        return;
    }
    i32 other = isred(trump) ? SPADES : HEARTS;
    order[0] = trump;
    order[1] = other;
    order[2] = trump ^ 1;
    order[3] = other ^ 1;
}

// Sort key for showing a hand: lower sorts first (leftmost).
static i32 sortkey(i32 c, i32 trump)
{
    i32 order[4];
    suitorder(trump, order);
    i32 s   = effsuit(c, trump);
    i32 pos = 0;
    for (; order[pos] != s; pos++) {}
    return pos*32 + (31 - power(c, trump, s));
}

// The cards of a mask in display order; returns the count.
static i32 sortcards(u32 m, i32 trump, i32 *out)
{
    i32 n = 0;
    for (; m; m &= m-1) {
        i32 c = lowest(m);
        i32 k = sortkey(c, trump);
        i32 i = n++;
        for (; i>0 && sortkey(out[i-1], trump)>k; i--) {
            out[i] = out[i-1];
        }
        out[i] = c;
    }
    return n;
}
