#include "movegen.h"

namespace {

inline void emit_board_move(Color us, Square from, Square to, PieceType pt, ExtMove*& list) {
    bool canProm = can_promote_type(pt) &&
                   (relative_rank(us, from) <= RANK_C || relative_rank(us, to) <= RANK_C);
    bool mustProm = false;
    if (pt == PAWN || pt == LANCE) mustProm = (relative_rank(us, to) == RANK_A);
    else if (pt == KNIGHT)         mustProm = (relative_rank(us, to) <= RANK_B);

    if (mustProm) {
        (*list++).move = make_move(from, to, true);
    } else {
        (*list++).move = make_move(from, to, false);
        if (canProm) (*list++).move = make_move(from, to, true);
    }
}

} // namespace

ExtMove* generate_all(const Position& pos, ExtMove* list) {
    Color us = pos.side_to_move();
    Bitboard occ = pos.pieces();
    Bitboard ours = pos.pieces(us);
    Bitboard notOurs = ~ours;

    // --- board moves ---
    Bitboard fromBB = ours;
    while (fromBB.any()) {
        Square from = fromBB.pop_lsb();
        Piece pc = pos.piece_on(from);
        PieceType pt = type_of(pc);
        Bitboard att = attacks_bb(pc, from, occ) & notOurs;
        while (att.any()) {
            Square to = att.pop_lsb();
            emit_board_move(us, from, to, pt, list);
        }
    }

    // --- drops ---
    const Hand& h = pos.hand_of(us);
    Bitboard emptyBB = ~occ;
    bool pawnFile[FILE_NB] = {false};
    Bitboard ourPawns = pos.pieces(us, PAWN);
    while (ourPawns.any()) { Square s = ourPawns.pop_lsb(); pawnFile[file_of(s)] = true; }

    for (PieceType pt : HAND_TYPES) {
        if (!h.get(pt)) continue;
        Bitboard t = emptyBB;
        while (t.any()) {
            Square to = t.pop_lsb();
            int rr = relative_rank(us, to);
            if (pt == PAWN)   { if (rr == RANK_A || pawnFile[file_of(to)]) continue; }
            else if (pt == LANCE)  { if (rr == RANK_A) continue; }
            else if (pt == KNIGHT) { if (rr <= RANK_B) continue; }
            (*list++).move = make_drop(pt, to);
        }
    }
    return list;
}

ExtMove* generate_captures(const Position& pos, ExtMove* list) {
    Color us = pos.side_to_move();
    Bitboard occ = pos.pieces();
    Bitboard ours = pos.pieces(us);
    Bitboard enemy = pos.pieces(~us);
    Bitboard notOurs = ~ours;

    Bitboard fromBB = ours;
    while (fromBB.any()) {
        Square from = fromBB.pop_lsb();
        Piece pc = pos.piece_on(from);
        PieceType pt = type_of(pc);
        Bitboard att = attacks_bb(pc, from, occ) & notOurs;
        while (att.any()) {
            Square to = att.pop_lsb();
            bool isCap = enemy.test(to);
            bool canProm = can_promote_type(pt) &&
                           (relative_rank(us, from) <= RANK_C || relative_rank(us, to) <= RANK_C);
            if (isCap) {
                emit_board_move(us, from, to, pt, list);
            } else if (canProm) {
                (*list++).move = make_move(from, to, true); // non-capture promotion
            }
        }
    }
    return list;
}
