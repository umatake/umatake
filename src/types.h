#ifndef UMATAKE_TYPES_H
#define UMATAKE_TYPES_H

#include <cstdint>
#include <cstddef>
#include <cassert>

// ===== Umatake USI Shogi Engine — core types =====
//
// Board: 81 squares. Internal square index sq = file*9 + rank.
//   file index f: 0..8  -> USI file '1'..'9'
//   rank index r: 0..8  -> USI rank 'a'..'i'  (r=0 top, r=8 bottom)
// Black (先手/sente, UPPERCASE in SFEN) sits at the bottom and moves toward
// rank 'a' (decreasing r). White (後手/gote) moves toward rank 'i'.

// ---- Color ----
enum Color : int { BLACK = 0, WHITE = 1, COLOR_NB = 2 };
constexpr Color operator~(Color c) { return Color(c ^ 1); }

// ---- File / Rank / Square ----
enum File : int { FILE_1, FILE_2, FILE_3, FILE_4, FILE_5, FILE_6, FILE_7, FILE_8, FILE_9, FILE_NB };
enum Rank : int { RANK_A, RANK_B, RANK_C, RANK_D, RANK_E, RANK_F, RANK_G, RANK_H, RANK_I, RANK_NB };

enum Square : int { SQ_NONE = -1, SQ_ZERO = 0, SQ_NB = 81 };

constexpr Square make_square(File f, Rank r) { return Square(int(f) * 9 + int(r)); }
constexpr File file_of(Square s) { return File(int(s) / 9); }
constexpr Rank rank_of(Square s) { return Rank(int(s) % 9); }
constexpr bool is_ok(Square s) { return s >= SQ_ZERO && s < SQ_NB; }

// Relative rank from a color's viewpoint (0 = own back rank side ... higher = deeper into enemy camp).
// For BLACK, promotion zone is r in {0,1,2}; for WHITE, r in {6,7,8}.
constexpr Rank relative_rank(Color c, Square s) {
    return c == BLACK ? rank_of(s) : Rank(8 - int(rank_of(s)));
}

// ---- Piece types ----
enum PieceType : int {
    NO_PIECE_TYPE = 0,
    PAWN   = 1,  // 歩
    LANCE  = 2,  // 香
    KNIGHT = 3,  // 桂
    SILVER = 4,  // 銀
    BISHOP = 5,  // 角
    ROOK   = 6,  // 飛
    GOLD   = 7,  // 金
    KING   = 8,  // 玉
    PRO_PAWN   = 9,   // と
    PRO_LANCE  = 10,  // 成香
    PRO_KNIGHT = 11,  // 成桂
    PRO_SILVER = 12,  // 成銀
    HORSE      = 13,  // 馬 (promoted bishop)
    DRAGON     = 14,  // 龍 (promoted rook)
    PIECE_TYPE_NB = 15,
    PROMOTE_BIT = 8
};

// Piece types that can be held in hand (unpromoted, non-king).
constexpr PieceType HAND_TYPES[7] = { PAWN, LANCE, KNIGHT, SILVER, GOLD, BISHOP, ROOK };

constexpr bool is_promoted(PieceType pt) { return pt >= PRO_PAWN; }
constexpr bool can_promote_type(PieceType pt) { return pt >= PAWN && pt <= ROOK; } // GOLD/KING cannot
constexpr PieceType promote_type(PieceType pt) { return PieceType(pt + PROMOTE_BIT); }
// Base (unpromoted) type. Only meaningful for pieces that came from hand / captures.
constexpr PieceType raw_type(PieceType pt) {
    return is_promoted(pt) ? PieceType(pt - PROMOTE_BIT) : pt;
}

