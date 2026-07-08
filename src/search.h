#ifndef UMATAKE_SEARCH_H
#define UMATAKE_SEARCH_H

#include "types.h"
#include <atomic>
#include <cstdint>

class Position;

namespace Search {

struct Limits {
    int      time[COLOR_NB] = {0, 0};
    int      inc[COLOR_NB]  = {0, 0};
    int      byoyomi   = 0;
    int      movetime  = 0;
    int      depth     = 0;
    uint64_t nodes     = 0;
    bool     infinite  = false;
    bool     ponder    = false;
};

extern std::atomic<bool> Stop;
extern std::atomic<bool> Pondering; // true while searching on a predicted (ponder) move

void init();
void clear();
void set_threads(int n);                          // Lazy SMP worker count (>= 1)
void think(Position& pos, const Limits& limits); // prints info + bestmove
uint64_t bench_search(Position& pos, int depth);

} // namespace Search

#endif // UMATAKE_SEARCH_H
