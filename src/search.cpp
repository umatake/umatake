#include "search.h"
#include "position.h"
#include "movegen.h"
#include "evaluate.h"
#include "tt.h"
#include "usi.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cmath>
#include <iostream>
#include <vector>
#include <thread>

namespace Search {

std::atomic<bool> Stop{false};
std::atomic<bool> Pondering{false};

namespace {

// --- per-thread node counters (Lazy SMP) ---------------------------------
// Each worker counts into its own cache-line-isolated slot to avoid false
// sharing on the hottest path. The main thread sums them for reporting.
struct alignas(64) NodeCounter { uint64_t n = 0; };
constexpr int MAX_THREADS = 256;
NodeCounter NodeCount[MAX_THREADS];
thread_local int ThreadId = 0;
int NumThreads = 1;    // configured worker count (USI "Threads" option)
int ActiveThreads = 1; // workers actually running in the current search

inline uint64_t& nodes_ref() { return NodeCount[ThreadId].n; }
inline uint64_t total_nodes() {
    uint64_t t = 0;
    for (int i = 0; i < ActiveThreads; ++i) t += NodeCount[i].n;
    return t;
}

std::chrono::steady_clock::time_point StartTime;
int64_t OptimumMs = 0, MaximumMs = 0;
bool UseTimeLimit = false;
int  MaxDepth = MAX_PLY - 2;
uint64_t NodesLimit = 0;

// Killers/History are shared across Lazy-SMP workers: the concurrent writes are
// benign data races (they are only move-ordering heuristics, and any transient
// inconsistency is filtered by legality checks in the search).
Move   Killers[MAX_PLY][2];
int    History[PIECE_NB][SQ_NB];

inline int64_t elapsed_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now() - StartTime).count();
}

inline void check_time() {
    if ((nodes_ref() & 2047) != 0) return;
    // While pondering we never stop on our own — only an external stop/ponderhit
    // ends the search.
    if (Pondering.load(std::memory_order_relaxed)) return;
    if (NodesLimit && total_nodes() >= NodesLimit) Stop = true;
    if (UseTimeLimit && elapsed_ms() >= MaximumMs) Stop = true;
}

inline bool is_capture(const Position& pos, Move m) {
    return !is_drop(m) && pos.piece_on(move_to(m)) != NO_PIECE;
}

inline Piece moved_piece(const Position& pos, Move m) {
    if (is_drop(m)) return make_piece(pos.side_to_move(), dropped_type(m));
    return pos.piece_on(move_from(m));
}

// Static Exchange Evaluation on the target square of a (non-drop) capture.
// Returns the net material swing from the mover's perspective, treating pieces
// at face value (promotion gains during the exchange are ignored — a standard
// simplification). Positive = the capture wins material.
Value see(const Position& pos, Move m) {
    Square to = move_to(m);
    Square from = move_from(m);
    Bitboard occ = pos.pieces();

    Value gain[40];
    int d = 0;
    gain[0] = Eval::PieceValue[type_of(pos.piece_on(to))];
    Value nextVictim = Eval::PieceValue[type_of(pos.piece_on(from))];
    Color stm = ~color_of(pos.piece_on(from));
    occ ^= square_bb(from);
    Bitboard attackers = pos.attackers_to(to, occ) & occ;

    while (true) {
        Bitboard stmAtt = attackers & pos.pieces(stm);
        if (stmAtt.none()) break;

        // Find the least valuable attacker of the side to move.
        Square lva = SQ_NONE;
        Value  lvaVal = VALUE_INFINITE;
        for (Bitboard t = stmAtt; t.any(); ) {
            Square s = t.pop_lsb();
            Value v = Eval::PieceValue[type_of(pos.piece_on(s))];
            if (v < lvaVal) { lvaVal = v; lva = s; }
        }

        bool isKing = type_of(pos.piece_on(lva)) == KING;
        occ ^= square_bb(lva);
        Bitboard newAtt = pos.attackers_to(to, occ) & occ;
        // A king may not recapture into a square still defended by the opponent.
        if (isKing && (newAtt & pos.pieces(~stm)).any())
            break;

        ++d;
        gain[d] = nextVictim - gain[d - 1];
        nextVictim = lvaVal;
        attackers = newAtt;
        stm = ~stm;
    }
    while (--d > 0)
        gain[d - 1] = -std::max(-gain[d - 1], gain[d]);
    return gain[0];
}

