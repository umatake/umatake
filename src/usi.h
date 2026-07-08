#ifndef UMATAKE_USI_H
#define UMATAKE_USI_H

#include "types.h"
#include <string>

class Position;

namespace USI {
void loop();
std::string square(Square s);
std::string move(Move m);
Move to_move(const std::string& str);
}

#endif // UMATAKE_USI_H
