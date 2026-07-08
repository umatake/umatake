#include "book.h"

#include <map>
#include <random>
#include <sstream>

namespace Book {

namespace {

// Each raw line is a full game opening written in USI move notation, alternating
// BLACK and WHITE. They are deliberately short (≈6 plies) and stick to textbook
// developing moves that transpose into the common castles / rook positions:
//
//   矢倉 / 相居飛車     — double static rook, Yagura-style development
//   四間飛車 (2h6h)     — fourth-file rook
//   中飛車 (2h5h)       — central rook
//
// Every individual move here is a standard, legal opening move; the depth is
// kept modest so the engine's own search takes over well before any real theory
// runs out. Correctness of deep theory is not the goal — giving the engine a
// sound, fast first few moves is.
const char* LINES[] = {
    // Static-rook / Yagura move orders
    "7g7f 8c8d 6g6f 3c3d 6i7h 4c4d",
    "7g7f 3c3d 2g2f 4c4d 3i4h 3a3b",
    "7g7f 8c8d 2g2f 8d8e 8h7g 3c3d",
    "7g7f 4c4d 2g2f 3c3d 5g5f 4a3b",
    "2g2f 3c3d 7g7f 4c4d 3i4h 4a3b",
    "2g2f 8c8d 7g7f 8d8e 8h7g 3c3d",
    "5g5f 3c3d 2g2f 4c4d 3i4h 3a3b",
    // Ranging-rook (振り飛車) systems
    "7g7f 3c3d 6g6f 8c8d 2h6h 4c4d",   // 四間飛車
    "7g7f 3c3d 6g6f 5c5d 2h5h 3a3b",   // 中飛車
    "7g7f 3c3d 2h6h 8c8d 6g6f 4c4d",   // 四間飛車 (early swing)
};

// Map from "moves played so far" (space-joined USI) to the set of book replies.
std::map<std::string, std::vector<std::string>> Table;

std::string join(const std::vector<std::string>& v, size_t n) {
    std::string s;
    for (size_t i = 0; i < n; ++i) {
        if (i) s += ' ';
        s += v[i];
    }
    return s;
}

} // namespace

void init() {
    if (!Table.empty()) return;
    for (const char* raw : LINES) {
        std::istringstream is(raw);
        std::vector<std::string> mv;
        std::string tok;
        while (is >> tok) mv.push_back(tok);

        // For each prefix, register the move that follows it as a candidate reply.
        for (size_t i = 0; i < mv.size(); ++i) {
            std::string key = join(mv, i);
            auto& cands = Table[key];
            bool dup = false;
            for (const auto& c : cands)
                if (c == mv[i]) { dup = true; break; }
            if (!dup) cands.push_back(mv[i]);
        }
    }
}

std::string probe(const std::vector<std::string>& history) {
    auto it = Table.find(join(history, history.size()));
    if (it == Table.end() || it->second.empty()) return "";

    const auto& cands = it->second;
    if (cands.size() == 1) return cands[0];

    // A fresh non-deterministic generator each call keeps repeated games from
    // marching down the same line every time.
    static thread_local std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<size_t> pick(0, cands.size() - 1);
    return cands[pick(rng)];
}

} // namespace Book