// Score a move for ordering.
int score_move(const Position& pos, Move m, Move ttMove, int ply) {
    if (m == ttMove) return 1 << 28;
    if (is_capture(pos, m)) {
        Value cap = Eval::PieceValue[type_of(pos.piece_on(move_to(m)))];
        Value att = Eval::PieceValue[type_of(pos.piece_on(move_from(m)))];
        int s = (1 << 24) + cap * 32 - att;
        if (is_promote(m)) s += 1 << 20;
        return s;
    }
    if (is_promote(m)) return (1 << 23);
    if (m == Killers[ply][0]) return (1 << 22);
    if (m == Killers[ply][1]) return (1 << 21);
    return History[moved_piece(pos, m)][move_to(m)];
}

constexpr int MAX_HISTORY = 1 << 14; // keep history well below the killer tiers

// "Gravity" history update: values saturate towards ±MAX_HISTORY instead of growing
// without bound, so the fixed move-ordering tiers (killer) stay meaningful.
inline void add_history(int& h, int bonus) {
    h += bonus - h * std::abs(bonus) / MAX_HISTORY;
}

void update_quiet_stats(const Position& pos, Move m, int ply, int depth) {
    if (Killers[ply][0] != m) { Killers[ply][1] = Killers[ply][0]; Killers[ply][0] = m; }
    int bonus = std::min(depth * depth, 400);
    add_history(History[moved_piece(pos, m)][move_to(m)], bonus);
}

Value qsearch(Position& pos, Value alpha, Value beta, int ply) {
    ++nodes_ref();
    check_time();
    if (Stop) return VALUE_ZERO;
    if (ply >= MAX_PLY - 1) return Eval::evaluate(pos);

    bool inCheck = pos.in_check();
    Value best;
    if (inCheck) {
        best = mated_in(ply);
    } else {
        best = Eval::evaluate(pos);
        if (best >= beta) return best;
        if (best > alpha) alpha = best;
    }

    ExtMove list[MAX_MOVES];
    ExtMove* end = inCheck ? generate_all(pos, list) : generate_captures(pos, list);
    for (ExtMove* it = list; it != end; ++it)
        it->value = score_move(pos, it->move, MOVE_NONE, ply);

    StateInfo st;
    int moveCount = 0;
    for (ExtMove* it = list; it != end; ++it) {
        // selection sort
        ExtMove* best_it = it;
        for (ExtMove* j = it + 1; j != end; ++j) if (j->value > best_it->value) best_it = j;
        std::swap(*it, *best_it);
        Move m = it->move;

        if (!pos.legal(m)) continue;
        // SEE pruning: skip captures that lose material (unless in check, where we
        // must consider every evasion, or promoting, which adds uncounted value).
        if (!inCheck && !is_promote(m) && see(pos, m) < 0) continue;
        ++moveCount;
        pos.do_move(m, st);
        Value score = -qsearch(pos, -beta, -alpha, ply + 1);
        pos.undo_move(m);
        if (Stop) return VALUE_ZERO;
        if (score > best) {
            best = score;
            if (score > alpha) {
                alpha = score;
                if (score >= beta) break;
            }
        }
    }
    if (inCheck && moveCount == 0) return mated_in(ply);
    return best;
}

