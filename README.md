# Umatake 🐴

A USI-compatible Shogi (将棋) engine written in modern C++17.

Umatake plays standard Shogi and speaks the [USI protocol](http://hgm.nubati.net/usi.html), so it can be driven by any USI GUI (ShogiGUI, ShogiHome, etc.) or by another program over stdin/stdout.

## Features

- **Board representation** — bitboard-based move generation with Zobrist hashing
- **Search** — alpha-beta with Principal Variation Search
  - Iterative deepening with aspiration windows
  - Transposition table
  - Null-move pruning
  - Futility pruning / razoring
  - SEE-based capture pruning in quiescence
  - Killer and gravity-history move ordering
  - Mate-distance pruning and repetition (sennichite) detection
  - **Lazy SMP** multithreaded search
  - Pondering support
- **Evaluation** — material, piece-square tables, mobility for long-range pieces, king-zone (safety) terms, and pieces in hand

## Requirements

- A C++17 compiler (`clang++` or `g++`)
- `make`
- Python 3 (optional, for `scripts/selfplay.py`)

## Building

```sh
make              # optimized build -> ./umatake
make debug        # -O1 build with ASan/UBSan
make clean        # remove objects and binary
```

The optimized build uses `-O3 -flto` and native architecture tuning
(`-mcpu=native` on Apple Silicon, `-march=native` elsewhere).

## Usage

Umatake reads USI commands on standard input:

```sh
./umatake
usi
isready
usinewgame
position startpos moves 7g7f 3c3d
go btime 300000 wtime 300000 byoyomi 5000
```

It also accepts a couple of one-shot subcommands:

```sh
./umatake bench     # fixed-position search benchmark
./umatake perft     # run the perft (move-generation) test suite
make bench          # build then run bench
make perft          # build then run perft
```

### USI options

| Option        | Type  | Default | Range     |
| ------------- | ----- | ------- | --------- |
| `USI_Hash`    | spin  | 64 (MB) | 1–4096    |
| `USI_Ponder`  | check | false   | —         |
| `Threads`     | spin  | 1       | 1–256     |

## Testing

```sh
tests/usi_test.sh          # smoke test of the USI command loop
make perft                 # verify move generation against known node counts
python3 scripts/selfplay.py  # run engine-vs-engine self-play games
```

## Project layout

```
src/
  bitboard.*    Bitboards and attack tables
  position.*    Board state, make/unmake, SFEN I/O
  movegen.*     Legal move generation
  evaluate.*    Static evaluation
  search.*      Alpha-beta / PVS search (Lazy SMP)
  tt.*          Transposition table
  usi.*         USI protocol loop
  zobrist.*     Zobrist hashing
  bench.*       Perft and benchmark harness
  types.h       Core types
scripts/
  selfplay.py   Engine self-play driver
tests/
  usi_test.sh   USI smoke test
```

## License

GPL-3.0.
