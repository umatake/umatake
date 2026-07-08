#include "usi.h"
#include "position.h"
#include "movegen.h"
#include "search.h"
#include "tt.h"
#include "bench.h"
#include <iostream>
#include <sstream>
#include <string>
#include <deque>
#include <thread>

namespace {
const char* STARTPOS = "lnsgkgsnl/1r5b1/ppppppppp/9/9/9/PPPPPPPPP/1B5R1/LNSGKGSNL b - 1";

char type_letter(PieceType pt) {
    switch (raw_type(pt)) {
        case PAWN: return 'P'; case LANCE: return 'L'; case KNIGHT: return 'N';
        case SILVER: return 'S'; case BISHOP: return 'B'; case ROOK: return 'R';
        case GOLD: return 'G'; case KING: return 'K'; default: return '?';
    }
}
PieceType letter_type(char c) {
    switch (c) {
        case 'P': return PAWN; case 'L': return LANCE; case 'N': return KNIGHT;
        case 'S': return SILVER; case 'B': return BISHOP; case 'R': return ROOK;
        case 'G': return GOLD; case 'K': return KING; default: return NO_PIECE_TYPE;
    }
}
} // namespace

namespace USI {

std::string square(Square s) {
    std::string r;
    r += char('1' + int(file_of(s)));
    r += char('a' + int(rank_of(s)));
    return r;
}

std::string move(Move m) {
    if (m == MOVE_NONE) return "resign";
    if (m == MOVE_NULL) return "0000";
    if (is_drop(m)) {
        std::string r;
        r += type_letter(dropped_type(m));
        r += '*';
        r += square(move_to(m));
        return r;
    }
    std::string r = square(move_from(m)) + square(move_to(m));
    if (is_promote(m)) r += '+';
    return r;
}

Move to_move(const std::string& str) {
    if (str.size() < 4) return MOVE_NONE;
    auto sq = [](char f, char r) {
        return make_square(File(f - '1'), Rank(r - 'a'));
    };
    if (str[1] == '*') {
        PieceType pt = letter_type(str[0]);
        return make_drop(pt, sq(str[2], str[3]));
    }
    Square from = sq(str[0], str[1]);
    Square to = sq(str[2], str[3]);
    bool promo = (str.size() >= 5 && str[4] == '+');
    return make_move(from, to, promo);
}

namespace {

Position pos;
std::deque<StateInfo> states;
std::thread searchThread;

void stop_search() {
    Search::Stop = true;
    if (searchThread.joinable()) searchThread.join();
}

void position_cmd(std::istringstream& is) {
    std::string token, sfen;
    is >> token;
    if (token == "startpos") {
        sfen = STARTPOS;
        is >> token; // maybe "moves"
    } else if (token == "sfen") {
        std::string a, b, c, d;
        is >> a >> b >> c >> d;
        sfen = a + " " + b + " " + c + " " + d;
        is >> token; // maybe "moves"
    } else {
        return;
    }

    states.clear();
    states.emplace_back();
    pos.set(sfen, &states.back());

    if (token == "moves") {
        while (is >> token) {
            Move m = to_move(token);
            states.emplace_back();
            pos.do_move(m, states.back());
        }
    }
}

void go_cmd(std::istringstream& is) {
    Search::Limits lim;
    std::string token;
    while (is >> token) {
        if (token == "btime") is >> lim.time[BLACK];
        else if (token == "wtime") is >> lim.time[WHITE];
        else if (token == "binc") is >> lim.inc[BLACK];
        else if (token == "winc") is >> lim.inc[WHITE];
        else if (token == "byoyomi") is >> lim.byoyomi;
        else if (token == "movetime") is >> lim.movetime;
        else if (token == "depth") is >> lim.depth;
        else if (token == "nodes") is >> lim.nodes;
        else if (token == "infinite") lim.infinite = true;
        else if (token == "ponder") lim.ponder = true;
        else if (token == "mate") { is >> token; }
    }
    stop_search();
    Search::Stop = false;
    searchThread = std::thread([lim]() { Search::think(pos, lim); });
}

} // namespace

void loop() {
    StateInfo si;
    pos.set(STARTPOS, &si);
    states.clear();
    states.emplace_back();
    pos.set(STARTPOS, &states.back());

    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream is(line);
        std::string cmd;
        is >> cmd;

        if (cmd == "usi") {
            std::cout << "id name Umatake\n";
            std::cout << "id author Umatake Project\n";
            std::cout << "option name USI_Hash type spin default 64 min 1 max 4096\n";
            std::cout << "option name USI_Ponder type check default false\n";
            std::cout << "option name Threads type spin default 1 min 1 max 256\n";
            std::cout << "usiok" << std::endl;
        } else if (cmd == "isready") {
            std::cout << "readyok" << std::endl;
        } else if (cmd == "setoption") {
            std::string t, name, value;
            is >> t >> name; // "name" <name>
            while (is >> t) {
                if (t == "value") { is >> value; break; }
                name += " " + t;
            }
            if (name == "USI_Hash") { try { TT.resize(std::stoul(value)); } catch (...) {} }
            else if (name == "Threads") { try { Search::set_threads(std::stoi(value)); } catch (...) {} }
        } else if (cmd == "usinewgame") {
            Search::clear();
        } else if (cmd == "position") {
            stop_search();
            position_cmd(is);
        } else if (cmd == "go") {
            go_cmd(is);
        } else if (cmd == "stop") {
            stop_search();
        } else if (cmd == "ponderhit") {
            // The pondered move was actually played: stop pondering and let the
            // running search switch to normal time management (the clock has been
            // ticking from the original "go", so its budget applies from here).
            Search::Pondering = false;
        } else if (cmd == "gameover") {
            stop_search();
        } else if (cmd == "quit") {
            stop_search();
            break;
        } else if (cmd == "d") {
            std::cout << pos.to_string() << "sfen: " << pos.sfen() << std::endl;
        } else if (cmd == "bench") {
            run_bench();
        } else if (cmd == "perft") {
            int d = 5; is >> d;
            perft_divide(pos, d);
        }
    }
    stop_search();
}

} // namespace USI

// Position helper defined here (needs USI::move).
std::string Position::move_to_usi(Move m) const { return USI::move(m); }