Value search(Position& pos, Value alpha, Value beta, int depth, int ply, bool isPV,
             Move prevMove) {
    if (depth <= 0) return qsearch(pos, alpha, beta, ply);

    ++nodes_ref();
    check_time();
    if (Stop) return VALUE_ZERO;

    // mate distance pruning
    alpha = std::max(alpha, mated_in(ply));
    beta  = std::min(beta, mate_in(ply + 1));
    if (alpha >= beta) return alpha;

    if (ply > 0) {
        RepetitionState r = pos.is_repetition();
        if (r == REP_DRAW) return VALUE_DRAW;
        if (r == REP_WIN)  return mate_in(ply);
        if (r == REP_LOSE) return mated_in(ply);
    }
    if (ply >= MAX_PLY - 1) return Eval::evaluate(pos);

    Key key = pos.key();
    bool found;
    TTEntry* tte = TT.probe(key, found);
    Move ttMove = found ? tte->move : MOVE_NONE;
    Value ttValue = found ? value_from_tt(tte->value, ply) : VALUE_NONE;

    if (!isPV && found && tte->depth >= depth && ttValue != VALUE_NONE) {
        if (tte->bound() & (ttValue >= beta ? BOUND_LOWER : BOUND_UPPER))
            return ttValue;
    }

    bool inCheck = pos.in_check();
    Value eval = inCheck ? VALUE_NONE : Eval::evaluate(pos);

    // Null move pruning
    if (!isPV && !inCheck && depth >= 2 && eval >= beta) {
        int R = 2 + depth / 6;
        StateInfo st;
        pos.do_null_move(st);
        Value nv = -search(pos, -beta, -beta + 1, depth - 1 - R, ply + 1, false, MOVE_NONE);
        pos.undo_null_move();
        if (Stop) return VALUE_ZERO;
        if (nv >= beta) return beta;
    }

    // Futility / razoring (shallow, non-PV, not in check)
    if (!isPV && !inCheck && depth <= 3 && eval != VALUE_NONE
        && eval - 160 * depth >= beta)
        return eval;

    ExtMove list[MAX_MOVES];
    ExtMove* end = generate_all(pos, list);
    for (ExtMove* it = list; it != end; ++it)
        it->value = score_move(pos, it->move, ttMove, ply);

    StateInfo st;
    Move bestMove = MOVE_NONE;
    Value best = -VALUE_INFINITE;
    int moveCount = 0;

    for (ExtMove* it = list; it != end; ++it) {
        ExtMove* best_it = it;
        for (ExtMove* j = it + 1; j != end; ++j) if (j->value > best_it->value) best_it = j;
        std::swap(*it, *best_it);
        Move m = it->move;

        if (!pos.legal(m)) continue;
        ++moveCount;

        bool capture = is_capture(pos, m);
        bool promo = is_promote(m);
        bool quiet = !capture && !promo;

        pos.do_move(m, st);
        bool givesCheck = pos.in_check();
        int newDepth = depth - 1 + (givesCheck ? 1 : 0);

        Value score;
        if (moveCount == 1) {
            score = -search(pos, -beta, -alpha, newDepth, ply + 1, isPV, m);
        } else {
            int red = 0;
            if (depth >= 3 && moveCount > 3 && quiet && !givesCheck && !inCheck) {
                red = 1 + (moveCount > 8 ? 1 : 0) + (depth > 6 ? 1 : 0);
                if (red >= newDepth) red = newDepth - 1;
                if (red < 0) red = 0;
            }
            score = -search(pos, -alpha - 1, -alpha, newDepth - red, ply + 1, false, m);
            if (score > alpha && (red > 0 || (isPV && score < beta)))
                score = -search(pos, -beta, -alpha, newDepth, ply + 1, isPV, m);
        }
        pos.undo_move(m);
        if (Stop) return VALUE_ZERO;

        if (score > best) {
            best = score;
            bestMove = m;
            if (score > alpha) {
                alpha = score;
                if (score >= beta) {
                    if (quiet)
                        update_quiet_stats(pos, m, ply, depth);
                    break;
                }
            }
        }
    }

    if (moveCount == 0) return mated_in(ply); // mated (no legal move in shogi)

    Bound b = best >= beta ? BOUND_LOWER
            : (isPV && bestMove != MOVE_NONE) ? BOUND_EXACT : BOUND_UPPER;
    TT.store(key, value_to_tt(best, ply), eval, b, depth, bestMove);
    return best;
}

std::string score_string(Value v) {
    char buf[32];
    if (std::abs(v) >= VALUE_MATE_IN_MAX_PLY) {
        int mate = v > 0 ? (VALUE_MATE - v + 1) / 2 : -(VALUE_MATE + v) / 2;
        snprintf(buf, sizeof buf, "mate %d", mate);
    } else {
        snprintf(buf, sizeof buf, "cp %d", v);
    }
    return buf;
}

// Extract PV by walking the TT (bounded, avoids cycles by length cap).
std::string extract_pv(Position& pos, Move first, int maxLen) {
    std::string pv;
    std::vector<Move> played;
    std::vector<StateInfo> states;
    states.reserve(maxLen + 1);
    Move m = first;
    while (m != MOVE_NONE && (int)played.size() < maxLen) {
        bool ok = false;
        for (Move lm : MoveList(pos)) if (lm == m) { ok = true; break; }
        if (!ok) break;
        pv += (played.empty() ? "" : " ") + USI::move(m);
        states.emplace_back();
        pos.do_move(m, states.back());
        played.push_back(m);
        bool found;
        TTEntry* tte = TT.probe(pos.key(), found);
        m = found ? tte->move : MOVE_NONE;
    }
    for (int i = (int)played.size() - 1; i >= 0; --i)
        pos.undo_move(played[i]);
    return pv;
}

