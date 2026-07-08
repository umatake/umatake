#include "tt.h"
#include <cstdlib>
#include <cstring>

TranspositionTable TT;

TranspositionTable::~TranspositionTable() { std::free(table); }

void TranspositionTable::resize(size_t mb) {
    std::free(table);
    size_t bytes = mb * 1024 * 1024;
    size_t n = bytes / sizeof(TTEntry);
    // round down to power of two
    size_t pow = 1;
    while (pow * 2 <= n) pow *= 2;
    count = pow ? pow : 1;
    table = (TTEntry*)std::malloc(count * sizeof(TTEntry));
    clear();
}

void TranspositionTable::clear() {
    if (table) std::memset(table, 0, count * sizeof(TTEntry));
    generation = 0;
}

TTEntry* TranspositionTable::probe(Key key, bool& found) const {
    TTEntry* e = &table[key & (count - 1)];
    found = (e->key32 == (uint32_t)(key >> 32)) && e->depth != 0;
    return e;
}

void TranspositionTable::store(Key key, Value v, Value ev, Bound b, int depth, Move m) {
    TTEntry* e = &table[key & (count - 1)];
    uint32_t k32 = (uint32_t)(key >> 32);
    // Replace if empty, same key, or shallower/older.
    if (e->key32 != k32 || depth + 4 > e->depth || b == BOUND_EXACT) {
        if (m != MOVE_NONE || e->key32 != k32) e->move = m;
        e->key32 = k32;
        e->value = (int16_t)v;
        e->eval  = (int16_t)ev;
        e->depth = (uint8_t)depth;
        e->genBound = (uint8_t)(generation | b);
    }
}

Value value_to_tt(Value v, int ply) {
    if (v >= VALUE_MATE_IN_MAX_PLY) return v + ply;
    if (v <= VALUE_MATED_IN_MAX_PLY) return v - ply;
    return v;
}

Value value_from_tt(Value v, int ply) {
    if (v >= VALUE_MATE_IN_MAX_PLY) return v - ply;
    if (v <= VALUE_MATED_IN_MAX_PLY) return v + ply;
    return v;
}
