#include "bitboard.h"
#include "zobrist.h"
#include "evaluate.h"
#include "search.h"
#include "usi.h"
#include "book.h"
#include "bench.h"
#include "position.h"
#include <string>
#include <cstring>

int main(int argc, char** argv) {
    Bitboards::init();
    Zobrist::init();
    Eval::init();
    Search::init();
    Book::init();

    if (argc >= 2) {
        std::string cmd = argv[1];
        if (cmd == "perft") {
            run_perft_suite();
            return 0;
        }
        if (cmd == "bench") {
            run_bench();
            return 0;
        }
    }

    USI::loop();
    return 0;
}
