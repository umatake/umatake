#include "bench.h"
#include "position.h"
#include "movegen.h"
#include "usi.h"
#include "search.h"
#include <cstdio>
#include <chrono>
#include <vector>

uint64_t perft(Position& pos, int depth) {
    if (depth == 0) return 1;
    ExtMove list[MAX_MOVES];
    ExtMove* end = generate_all(pos, list);
    uint64_t nodes = 0;
    StateInfo st;
    for (ExtMove* it = list; it != end; ++it) {
        if (!pos.legal(it->move)) continue;
        if (depth == 1) { ++nodes; continue; }
        pos.do_move(it->move, st);
        nodes += perft(pos, depth - 1);
        pos.undo_move(it->move);
    }
    return nodes;
}

void perft_divide(Position& pos, int depth) {
    ExtMove list[MAX_MOVES];
    ExtMove* end = generate_all(pos, list);
    uint64_t total = 0;
    StateInfo st;
    for (ExtMove* it = list; it != end; ++it) {
        if (!pos.legal(it->move)) continue;
        pos.do_move(it->move, st);
        uint64_t n = depth > 1 ? perft(pos, depth - 1) : 1;
        pos.undo_move(it->move);
        printf("%s: %llu\n", USI::move(it->move).c_str(), (unsigned long long)n);
        total += n;
    }
    printf("total: %llu\n", (unsigned long long)total);
}

void run_perft_suite() {
    struct { int depth; uint64_t expected; } cases[] = {
        {1, 30ULL}, {2, 900ULL}, {3, 25470ULL}, {4, 719731ULL}, {5, 19861490ULL},
    };
    StateInfo si;
    bool allOk = true;
    for (auto& c : cases) {
        Position pos;
        pos.set_startpos(&si);
        auto t0 = std::chrono::steady_clock::now();
        uint64_t n = perft(pos, c.depth);
        auto t1 = std::chrono::steady_clock::now();
        double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        bool ok = (n == c.expected);
        allOk &= ok;
        printf("perft(%d) = %-12llu expected %-12llu %s  (%.0f ms, %.2f Mnps)\n",
               c.depth, (unsigned long long)n, (unsigned long long)c.expected,
               ok ? "OK" : "FAIL", ms, ms > 0 ? n / ms / 1000.0 : 0.0);
    }
    printf("%s\n", allOk ? "ALL PERFT PASSED" : "PERFT FAILED");
}

void run_bench() {
    const char* positions[] = {
        "lnsgkgsnl/1r5b1/ppppppppp/9/9/9/PPPPPPPPP/1B5R1/LNSGKGSNL b - 1",
        "l6nl/5+P1gk/2np1S3/p1p4Pp/3P2Sp1/1PPb2P1P/P5GS1/R8/LN4bKL w RGgsn5p 1",
        "ln1gk2nl/1r4gs1/p1sppp1pp/2p3p2/1p7/2P6/PPSPPPPPP/2G2S1R1/LN2KG1NL b Bb 1",
    };
    uint64_t totalNodes = 0;
    auto t0 = std::chrono::steady_clock::now();
    for (const char* p : positions) {
        StateInfo si;
        Position pos;
        pos.set(p, &si);
        totalNodes += Search::bench_search(pos, 7);
    }
    auto t1 = std::chrono::steady_clock::now();
    double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    printf("bench: %llu nodes  %.0f ms  %.0f knps\n",
           (unsigned long long)totalNodes, ms, ms > 0 ? totalNodes / ms : 0.0);
}
