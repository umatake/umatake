#include "position.h"
#include "movegen.h"
#include <sstream>
#include <cctype>
#include <algorithm>

// ---- piece <-> SFEN char ----
namespace {
// For output we need per-type letters including king.
char base_letter(PieceType pt) {
    switch (raw_type(pt)) {
        case PAWN: return 'P'; case LANCE: return 'L'; case KNIGHT: return 'N';
        case SILVER: return 'S'; case BISHOP: return 'B'; case ROOK: return 'R';
        case GOLD: return 'G'; case KING: return 'K'; default: return '?';
    }
}
} // namespace

void Position::put_piece(Piece pc, Square s) {
    board[s] = pc;
    PieceType pt = type_of(pc);
    Color c = color_of(pc);
    byType[pt].set(s);
    byColor[c].set(s);
    occupied.set(s);
    if (pt == KING) kingSquare[c] = s;
}

void Position::remove_piece(Square s) {
    Piece pc = board[s];
    PieceType pt = type_of(pc);
    Color c = color_of(pc);
    byType[pt].reset(s);
    byColor[c].reset(s);
    occupied.reset(s);
    board[s] = NO_PIECE;
}

void Position::set(const std::string& sfen, StateInfo* si) {
    for (int i = 0; i < SQ_NB; ++i) board[i] = NO_PIECE;
    for (int i = 0; i < COLOR_NB; ++i) { byColor[i] = Bitboard(); hands[i] = Hand(); }
    for (int i = 0; i < PIECE_TYPE_NB; ++i) byType[i] = Bitboard();
    occupied = Bitboard();
    kingSquare[BLACK] = kingSquare[WHITE] = SQ_NONE;

    *si = StateInfo();
    st = si;

    std::istringstream is(sfen);
    std::string boardStr, turnStr, handStr, plyStr;
    is >> boardStr >> turnStr >> handStr;
    if (!(is >> plyStr)) plyStr = "1";

    // board
    int f = 8, r = 0;
    bool promo = false;
    for (char ch : boardStr) {
        if (ch == '/') { r++; f = 8; promo = false; continue; }
        if (ch == '+') { promo = true; continue; }
        if (std::isdigit((unsigned char)ch)) { f -= (ch - '0'); continue; }
        Color c = std::isupper((unsigned char)ch) ? BLACK : WHITE;
        char up = std::toupper((unsigned char)ch);
        PieceType pt;
        switch (up) {
            case 'P': pt = PAWN; break; case 'L': pt = LANCE; break;
            case 'N': pt = KNIGHT; break; case 'S': pt = SILVER; break;
            case 'B': pt = BISHOP; break; case 'R': pt = ROOK; break;
            case 'G': pt = GOLD; break; case 'K': pt = KING; break;
            default: pt = NO_PIECE_TYPE; break;
        }
        if (promo) pt = promote_type(pt);
        if (pt != NO_PIECE_TYPE && f >= 0 && f <= 8 && r >= 0 && r <= 8)
            put_piece(make_piece(c, pt), make_square(File(f), Rank(r)));
        promo = false;
        f--;
    }

    sideToMove = (turnStr == "w") ? WHITE : BLACK;

    // hand
    if (handStr != "-") {
        int num = 0;
        for (char ch : handStr) {
            if (std::isdigit((unsigned char)ch)) { num = num * 10 + (ch - '0'); continue; }
            Color c = std::isupper((unsigned char)ch) ? BLACK : WHITE;
            char up = std::toupper((unsigned char)ch);
            PieceType pt;
            switch (up) {
                case 'P': pt = PAWN; break; case 'L': pt = LANCE; break;
                case 'N': pt = KNIGHT; break; case 'S': pt = SILVER; break;
                case 'B': pt = BISHOP; break; case 'R': pt = ROOK; break;
                case 'G': pt = GOLD; break; default: pt = NO_PIECE_TYPE; break;
            }
            int add = num ? num : 1;
            for (int i = 0; i < add && pt != NO_PIECE_TYPE; ++i) hands[c].add(pt);
            num = 0;
        }
    }

    gamePly = std::max(1, atoi(plyStr.c_str()));

    // rebuild key + checkers
    Key k = 0;
    for (Square s = SQ_ZERO; s < SQ_NB; ++s)
        if (board[s] != NO_PIECE) k ^= Zobrist::psq[board[s]][s];
    for (Color c : {BLACK, WHITE})
        for (PieceType pt : HAND_TYPES)
            for (int n = hands[c].get(pt); n > 0; --n) k ^= Zobrist::hand[c][pt][n];
    if (sideToMove == WHITE) k ^= Zobrist::side;
    st->key = k;
    st->pliesFromNull = 0;
    st->checkers = attackers_to(kingSquare[sideToMove], occupied) & byColor[~sideToMove];
}

