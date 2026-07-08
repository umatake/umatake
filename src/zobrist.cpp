#include "zobrist.h"

namespace Zobrist {
Key psq[PIECE_NB][SQ_NB];
Key hand[COLOR_NB][8][19];
Key side;
}

namespace {
// SplitMix64 PRNG for reproducible keys.
struct Rng {
    uint64_t s;
    explicit Rng(uint64_t seed) : s(seed) {}
    uint64_t next() {
        uint64_t z = (s += 0x9E3779B97F4A7C15ULL);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    }
};
}

namespace Zobrist {
void init() {
    Rng rng(20240607ULL);
    for (int pc = 0; pc < PIECE_NB; ++pc)
        for (int s = 0; s < SQ_NB; ++s)
            psq[pc][s] = rng.next();
    for (int c = 0; c < COLOR_NB; ++c)
        for (int pt = 0; pt < 8; ++pt)
            for (int n = 0; n < 19; ++n)
                hand[c][pt][n] = rng.next();
    side = rng.next();
}
}
