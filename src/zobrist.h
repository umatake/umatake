#ifndef UMATAKE_ZOBRIST_H
#define UMATAKE_ZOBRIST_H

#include "types.h"

using Key = uint64_t;

namespace Zobrist {
extern Key psq[PIECE_NB][SQ_NB];       // board piece on square
extern Key hand[COLOR_NB][8][19];      // hand: color, piece type (1..7), count (0..18)
extern Key side;                        // side to move (XOR when White to move)
void init();
}

#endif // UMATAKE_ZOBRIST_H