void Position::set_startpos(StateInfo* si) {
    set("lnsgkgsnl/1r5b1/ppppppppp/9/9/9/PPPPPPPPP/1B5R1/LNSGKGSNL b - 1", si);
}

Bitboard Position::attackers_to(Square s, const Bitboard& occ) const {
    Bitboard b;
    // Non-sliding: squares from which a color-c piece attacks s equal the
    // opposite-color step attacks from s.
    b |= StepAttacks[W_PAWN][s]   & pieces(BLACK, PAWN);
    b |= StepAttacks[B_PAWN][s]   & pieces(WHITE, PAWN);
    b |= StepAttacks[W_KNIGHT][s] & pieces(BLACK, KNIGHT);
    b |= StepAttacks[B_KNIGHT][s] & pieces(WHITE, KNIGHT);
    b |= StepAttacks[W_SILVER][s] & pieces(BLACK, SILVER);
    b |= StepAttacks[B_SILVER][s] & pieces(WHITE, SILVER);
    b |= StepAttacks[W_GOLD][s]   & golds(BLACK);
    b |= StepAttacks[B_GOLD][s]   & golds(WHITE);
    b |= StepAttacks[B_KING][s]   & (pieces(BLACK, KING) | pieces(WHITE, KING));
    // horse king-orthogonal & dragon king-diagonal steps (color-symmetric)
    b |= StepAttacks[B_HORSE][s]  & (byType[HORSE]);
    b |= StepAttacks[B_DRAGON][s] & (byType[DRAGON]);
    // lance (color-dependent)
    b |= lance_attacks(WHITE, s, occ) & pieces(BLACK, LANCE);
    b |= lance_attacks(BLACK, s, occ) & pieces(WHITE, LANCE);
    // bishop / horse diagonal sliders
    Bitboard bishops = byType[BISHOP] | byType[HORSE];
    b |= bishop_attacks(s, occ) & bishops;
    // rook / dragon orthogonal sliders
    Bitboard rooks = byType[ROOK] | byType[DRAGON];
    b |= rook_attacks(s, occ) & rooks;
    return b;
}

void Position::do_move(Move m, StateInfo& nst) {
    Color us = sideToMove, them = ~us;
    Square to = move_to(m);
    Key k = st->key ^ Zobrist::side;

    nst.previous = st;
    nst.move = m;
    nst.pliesFromNull = st->pliesFromNull + 1;
    nst.captured = NO_PIECE;

    if (is_drop(m)) {
        PieceType pt = dropped_type(m);
        int cnt = hands[us].get(pt);
        k ^= Zobrist::hand[us][pt][cnt];
        hands[us].sub(pt);
        Piece pc = make_piece(us, pt);
        put_piece(pc, to);
        k ^= Zobrist::psq[pc][to];
    } else {
        Square from = move_from(m);
        Piece pc = board[from];
        Piece captured = board[to];
        if (captured != NO_PIECE) {
            PieceType craw = raw_type(type_of(captured));
            int cnt = hands[us].get(craw);
            k ^= Zobrist::hand[us][craw][cnt + 1];
            hands[us].add(craw);
            k ^= Zobrist::psq[captured][to];
            remove_piece(to);
            nst.captured = captured;
        }
        k ^= Zobrist::psq[pc][from];
        remove_piece(from);
        Piece moved = is_promote(m) ? promote_piece(pc) : pc;
        put_piece(moved, to);
        k ^= Zobrist::psq[moved][to];
    }

    sideToMove = them;
    ++gamePly;
    nst.key = k;
    st = &nst;
    st->checkers = attackers_to(kingSquare[them], occupied) & byColor[us];
}

