# Euchre as this program plays it

## The game

Four players in two partnerships: you (South) and your partner (North)
against West and East. Play goes clockwise: South, West, North, East.
The pack is 24 cards, the 9, 10, jack, queen, king and ace of each
suit. The first team to the target (10 by default) wins.

## The deal

The dealer gives each player five cards in packets of three and two, and
turns the next card face up on the four left over (the kitty). The first
dealer is chosen at random; the deal passes left after every hand.

## Trump and the bowers

One suit is trump each hand. Its jack, the right bower, is the highest
card; the other jack of the same color, the left bower, is second and
belongs to the trump suit for every purpose, following suit included.
Then come the ace, king, queen, 10 and 9 of trump. The other suits rank
ace, king, queen, jack, 10, 9.

## Making trump

Round one, from the dealer's left: each player passes or orders up the
turned card (the dealer picks it up). Once someone does, its suit is
trump, and the dealer takes the card and discards one face down.

If all four pass, the card is turned down, and in round two each may
name any other suit, or pass. With **stick the dealer** (on by default)
the dealer may not pass in round two and must name a suit; with it off,
four more passes throw the hand in and the deal passes.

## Going alone

Whoever makes trump may go alone. Their partner sits the hand out. When
the dealer's partner orders up alone, the dealer sits out, and the
turned card stays in the kitty.

## The play

The dealer's left leads the first trick, or the next player if that seat
sits out. Everyone must follow the suit led if able; otherwise any card
may be played. The highest trump takes the trick, or the highest card of
the suit led if no trump was played. The winner leads the next trick.

## Scoring

| Makers take          | Score                 |
|----------------------|-----------------------|
| 3 or 4 tricks        | 1 to the makers       |
| all 5 (a march)      | 2 to the makers       |
| all 5, alone         | 4 to the maker's team |
| fewer than 3         | 2 to the defenders (euchred) |

## Options

- **Stick the dealer**: on or off, as above.
- **Game to**: 5, 7 or 10 points.

Rule and seating changes take effect with the next game.

## The computer players

Each seat is a character: a level and a style.

- **Beginner** (Gus, Dot): counts trump to bid; plays its highest card
  when that wins the trick, else its lowest. Slips now and then.
- **Intermediate** (Bev, Earl, Marge): weighs the turned card by where
  it will go, favors "next" in round two, pulls trump as a maker, cashes
  aces, lets a partner's sure winner stand, and tracks the master card of
  each suit.
- **Expert** (Ruth, Hank, Walt): deals out the cards it can't see dozens
  of ways, consistent with everything its seat knows (cards played, who
  has shown out of a suit, where the turned card went, and what each bid
  suggests), solves each deal exactly, and picks the move with the best
  average score. Bids, loners and the dealer's discard are weighed the
  same way. The Hint button asks this player about your hand.

In duplicate matches (`euchre_test --duel`), an expert pair gains about
0.15 points per hand over an intermediate pair, and an intermediate pair
about 0.28 over a beginner pair.
