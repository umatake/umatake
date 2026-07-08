#ifndef UMATAKE_BENCH_H
#define UMATAKE_BENCH_H

#include <cstdint>
#include <string>

class Position;

uint64_t perft(Position& pos, int depth);
void perft_divide(Position& pos, int depth);
void run_perft_suite();
void run_bench();

#endif // UMATAKE_BENCH_H