void Position::undo_move(Move m) {
    sideToMove = ~sideToMove;
    Color us = sideToMove;
    Square to = move_to(m);
    --gamePly;

    if (is_drop(m)) {
        PieceType pt = dropped_type(m);
        remove_piece(to);
        hands[us].add(pt);
    } else {
        Square from = move_from(m);
        Piece moved = board[to];
        PieceType origType = is_promote(m) ? PieceType(type_of(moved) - PROMOTE_BIT)
                                           : type_of(moved);
        remove_piece(to);
        put_piece(make_piece(us, origType), from);
        if (st->captured != NO_PIECE) {
            put_piece(st->captured, to);
            hands[us].sub(raw_type(type_of(st->captured)));
        }
    }
    st = st->previous;
}

void Position::do_null_move(StateInfo& nst) {
    nst = *st;
    nst.previous = st;
    nst.key = st->key ^ Zobrist::side;
    nst.pliesFromNull = 0;
    nst.move = MOVE_NULL;
    nst.captured = NO_PIECE;
    sideToMove = ~sideToMove;
    ++gamePly;
    st = &nst;
    st->checkers = Bitboard(); // null move only allowed when not in check
}

void Position::undo_null_move() {
    sideToMove = ~sideToMove;
    --gamePly;
    st = st->previous;
}

bool Position::legal(Move m, bool checkUchifuzume) {
    Color us = sideToMove, them = ~us;
    Square to = move_to(m);

    if (is_drop(m)) {
        // Adding a friendly piece can only reduce checks on own king.
        if (st->checkers.any()) {
            Bitboard occ = occupied; occ.set(to);
            if ((attackers_to(kingSquare[us], occ) & byColor[them]).any())
                return false;
        }
        // uchifuzume: dropping a pawn that delivers checkmate is illegal.
        if (checkUchifuzume && dropped_type(m) == PAWN) {
            StateInfo tmp;
            do_move(m, tmp);
            bool inCheck = in_check();
            bool escape = inCheck ? has_legal_move() : true;
            undo_move(m);
            if (inCheck && !escape) return false;
        }
        return true;
    }

    Square from = move_from(m);
    Piece pc = board[from];
    Bitboard enemy = byColor[them];
    if (board[to] != NO_PIECE) enemy = enemy ^ square_bb(to); // captured piece removed

    if (type_of(pc) == KING) {
        Bitboard occ = occupied ^ square_bb(from);
        occ.set(to);
        return (attackers_to(to, occ) & enemy).none();
    } else {
        Bitboard occ = occupied ^ square_bb(from);
        occ.set(to);
        return (attackers_to(kingSquare[us], occ) & enemy).none();
    }
}

bool Position::gives_check(Move m) {
    StateInfo tmp;
    do_move(m, tmp);
    bool c = in_check();
    undo_move(m);
    return c;
}

bool Position::has_legal_move() {
    ExtMove list[MAX_MOVES];
    ExtMove* end = generate_all(*this, list);
    for (ExtMove* it = list; it != end; ++it)
        if (legal(it->move, /*checkUchifuzume=*/false)) return true;
    return false;
}

// 入玉 (entering king) declaration under the 27-point rule. The side to move may
// declare an immediate win when ALL of the following hold:
//   1. its king is inside the enemy camp (the promotion zone),
//   2. it is not in check,
//   3. at least 10 of its other pieces are inside the enemy camp, and
//   4. its point total — big pieces (rook/bishop, incl. dragon/horse) = 5, every
//      other piece = 1, counting pieces in the enemy camp plus pieces in hand —
//      is >= 28 for Black (先手) or >= 27 for White (後手).
// The predicate is intentionally strict: an illegal declaration forfeits the game.
bool Position::can_declare_win() const {
    Color us = sideToMove;
    Square ksq = kingSquare[us];
    if (ksq == SQ_NONE) return false;
    if (!PromotionZoneBB[us].test(ksq)) return false; // king must be in the enemy camp
    if (in_check()) return false;

    Bitboard zone = byColor[us] & PromotionZoneBB[us];
    zone.reset(ksq); // exclude the king itself from the count and the points
    if (zone.count() < 10) return false;

    int points = 0;
    for (Bitboard b = zone; b.any(); ) {
        PieceType pt = type_of(board[b.pop_lsb()]);
        points += (pt == ROOK || pt == BISHOP || pt == DRAGON || pt == HORSE) ? 5 : 1;
    }
    points += 5 * (count_in_hand(us, ROOK) + count_in_hand(us, BISHOP));
    points += count_in_hand(us, PAWN) + count_in_hand(us, LANCE)
            + count_in_hand(us, KNIGHT) + count_in_hand(us, SILVER)
            + count_in_hand(us, GOLD);

    return points >= (us == BLACK ? 28 : 27);
}

