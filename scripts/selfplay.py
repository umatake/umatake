#!/usr/bin/env python3
"""Self-play / match driver for the Umatake USI engine.

Runs two USI engines (default: Umatake vs itself) against each other and
reports the result. Verifies the engine only ever produces legal moves
(the opponent engine will reject illegal input by simply losing on a bad
move — but primarily this validates that games reach a natural end).

Usage:
    scripts/selfplay.py [--engine1 ./umatake] [--engine2 ./umatake]
                        [--movetime 200] [--games 2] [--maxmoves 400]
"""
import argparse
import subprocess
import sys
import time


class Engine:
    def __init__(self, path, name):
        self.name = name
        self.p = subprocess.Popen(
            [path], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            text=True, bufsize=1)
        self._send("usi")
        self._wait("usiok")
        self._send("isready")
        self._wait("readyok")

    def _send(self, cmd):
        self.p.stdin.write(cmd + "\n")
        self.p.stdin.flush()

    def _wait(self, token):
        while True:
            line = self.p.stdout.readline()
            if line == "":
                raise RuntimeError(f"{self.name}: engine closed pipe")
            if line.strip().startswith(token):
                return line.strip()

    def newgame(self):
        self._send("usinewgame")

    def bestmove(self, moves, movetime, depth=0, nodes=0):
        pos = "position startpos"
        if moves:
            pos += " moves " + " ".join(moves)
        self._send(pos)
        # Fixed-nodes search is contention-immune: each engine gets exactly the
        # same amount of work per move regardless of CPU load, so the comparison
        # stays fair even on a busy machine. Depth and movetime are the fallbacks.
        if nodes:
            self._send(f"go nodes {nodes}")
        elif depth:
            self._send(f"go depth {depth}")
        else:
            self._send(f"go movetime {movetime}")
        line = self._wait("bestmove")
        return line.split()[1]

    def quit(self):
        try:
            self._send("quit")
            self.p.wait(timeout=3)
        except Exception:
            self.p.kill()


# Short, standard, known-legal opening lines (USI) to diversify self-play games.
# A varied start is essential for A/B signal: two deterministic engines from the
# same startpos otherwise replay only a handful of distinct games.
OPENINGS = [
    [],
    ["7g7f", "3c3d"],
    ["2g2f", "8c8d"],
    ["7g7f", "8c8d", "2g2f", "8d8e"],
    ["5g5f", "5c5d"],
    ["7g7f", "3c3d", "6g6f"],
    ["2g2f", "3c3d", "7g7f", "4c4d"],
    ["1g1f", "1c1d"],
    ["7g7f", "8c8d", "7i6h"],
    ["9g9f", "9c9d"],
    ["3g3f", "5c5d"],
    ["4g4f", "3c3d"],
]


def play_game(e1, e2, movetime, maxmoves, depth=0, opening=None, nodes=0):
    e1.newgame(); e2.newgame()
    moves = list(opening) if opening else []
    engines = [e1, e2]  # index 0 = Black (sente)
    start_ply = len(moves)
    for ply in range(start_ply, maxmoves):
        mover = engines[ply % 2]
        bm = mover.bestmove(moves, movetime, depth, nodes)
        if bm == "win":
            # 入玉 declaration: the side to move claims an immediate win.
            return mover.name, f"{mover.name} declared win (nyugyoku) at ply {ply}", moves
        if bm in ("resign", "0000", ""):
            winner = engines[(ply + 1) % 2]
            return winner.name, f"{mover.name} {bm} at ply {ply}", moves
        moves.append(bm)
    return "draw", f"reached move limit ({maxmoves})", moves


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--engine1", default="./umatake")
    ap.add_argument("--engine2", default="./umatake")
    ap.add_argument("--movetime", type=int, default=200)
    ap.add_argument("--depth", type=int, default=0,
                    help="fixed search depth (overrides movetime when > 0)")
    ap.add_argument("--nodes", type=int, default=0,
                    help="fixed node count per move (contention-immune; overrides depth/movetime)")
    ap.add_argument("--games", type=int, default=2)
    ap.add_argument("--maxmoves", type=int, default=400)
    ap.add_argument("--openings", action="store_true",
                    help="diversify games using a small opening book "
                         "(same opening for each color-swapped pair)")
    args = ap.parse_args()

    score = {args.engine1: 0.0, args.engine2: 0.0, "draw": 0.0}
    for g in range(args.games):
        # alternate colors
        if g % 2 == 0:
            e1 = Engine(args.engine1, "E1"); e2 = Engine(args.engine2, "E2")
            names = {"E1": args.engine1, "E2": args.engine2}
        else:
            e1 = Engine(args.engine2, "E1"); e2 = Engine(args.engine1, "E2")
            names = {"E1": args.engine2, "E2": args.engine1}
        opening = OPENINGS[(g // 2) % len(OPENINGS)] if args.openings else None
        t0 = time.time()
        winner, reason, moves = play_game(e1, e2, args.movetime, args.maxmoves,
                                          args.depth, opening, args.nodes)
        dt = time.time() - t0
        e1.quit(); e2.quit()
        wname = names.get(winner, winner)
        print(f"game {g+1}: winner={wname:12s} plies={len(moves):3d} "
              f"({reason}) [{dt:.1f}s]", flush=True)
        if winner == "draw":
            score["draw"] += 1
        else:
            score[wname] += 1

    print("---")
    print(f"{args.engine1}: {score.get(args.engine1,0)}  "
          f"{args.engine2}: {score.get(args.engine2,0)}  "
          f"draws: {score['draw']}")


if __name__ == "__main__":
    sys.exit(main())
