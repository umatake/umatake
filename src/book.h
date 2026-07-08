#ifndef UMATAKE_BOOK_H
#define UMATAKE_BOOK_H

#include <string>
#include <vector>

// A tiny, self-contained opening book (定跡).
//
// Lines are stored as sequences of USI moves from the initial position. Probing
// matches the game's move history against every stored line and, if the history
// is an exact prefix of one or more lines, returns one of the continuing moves
// (chosen at random for variety). The caller is responsible for verifying that
// the returned move is legal in the current position before playing it — the
// book itself performs no board reasoning, so this legality check is the safety
// net that keeps a mistyped line from ever reaching the board.
namespace Book {

void init();

// `history` is the list of USI move strings played from the start position.
// Returns a book reply (USI string) or an empty string when out of book.
std::string probe(const std::vector<std::string>& history);

} // namespace Book

#endif // UMATAKE_BOOK_H
