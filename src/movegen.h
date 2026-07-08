#ifndef UMATAKE_MOVEGEN_H
#define UMATAKE_MOVEGEN_H

#include "types.h"
#include "position.h"

struct ExtMove {
    Move move;
    int  value;
    operator Move() const { return move; }
};

// Pseudo-legal generators (king-safety NOT applied; caller filters with Position::legal).
// Drops already respect nifu / dead-piece rules.
ExtMove* generate_all(const Position& pos, ExtMove* list);      // all pseudo-legal
ExtMove* generate_captures(const Position& pos, ExtMove* list); // captures + promotions (for qsearch)

// Fully-legal move list (convenience; applies legal()).
struct MoveList {
    ExtMove moves[MAX_MOVES];
    ExtMove* last;
    explicit MoveList(Position& pos) {
        ExtMove* end = generate_all(pos, moves);
        last = moves;
        for (ExtMove* it = moves; it != end; ++it)
            if (pos.legal(it->move)) *last++ = *it;
    }
    ExtMove* begin() { return moves; }
    ExtMove* end() { return last; }
    size_t size() const { return last - moves; }
};

#endif // UMATAKE_MOVEGEN_H
