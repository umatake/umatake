#include "evaluate.h"
#include "position.h"
#include "bitboard.h"
#include <cstdlib>

namespace Eval {

Value PieceValue[PIECE_TYPE_NB] = {
    0,      // NO_PIECE_TYPE
    90,     // PAWN
    315,    // LANCE
    405,    // KNIGHT
    495,    // SILVER
    855,    // BISHOP
    990,    // ROOK
    540,    // GOLD
    15000,  // KING
    540,    // PRO_PAWN (と)
    540,    // PRO_LANCE
    540,    // PRO_KNIGHT
    540,    // PRO_SILVER
    945,    // HORSE
    1395,   // DRAGON
};

// Value of a piece held in hand (slightly premium — flexible placement).
static Value HandValue[8] = {0, 100, 330, 420, 510, 870, 1010, 550};

// Small bonus for holding the initiative (the side to move).
static const Value Tempo = 20;

// PSQT from BLACK's perspective. White uses square 80-s.
static Value PSQT[PIECE_TYPE_NB][SQ_NB];

// Precomputed per-king-square geometry (depends only on king square, so hoisted
// out of the leaf eval). KingZone = king + 8 neighbours. KingInfluence = the
// Chebyshev-radius-3 dilation of that zone (the region from which any non-slider
// could reach the zone). ShieldZone = the up-to-6 squares in front of / beside
// the king where a castle's pawn-and-general wall lives.
static Bitboard KingZone[SQ_NB];
static Bitboard KingInfluence[SQ_NB];
static Bitboard ShieldZone[COLOR_NB][SQ_NB];

void init() {
    for (int pt = 0; pt < PIECE_TYPE_NB; ++pt)
        for (int s = 0; s < SQ_NB; ++s) PSQT[pt][s] = 0;

    auto dilate = [](Bitboard z) {
        Bitboard r = z, t = z;
        while (t.any()) r |= StepAttacks[B_KING][t.pop_lsb()];
        return r;
    };
    for (Square s = SQ_ZERO; s < SQ_NB; ++s) {
        Bitboard z = StepAttacks[B_KING][s];
        z.set(s);
        KingZone[s] = z;
        KingInfluence[s] = dilate(dilate(z));

        // The shield is the king zone restricted to the king's own rank and the
        // rank in front of it (a castle's pawn wall stands in front of the king,
        // on the flank the enemy advances from). Computed per colour because
        // "in front" flips between BLACK and WHITE.
        for (int c = 0; c < COLOR_NB; ++c) {
            Bitboard sh;
            int kr = int(relative_rank(Color(c), s));
            Bitboard t = z;
            while (t.any()) {
                Square q = t.pop_lsb();
                if (int(relative_rank(Color(c), q)) <= kr) sh.set(q);
            }
            ShieldZone[c][s] = sh;
        }
    }

    for (Square s = SQ_ZERO; s < SQ_NB; ++s) {
        int f = int(file_of(s));
        int rr = int(relative_rank(BLACK, s)); // 0 = enemy back rank ... 8 = own back rank
        int adv = 8 - rr;                       // 0..8, larger = more advanced
        int centre = 4 - std::abs(f - 4);       // 0..4, larger = closer to centre file

        // Foot soldiers: reward advance, mild central bias.
        PSQT[PAWN][s]   = adv * 6 + centre * 1;
        PSQT[LANCE][s]  = adv * 3 + centre * 1;
        PSQT[KNIGHT][s] = adv * 4 + centre * 2;
        PSQT[SILVER][s] = adv * 3 + centre * 3;
        PSQT[GOLD][s]   = adv * 2 + centre * 3;
        // Big sliders: value mobility centre, small advance bias.
        PSQT[BISHOP][s] = adv * 2 + centre * 4;
        PSQT[ROOK][s]   = adv * 3 + centre * 3;
        // Promoted pieces roughly gold-shaped but a touch more central.
        PSQT[PRO_PAWN][s] = PSQT[PRO_LANCE][s] = PSQT[PRO_KNIGHT][s] = PSQT[PRO_SILVER][s]
            = adv * 2 + centre * 3;
        PSQT[HORSE][s]  = adv * 2 + centre * 5;
        PSQT[DRAGON][s] = adv * 3 + centre * 4;
        // King: prefer own back ranks and edges (castle-friendly), penalise advance.
        PSQT[KING][s]   = rr * 4 - centre * 2;
    }
}

// Mobility weight (per controlled square) for long-range pieces.
static const int MobWeight[PIECE_TYPE_NB] = {
    0, 0, 3, 0, 0, 4, 4, 0, 0, 0, 0, 0, 0, 4, 4
    // -  P  L  N  S  B  R  G  K  +p +l +n +s  H  D
};

Value evaluate(const Position& pos) {
    Value score = 0; // from BLACK's perspective
    Bitboard occ = pos.pieces();

    // --- King-safety zones (king square + its 8 neighbours), computed once. ---
    // For each zone we tally the summed effect (kiki) of attacking vs. defending
    // pieces. This is mathematically identical to summing a full effect board over
    // the zone squares, but avoids materialising a 2x81 board on every leaf eval.
    Square bk = pos.king_square(BLACK), wk = pos.king_square(WHITE);
    bool haveBK = bk != SQ_NONE, haveWK = wk != SQ_NONE;
    Bitboard bZone, wZone;
    if (haveBK) bZone = KingZone[bk];
    if (haveWK) wZone = KingZone[wk];
    int bAtk = 0, bDef = 0; // effect on BLACK king zone (atk = white pieces)
    int wAtk = 0, wDef = 0; // effect on WHITE king zone (atk = black pieces)

    // Region within which a *non-slider* could possibly attack a king zone.
    // A non-slider reaches at most a knight's distance, so dilating each zone by
    // king steps twice (Chebyshev radius 3 from the king) safely covers every
    // non-slider attacker. Far-away non-sliders can never touch a king zone, so we
    // skip their (comparatively costly) attack generation entirely.
    Bitboard influence;
    if (haveBK) influence |= KingInfluence[bk];
    if (haveWK) influence |= KingInfluence[wk];

    // --- material + PSQT + mobility + zone effect + open-file, single pass ---
    Bitboard b = occ;
    while (b.any()) {
        Square s = b.pop_lsb();
        Piece pc = pos.piece_on(s);
        PieceType pt = type_of(pc);
        Color c = color_of(pc);

        Value v = PieceValue[pt] + PSQT[pt][c == BLACK ? int(s) : 80 - int(s)];

        // Sliders (MobWeight != 0) always need attacks (mobility + long-range king
        // pressure). Non-sliders only matter if they sit near a king.
        if (MobWeight[pt]) {
            Bitboard a = attacks_bb(pc, s, occ);
            if (haveBK) { int n = (a & bZone).count(); if (c == BLACK) bDef += n; else bAtk += n; }
            if (haveWK) { int n = (a & wZone).count(); if (c == BLACK) wAtk += n; else wDef += n; }
            v += MobWeight[pt] * (a & ~pos.pieces(c)).count();

            // Heavy pieces love files unobstructed by a friendly pawn: a rook or
            // lance on an open (or semi-open) file is worth extra swing.
            if (pt == ROOK || pt == DRAGON || pt == LANCE) {
                Bitboard ownPawnsOnFile = FileBB[file_of(s)] & pos.pieces(c, PAWN);
                if (ownPawnsOnFile.none())
                    v += (pt == LANCE) ? 25 : 55;
            }
        } else if (influence.test(s)) {
            Bitboard a = attacks_bb(pc, s, occ);
            if (haveBK) { int n = (a & bZone).count(); if (c == BLACK) bDef += n; else bAtk += n; }
            if (haveWK) { int n = (a & wZone).count(); if (c == BLACK) wAtk += n; else wDef += n; }
        }
        score += (c == BLACK) ? v : -v;
    }

    // --- hand ---
    for (PieceType pt : HAND_TYPES) {
        score += HandValue[pt] * pos.count_in_hand(BLACK, pt);
        score -= HandValue[pt] * pos.count_in_hand(WHITE, pt);
    }

    // --- castle / shelter: generals guarding the king and pawns walling it in ---
    // A completed castle (囲い) is the single biggest source of practical strength;
    // reward defenders clustered around the king and a pawn wall in front of it.
    auto shelter = [&](Color c, Square k) {
        Bitboard z = KingZone[k];
        Bitboard sh = ShieldZone[c][k];
        int generals = ((pos.golds(c) | pos.pieces(c, SILVER)) & z).count();
        int wall     = (pos.pieces(c, PAWN) & sh).count();
        return generals * 30 + wall * 14;
    };
    if (haveBK) score += shelter(BLACK, bk);
    if (haveWK) score -= shelter(WHITE, wk);

    // --- king safety from zone effect tallies ---
    // Danger grows super-linearly with net attacking kiki, and — crucially — with
    // how much material the attacker holds in hand: in shogi a stockpile of drops
    // turns a modest attack into mate, so hand pressure counts only where an attack
    // has already begun.
    auto hand_pressure = [&](Color atkr) {
        const Hand& h = pos.hand_of(atkr);
        return h.get(ROOK) * 4 + h.get(BISHOP) * 3 + h.get(GOLD) * 3
             + h.get(SILVER) * 2 + h.get(KNIGHT) * 1 + h.get(LANCE) * 1;
    };
    auto king_penalty = [](int atk, int def, int handPress) {
        int danger = atk * 12 - def * 6 + (atk ? atk * handPress : 0);
        if (danger < 0) danger = 0;
        int penalty = danger + danger * danger / 64;
        return penalty > 1200 ? 1200 : penalty;
    };
    if (haveBK) score -= king_penalty(bAtk, bDef, hand_pressure(WHITE));
    if (haveWK) score += king_penalty(wAtk, wDef, hand_pressure(BLACK));

    // --- tempo: the side to move holds the initiative ---
    score += (pos.side_to_move() == BLACK) ? Tempo : -Tempo;

    return pos.side_to_move() == BLACK ? score : -score;
}

} // namespace Eval
