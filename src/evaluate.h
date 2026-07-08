#ifndef UMATAKE_EVALUATE_H
#define UMATAKE_EVALUATE_H

#include "types.h"

class Position;

namespace Eval {
void init();
Value evaluate(const Position& pos); // from side-to-move's perspective
extern Value PieceValue[PIECE_TYPE_NB];
}

#endif // UMATAKE_EVALUATE_H
