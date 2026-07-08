#include "bitboard.h"
#include <sstream>

Bitboard SquareBB[SQ_NB];
Bitboard StepAttacks[PIECE_NB][SQ_NB];
Bitboard FileBB[FILE_NB];
Bitboard RankBB[RANK_NB];
Bitboard PromotionZoneBB[COLOR_NB];
// Full ray from each square in each of 8 directions (empty-board attacks).
Bitboard RayBB[SQ_NB][8];

namespace {

struct Off { int df, dr; };

// Ray walk from `s` in direction (df,dr), stopping at (and including) first blocker.
Bitboard walk(Square s, int df, int dr, const Bitboard& occ) {
    Bitboard b;
    int f = int(file_of(s)), r = int(rank_of(s));
    for (;;) {
        f += df; r += dr;
        if (f < 0 || f > 8 || r < 0 || r > 8) break;
        Square t = make_square(File(f), Rank(r));
        b.set(t);
        if (occ.test(t)) break;
    }
    return b;
}

// Step targets (single step, bounds-checked).
Bitboard steps(Square s, const Off* offs, int n) {
    Bitboard b;
    int f0 = int(file_of(s)), r0 = int(rank_of(s));
    for (int i = 0; i < n; ++i) {
        int f = f0 + offs[i].df, r = r0 + offs[i].dr;
        if (f < 0 || f > 8 || r < 0 || r > 8) continue;
        b.set(make_square(File(f), Rank(r)));
    }
    return b;
}

// Ray directions, indexed 0..7. Positivity = whether the direction increases the
// square index (index = file*9 + rank), which selects lsb vs msb blocker.
//        idx:  0(E)   1(W)   2(S)   3(N)   4(SE)  5(NW)  6(NE)  7(SW)
const int  DirDF[8]  = {  1,    -1,     0,     0,     1,    -1,     1,    -1 };
const int  DirDR[8]  = {  0,     0,     1,    -1,     1,    -1,    -1,     1 };
const bool DirPos[8] = { true, false, true, false, true, false, true, false };

// BLACK-perspective step offsets (forward = rank decreasing, dr negative).
const Off PawnOff[]   = { {0,-1} };
const Off KnightOff[] = { {-1,-2}, {1,-2} };
const Off SilverOff[] = { {0,-1}, {-1,-1}, {1,-1}, {-1,1}, {1,1} };
const Off GoldOff[]   = { {0,-1}, {-1,-1}, {1,-1}, {-1,0}, {1,0}, {0,1} };
const Off KingOff[]   = { {-1,-1}, {0,-1}, {1,-1}, {-1,0}, {1,0}, {-1,1}, {0,1}, {1,1} };
const Off HorseStep[] = { {0,-1}, {0,1}, {-1,0}, {1,0} };  // king-orthogonal
const Off DragStep[]  = { {-1,-1}, {1,-1}, {-1,1}, {1,1} }; // king-diagonal

// Return a color-flipped copy of an offset list (flip dr) into `out`.
void flip(const Off* in, int n, Off* out) {
    for (int i = 0; i < n; ++i) { out[i].df = in[i].df; out[i].dr = -in[i].dr; }
}

} // namespace

// Sliding attack along one direction using the precomputed ray + first blocker.
static inline Bitboard ray_attack(Square s, int d, const Bitboard& occ) {
    Bitboard ray = RayBB[s][d];
    Bitboard blockers = ray & occ;
    if (blockers.any()) {
        Square first = DirPos[d] ? blockers.lsb() : blockers.msb();
        ray = ray ^ RayBB[first][d]; // trim everything beyond the blocker
    }
    return ray;
}

Bitboard lance_attacks(Color c, Square s, const Bitboard& occ) {
    return ray_attack(s, c == BLACK ? 3 : 2, occ); // N for black, S for white
}
Bitboard bishop_attacks(Square s, const Bitboard& occ) {
    return ray_attack(s, 4, occ) | ray_attack(s, 5, occ)
         | ray_attack(s, 6, occ) | ray_attack(s, 7, occ);
}
Bitboard rook_attacks(Square s, const Bitboard& occ) {
    return ray_attack(s, 0, occ) | ray_attack(s, 1, occ)
         | ray_attack(s, 2, occ) | ray_attack(s, 3, occ);
}

Bitboard attacks_bb(Color c, PieceType pt, Square s, const Bitboard& occ) {
    Piece pc = make_piece(c, pt);
    switch (pt) {
        case PAWN: case KNIGHT: case SILVER: case GOLD: case KING:
        case PRO_PAWN: case PRO_LANCE: case PRO_KNIGHT: case PRO_SILVER:
            return StepAttacks[pc][s];
        case LANCE:  return lance_attacks(c, s, occ);
        case BISHOP: return bishop_attacks(s, occ);
        case ROOK:   return rook_attacks(s, occ);
        case HORSE:  return bishop_attacks(s, occ) | StepAttacks[pc][s];
        case DRAGON: return rook_attacks(s, occ) | StepAttacks[pc][s];
        default:     return Bitboard();
    }
}
Bitboard attacks_bb(Piece pc, Square s, const Bitboard& occ) {
    return attacks_bb(color_of(pc), type_of(pc), s, occ);
}

namespace Bitboards {

void init() {
    for (Square s = SQ_ZERO; s < SQ_NB; ++s) {
        SquareBB[s] = Bitboard();
        SquareBB[s].set(s);
        FileBB[file_of(s)].set(s);
        RankBB[rank_of(s)].set(s);
    }
    for (Square s = SQ_ZERO; s < SQ_NB; ++s) {
        if (relative_rank(BLACK, s) <= RANK_C) PromotionZoneBB[BLACK].set(s);
        if (relative_rank(WHITE, s) <= RANK_C) PromotionZoneBB[WHITE].set(s);
    }

    // Full empty-board rays for each square/direction.
    Bitboard empty;
    for (Square s = SQ_ZERO; s < SQ_NB; ++s)
        for (int d = 0; d < 8; ++d)
            RayBB[s][d] = walk(s, DirDF[d], DirDR[d], empty);

    Off buf[8];
    for (Square s = SQ_ZERO; s < SQ_NB; ++s) {
        for (Color c : {BLACK, WHITE}) {
            auto fill = [&](PieceType pt, const Off* base, int n) {
                if (c == BLACK) StepAttacks[make_piece(BLACK, pt)][s] = steps(s, base, n);
                else { flip(base, n, buf); StepAttacks[make_piece(WHITE, pt)][s] = steps(s, buf, n); }
            };
            fill(PAWN,   PawnOff,   1);
            fill(KNIGHT, KnightOff, 2);
            fill(SILVER, SilverOff, 5);
            fill(GOLD,   GoldOff,   6);
            fill(KING,   KingOff,   8);
            fill(PRO_PAWN,   GoldOff, 6);
            fill(PRO_LANCE,  GoldOff, 6);
            fill(PRO_KNIGHT, GoldOff, 6);
            fill(PRO_SILVER, GoldOff, 6);
            // Horse/Dragon step components are color-symmetric.
            fill(HORSE,  HorseStep, 4);
            fill(DRAGON, DragStep,  4);
        }
    }
}

} // namespace Bitboards

std::string pretty(const Bitboard& b) {
    std::ostringstream os;
    for (int r = 0; r < 9; ++r) {
        for (int f = 8; f >= 0; --f)
            os << (b.test(make_square(File(f), Rank(r))) ? " *" : " .");
        os << '\n';
    }
    return os.str();
}