void set_time(Position& pos, const Limits& lim) {
    UseTimeLimit = false;
    NodesLimit = lim.nodes;
    MaxDepth = lim.depth ? lim.depth : MAX_PLY - 2;
    if (lim.infinite) return;
    if (lim.nodes) return;

    if (lim.movetime > 0) {
        UseTimeLimit = true;
        OptimumMs = MaximumMs = std::max<int64_t>(1, lim.movetime - 30);
        return;
    }
    Color us = pos.side_to_move();
    int t = lim.time[us];
    int inc = lim.inc[us];
    int byo = lim.byoyomi;
    if (t == 0 && byo == 0 && inc == 0) return; // no limit given -> depth/infinite

    UseTimeLimit = true;
    int64_t total = t + byo;
    int64_t opt = t / 30 + inc + byo;
    int64_t mx  = t / 8 + inc + byo;
    int64_t margin = 40;
    OptimumMs = std::max<int64_t>(1, std::min<int64_t>(opt, total - margin));
    MaximumMs = std::max<int64_t>(OptimumMs, std::min<int64_t>(mx, total - margin));
}

} // namespace

void init() {
    TT.resize(64);
    clear();
}

void set_threads(int n) {
    if (n < 1) n = 1;
    if (n > MAX_THREADS) n = MAX_THREADS;
    NumThreads = n;
}

void clear() {
    TT.clear();
    for (int i = 0; i < MAX_PLY; ++i) Killers[i][0] = Killers[i][1] = MOVE_NONE;
    for (int p = 0; p < PIECE_NB; ++p) for (int s = 0; s < SQ_NB; ++s)
        History[p][s] = 0;
}

// One worker's iterative-deepening loop. The main worker (isMain) manages time,
// prints "info", and emits the final "bestmove"; helper workers only warm the
// shared transposition table so the main worker searches deeper in the same time.
void iterative_deepening(Position& pos, bool isMain) {
    MoveList rootMoves(pos);
    if (rootMoves.size() == 0) return; // resign handled by think() for the main worker

    Move bestMove = rootMoves.begin()->move;
    StateInfo st;
    Value prevScore = VALUE_ZERO;

    // Run one full pass over the root moves with the given window. Returns the best
    // score; writes the best move to `outBest`. Records per-move scores for ordering.
    auto run_root = [&](int depth, Value alpha, Value beta, Move& outBest) -> Value {
        Value best = -VALUE_INFINITE;
        // order: current best first, then by remembered score
        for (ExtMove* it = rootMoves.begin(); it != rootMoves.end(); ++it)
            if (it->move == bestMove) it->value = (1 << 30);

        int moveCount = 0;
        for (ExtMove* it = rootMoves.begin(); it != rootMoves.end(); ++it) {
            ExtMove* best_it = it;
            for (ExtMove* j = it + 1; j != rootMoves.end(); ++j)
                if (j->value > best_it->value) best_it = j;
            std::swap(*it, *best_it);
            Move m = it->move;
            ++moveCount;

            pos.do_move(m, st);
            bool givesCheck = pos.in_check();
            int newDepth = depth - 1 + (givesCheck ? 1 : 0);
            Value score;
            if (moveCount == 1)
                score = -search(pos, -beta, -alpha, newDepth, 1, true, m);
            else {
                score = -search(pos, -alpha - 1, -alpha, newDepth, 1, false, m);
                if (score > alpha && score < beta)
                    score = -search(pos, -beta, -alpha, newDepth, 1, true, m);
            }
            pos.undo_move(m);

            if (Stop) break;
            it->value = score; // remember for next iteration ordering
            if (score > best) {
                best = score;
                outBest = m;
                if (score > alpha) alpha = score;
            }
        }
        return best;
    };

    for (int depth = 1; depth <= MaxDepth; ++depth) {
        Move iterationBest = bestMove;
        Value best;

        // Aspiration windows: from a stable depth, search a narrow band around the
        // previous score and re-search wider only on fail-high/low. Saves nodes.
        if (depth <= 4) {
            best = run_root(depth, -VALUE_INFINITE, VALUE_INFINITE, iterationBest);
        } else {
            Value delta = 24;
            Value alpha = std::max<Value>(prevScore - delta, -VALUE_INFINITE);
            Value beta  = std::min<Value>(prevScore + delta,  VALUE_INFINITE);
            for (;;) {
                best = run_root(depth, alpha, beta, iterationBest);
                if (Stop) break;
                if (best <= alpha) {            // fail low: relax alpha
                    beta = (alpha + beta) / 2;
                    alpha = std::max<Value>(best - delta, -VALUE_INFINITE);
                } else if (best >= beta) {      // fail high: relax beta
                    beta = std::min<Value>(best + delta, VALUE_INFINITE);
                } else break;                   // within window
                delta += delta / 2 + 8;
            }
        }

        if (!Stop) { bestMove = iterationBest; prevScore = best; }

        // Report every depth. If Stop hit before this iteration produced any
        // usable score, `best` is still -VALUE_INFINITE (which would render as a
        // bogus "score mate 0"); fall back to the last completed score so the
        // line stays coherent instead.
        if (isMain) {
            int64_t ms = elapsed_ms();
            uint64_t nds = total_nodes();
            Value reportScore = (best > -VALUE_INFINITE) ? best : prevScore;
            std::string pv = extract_pv(pos, bestMove, depth);
            std::cout << "info depth " << depth
                      << " score " << score_string(reportScore)
                      << " nodes " << nds
                      << " nps " << (ms > 0 ? (uint64_t)(nds * 1000 / ms) : nds)
                      << " time " << ms
                      << " pv " << pv << std::endl;
        }

        if (Stop) break;
        // Time and mate stops apply only to the main worker; helpers run until an
        // external Stop so they keep filling the TT. Neither applies while pondering.
        if (isMain && !Pondering.load(std::memory_order_relaxed)) {
            if (UseTimeLimit && elapsed_ms() >= OptimumMs) break;
            if (std::abs(best) >= VALUE_MATE_IN_MAX_PLY) break;
        }
    }

    if (!isMain) return;

    // If we ran out of work (mate found or max depth) while still pondering, USI
    // forbids sending "bestmove" until ponderhit/stop arrives — so wait for it.
    while (Pondering.load(std::memory_order_relaxed) && !Stop)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

    // Offer a ponder move (the predicted reply) from the TT if it is legal.
    Move ponderMove = MOVE_NONE;
    {
        StateInfo pst;
        pos.do_move(bestMove, pst);
        bool f;
        TTEntry* e = TT.probe(pos.key(), f);
        if (f && e->move != MOVE_NONE)
            for (Move lm : MoveList(pos))
                if (lm == e->move) { ponderMove = e->move; break; }
        pos.undo_move(bestMove);
    }

    std::cout << "bestmove " << USI::move(bestMove);
    if (ponderMove != MOVE_NONE) std::cout << " ponder " << USI::move(ponderMove);
    std::cout << std::endl;
}