// ---- Piece (color-tagged) ----
// piece = (color << 4) | pieceType ; NO_PIECE = 0.
enum Piece : int {
    NO_PIECE = 0,
    B_PAWN = PAWN, B_LANCE, B_KNIGHT, B_SILVER, B_BISHOP, B_ROOK, B_GOLD, B_KING,
    B_PRO_PAWN, B_PRO_LANCE, B_PRO_KNIGHT, B_PRO_SILVER, B_HORSE, B_DRAGON,
    W_PAWN = PAWN + 16, W_LANCE, W_KNIGHT, W_SILVER, W_BISHOP, W_ROOK, W_GOLD, W_KING,
    W_PRO_PAWN, W_PRO_LANCE, W_PRO_KNIGHT, W_PRO_SILVER, W_HORSE, W_DRAGON,
    PIECE_NB = 32
};

constexpr Piece make_piece(Color c, PieceType pt) { return Piece((int(c) << 4) | int(pt)); }
constexpr PieceType type_of(Piece pc) { return PieceType(int(pc) & 15); }
constexpr Color color_of(Piece pc) { return Color(int(pc) >> 4); }
constexpr Piece promote_piece(Piece pc) { return Piece(int(pc) + PROMOTE_BIT); }

// ---- Value ----
using Value = int;
enum : Value {
    VALUE_ZERO = 0,
    VALUE_DRAW = 0,
    VALUE_MATE = 32000,
    VALUE_INFINITE = 32001,
    VALUE_NONE = 32002,
    VALUE_MATE_IN_MAX_PLY = VALUE_MATE - 1024,
    VALUE_MATED_IN_MAX_PLY = -VALUE_MATE + 1024,
};

constexpr Value mate_in(int ply) { return Value(VALUE_MATE - ply); }
constexpr Value mated_in(int ply) { return Value(-VALUE_MATE + ply); }

// ---- Move (16-bit) ----
// bits  0..6  : to square (0..80)
// bits  7..13 : from square (0..80)  OR dropped PieceType (1..7) when drop flag set
// bit  14     : promote flag
// bit  15     : drop flag
using Move = uint16_t;
enum : Move { MOVE_NONE = 0, MOVE_NULL = 0xFFFF };

constexpr Move MOVE_PROMOTE_FLAG = 1 << 14;
constexpr Move MOVE_DROP_FLAG    = 1 << 15;

constexpr Move make_move(Square from, Square to, bool promo = false) {
    return Move((int(from) << 7) | int(to) | (promo ? MOVE_PROMOTE_FLAG : 0));
}
constexpr Move make_drop(PieceType pt, Square to) {
    return Move(MOVE_DROP_FLAG | (int(pt) << 7) | int(to));
}
constexpr Square move_to(Move m)   { return Square(m & 0x7f); }
constexpr Square move_from(Move m) { return Square((m >> 7) & 0x7f); }
constexpr bool   is_drop(Move m)    { return m & MOVE_DROP_FLAG; }
constexpr bool   is_promote(Move m) { return m & MOVE_PROMOTE_FLAG; }
constexpr PieceType dropped_type(Move m) { return PieceType((m >> 7) & 0x7f); }

// ---- Hand (pieces in hand): simple count array indexed by PieceType 1..7 ----
struct Hand {
    uint8_t count[8] = {0,0,0,0,0,0,0,0}; // index by HAND_TYPES; only 1..7 used
    int  get(PieceType pt) const { return count[pt]; }
    void add(PieceType pt)  { ++count[pt]; }
    void sub(PieceType pt)  { --count[pt]; }
    bool empty() const {
        for (PieceType pt : HAND_TYPES) if (count[pt]) return false;
        return true;
    }
};

// Increment/decrement operators for enums (loops).
inline Square& operator++(Square& s) { return s = Square(int(s) + 1); }
inline File&   operator++(File& f)   { return f = File(int(f) + 1); }
inline Rank&   operator++(Rank& r)   { return r = Rank(int(r) + 1); }

constexpr int MAX_PLY   = 128;
constexpr int MAX_MOVES = 600; // upper bound on legal moves in any shogi position

#endif // UMATAKE_TYPES_H