RepetitionState Position::is_repetition() const {
    int end = st->pliesFromNull;
    if (end < 4) return REP_NONE;

    bool themPerp = st->checkers.any();
    bool usPerp = true;
    StateInfo* stp = st;
    for (int i = 2; i <= end; i += 2) {
        StateInfo* s1 = stp->previous;
        if (!s1 || !s1->previous) break;
        StateInfo* s2 = s1->previous;
        usPerp = usPerp && s1->checkers.any();
        if (s2->key == st->key) {
            if (themPerp) return REP_WIN;  // opponent perpetually checked us -> we win
            if (usPerp)   return REP_LOSE; // we perpetually checked -> we lose
            return REP_DRAW;
        }
        themPerp = themPerp && s2->checkers.any();
        stp = s2;
    }
    return REP_NONE;
}

std::string Position::to_string() const {
    std::ostringstream os;
    os << "  9 8 7 6 5 4 3 2 1\n";
    for (int r = 0; r < 9; ++r) {
        os << char('a' + r) << ' ';
        for (int f = 8; f >= 0; --f) {
            Square s = make_square(File(f), Rank(r));
            Piece pc = board[s];
            if (pc == NO_PIECE) { os << " ."; continue; }
            char c = base_letter(type_of(pc));
            if (color_of(pc) == WHITE) c = std::tolower((unsigned char)c);
            os << (is_promoted(type_of(pc)) ? '+' : ' ') << c;
        }
        os << '\n';
    }
    os << "Turn: " << (sideToMove == BLACK ? "Black" : "White") << "  ";
    auto dumpHand = [&](Color col) {
        for (PieceType pt : HAND_TYPES) {
            int n = hands[col].get(pt);
            if (n) os << base_letter(pt) << n << ' ';
        }
    };
    os << "Hand B: "; dumpHand(BLACK);
    os << " W: "; dumpHand(WHITE);
    os << '\n';
    return os.str();
}

std::string Position::sfen() const {
    std::ostringstream os;
    for (int r = 0; r < 9; ++r) {
        int empty = 0;
        for (int f = 8; f >= 0; --f) {
            Square s = make_square(File(f), Rank(r));
            Piece pc = board[s];
            if (pc == NO_PIECE) { ++empty; continue; }
            if (empty) { os << empty; empty = 0; }
            char c = base_letter(type_of(pc));
            if (color_of(pc) == WHITE) c = std::tolower((unsigned char)c);
            if (is_promoted(type_of(pc))) os << '+';
            os << c;
        }
        if (empty) os << empty;
        if (r < 8) os << '/';
    }
    os << ' ' << (sideToMove == BLACK ? 'b' : 'w') << ' ';
    std::string hs;
    for (Color c : {BLACK, WHITE}) {
        // SFEN hand order: R B G S N L P
        const PieceType order[7] = { ROOK, BISHOP, GOLD, SILVER, KNIGHT, LANCE, PAWN };
        for (PieceType pt : order) {
            int n = hands[c].get(pt);
            if (!n) continue;
            if (n > 1) hs += std::to_string(n);
            char ch = base_letter(pt);
            hs += (c == BLACK) ? ch : char(std::tolower((unsigned char)ch));
        }
    }
    os << (hs.empty() ? "-" : hs) << ' ' << gamePly;
    return os.str();
}