void think(Position& pos, const Limits& limits) {
    // Note: Stop is reset by the caller (go_cmd) *before* this thread is created,
    // so we must not reset it here — doing so would race with an external "stop".
    Pondering = limits.ponder;
    StartTime = std::chrono::steady_clock::now();
    set_time(pos, limits);
    TT.new_search();
    for (int i = 0; i < MAX_PLY; ++i) Killers[i][0] = Killers[i][1] = MOVE_NONE;

    // 入玉 nyugyoku: if the entering-king declaration conditions are met, claiming
    // the win immediately is strictly optimal.
    if (pos.can_declare_win()) {
        std::cout << "bestmove win" << std::endl;
        return;
    }

    MoveList rootMoves(pos);
    if (rootMoves.size() == 0) {
        std::cout << "bestmove resign" << std::endl;
        return;
    }

    // Fixed-nodes searches stay single-threaded so the node budget (and thus the
    // result) is deterministic — essential for reproducible A/B testing.
    int n = (NodesLimit ? 1 : NumThreads);
    if (n < 1) n = 1;
    if (n > MAX_THREADS) n = MAX_THREADS;
    ActiveThreads = n;
    for (int i = 0; i < n; ++i) NodeCount[i].n = 0;

    // Launch helper workers, each on its own copy of the position (do_move mutates
    // the board). The copy shares the root StateInfo chain, so every worker sees
    // the full pre-search history for repetition detection.
    std::vector<std::thread> helpers;
    helpers.reserve(n - 1);
    for (int i = 1; i < n; ++i) {
        helpers.emplace_back([&pos, i]() {
            ThreadId = i;
            Position hp = pos;
            iterative_deepening(hp, false);
        });
    }

    ThreadId = 0;
    iterative_deepening(pos, true);

    // The main worker has produced the answer; tell helpers to stop and reap them.
    Stop = true;
    for (auto& t : helpers) t.join();
}

uint64_t bench_search(Position& pos, int depth) {
    Stop = false;
    Pondering = false;
    ThreadId = 0; ActiveThreads = 1; NodeCount[0].n = 0; // bench is always single-threaded
    StartTime = std::chrono::steady_clock::now();
    UseTimeLimit = false; NodesLimit = 0; MaxDepth = depth;
    TT.new_search();
    for (int i = 0; i < MAX_PLY; ++i) Killers[i][0] = Killers[i][1] = MOVE_NONE;

    StateInfo st;
    for (int d = 1; d <= depth; ++d) {
        search(pos, -VALUE_INFINITE, VALUE_INFINITE, d, 0, true, MOVE_NONE);
        if (Stop) break;
    }
    return NodeCount[0].n;
}

} // namespace Search
