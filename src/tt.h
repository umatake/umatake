#ifndef UMATAKE_TT_H
#define UMATAKE_TT_H

#include "types.h"
#include "zobrist.h"

enum Bound : uint8_t { BOUND_NONE = 0, BOUND_UPPER = 1, BOUND_LOWER = 2, BOUND_EXACT = 3 };

struct TTEntry {
    uint32_t key32 = 0;
    Move     move = MOVE_NONE;
    int16_t  value = 0;
    int16_t  eval = 0;
    uint8_t  depth = 0;
    uint8_t  genBound = 0; // gen (top 6 bits) | bound (low 2)

    Bound bound() const { return Bound(genBound & 3); }
};

class TranspositionTable {
public:
    ~TranspositionTable();
    void resize(size_t mb);
    void clear();
    void new_search() { generation += 4; }
    TTEntry* probe(Key key, bool& found) const;
    void store(Key key, Value v, Value ev, Bound b, int depth, Move m);

private:
    TTEntry* table = nullptr;
    size_t   count = 0; // number of entries (power of two)
    uint8_t  generation = 0;
};

extern TranspositionTable TT;

// Adjust mate scores for storage (relative to root distance).
Value value_to_tt(Value v, int ply);
Value value_from_tt(Value v, int ply);

#endif // UMATAKE_TT_H
