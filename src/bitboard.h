#ifndef UMATAKE_BITBOARD_H
#define UMATAKE_BITBOARD_H

#include "types.h"

// 81-bit bitboard split into two 64-bit words (Apery-style):
//   p[0] : squares 0..62  (internal files 0..6 = USI files 1..7)
//   p[1] : squares 63..80 (internal files 7..8 = USI files 8..9)
struct Bitboard {
    uint64_t p[2];

    Bitboard() : p{0, 0} {}
    Bitboard(uint64_t p0, uint64_t p1) : p{p0, p1} {}

    static constexpr int  word_of(Square s) { return int(s) >= 63 ? 1 : 0; }
    static constexpr int  bit_of(Square s)  { return int(s) >= 63 ? int(s) - 63 : int(s); }
    static uint64_t maskbit(Square s) { return 1ULL << bit_of(s); }

    bool test(Square s) const { return p[word_of(s)] & maskbit(s); }
    void set(Square s)   { p[word_of(s)] |= maskbit(s); }
    void reset(Square s)  { p[word_of(s)] &= ~maskbit(s); }
    void xor_bit(Square s) { p[word_of(s)] ^= maskbit(s); }

    bool none() const { return (p[0] | p[1]) == 0; }
    bool any()  const { return (p[0] | p[1]) != 0; }
    explicit operator bool() const { return any(); }

    int count() const { return __builtin_popcountll(p[0]) + __builtin_popcountll(p[1]); }

    Bitboard operator~() const {
        // valid bits: word0 -> 63 bits, word1 -> 18 bits
        return Bitboard(~p[0] & ((1ULL << 63) - 1), ~p[1] & ((1ULL << 18) - 1));
    }
    Bitboard operator&(const Bitboard& b) const { return Bitboard(p[0] & b.p[0], p[1] & b.p[1]); }
    Bitboard operator|(const Bitboard& b) const { return Bitboard(p[0] | b.p[0], p[1] | b.p[1]); }
    Bitboard operator^(const Bitboard& b) const { return Bitboard(p[0] ^ b.p[0], p[1] ^ b.p[1]); }
    Bitboard& operator&=(const Bitboard& b) { p[0] &= b.p[0]; p[1] &= b.p[1]; return *this; }
    Bitboard& operator|=(const Bitboard& b) { p[0] |= b.p[0]; p[1] |= b.p[1]; return *this; }
    Bitboard& operator^=(const Bitboard& b) { p[0] ^= b.p[0]; p[1] ^= b.p[1]; return *this; }
    bool operator==(const Bitboard& b) const { return p[0] == b.p[0] && p[1] == b.p[1]; }
    bool operator!=(const Bitboard& b) const { return !(*this == b); }

    Bitboard& operator|=(Square s) { p[word_of(s)] |= maskbit(s); return *this; }

    // Pop least-significant set bit, returning its square.
    Square pop_lsb() {
        if (p[0]) {
            int b = __builtin_ctzll(p[0]);
            p[0] &= p[0] - 1;
            return Square(b);
        }
        int b = __builtin_ctzll(p[1]);
        p[1] &= p[1] - 1;
        return Square(b + 63);
    }
    Square lsb() const {
        if (p[0]) return Square(__builtin_ctzll(p[0]));
        return Square(__builtin_ctzll(p[1]) + 63);
    }
    // Highest set bit's square (caller must ensure any()).
    Square msb() const {
        if (p[1]) return Square(63 + 63 - __builtin_clzll(p[1]));
        return Square(63 - __builtin_clzll(p[0]));
    }
};

namespace Bitboards {
void init();
}

// Global attack tables (initialised by Bitboards::init()).
extern Bitboard SquareBB[SQ_NB];
extern Bitboard StepAttacks[PIECE_NB][SQ_NB]; // for non-sliding pieces (indexed by Piece)
extern Bitboard FileBB[FILE_NB];
extern Bitboard RankBB[RANK_NB];
extern Bitboard PromotionZoneBB[COLOR_NB]; // squares in each color's promotion zone

inline Bitboard square_bb(Square s) { return SquareBB[s]; }

// Sliding attacks computed against an occupancy bitboard.
Bitboard lance_attacks(Color c, Square s, const Bitboard& occ);
Bitboard bishop_attacks(Square s, const Bitboard& occ);
Bitboard rook_attacks(Square s, const Bitboard& occ);

// Full attack set of a piece standing on `s` with board occupancy `occ`.
Bitboard attacks_bb(Piece pc, Square s, const Bitboard& occ);
// Attack set of piece type `pt` of color `c`.
Bitboard attacks_bb(Color c, PieceType pt, Square s, const Bitboard& occ);

// Debug helper.
#include <string>
std::string pretty(const Bitboard& b);

// Iterate set squares: `for (Square s : bb_range(b))` style helper.
#define FOREACH_SQ(bb, sq) for (Bitboard _t = (bb); _t.any() && ((sq) = _t.pop_lsb(), true); )

#endif // UMATAKE_BITBOARD_H
