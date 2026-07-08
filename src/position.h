#ifndef UMATAKE_POSITION_H
#define UMATAKE_POSITION_H

#include "types.h"
#include "bitboard.h"
#include "zobrist.h"
#include <string>

enum RepetitionState { REP_NONE, REP_DRAW, REP_WIN, REP_LOSE };

struct StateInfo {
    Key      key = 0;
    Bitboard checkers;
    Piece    captured = NO_PIECE;
    int      pliesFromNull = 0;
    Move     move = MOVE_NONE;
    StateInfo* previous = nullptr;
};

class Position {
public:
    Position() = default;

    void set(const std::string& sfen, StateInfo* si);
    void set_startpos(StateInfo* si);
    std::string sfen() const;
    std::string to_string() const; // human-readable board

    void do_move(Move m, StateInfo& newSt);
    void undo_move(Move m);
    void do_null_move(StateInfo& newSt);
    void undo_null_move();

    // --- accessors ---
    Piece piece_on(Square s) const { return board[s]; }
    bool  empty(Square s) const { return board[s] == NO_PIECE; }
    Color side_to_move() const { return sideToMove; }
    int   game_ply() const { return gamePly; }

    Bitboard pieces() const { return occupied; }
    Bitboard pieces(Color c) const { return byColor[c]; }
    Bitboard pieces(PieceType pt) const { return byType[pt]; }
    Bitboard pieces(Color c, PieceType pt) const { return byColor[c] & byType[pt]; }
    Bitboard golds(Color c) const {
        return byColor[c] & (byType[GOLD] | byType[PRO_PAWN] | byType[PRO_LANCE]
                             | byType[PRO_KNIGHT] | byType[PRO_SILVER]);
    }
    Square king_square(Color c) const { return kingSquare[c]; }
    const Hand& hand_of(Color c) const { return hands[c]; }
    int count_in_hand(Color c, PieceType pt) const { return hands[c].get(pt); }

    Key  key() const { return st->key; }
    Bitboard checkers() const { return st->checkers; }
    bool in_check() const { return st->checkers.any(); }
    StateInfo* state() const { return st; }

    Bitboard attackers_to(Square s, const Bitboard& occ) const;
    Bitboard attackers_to(Square s) const { return attackers_to(s, occupied); }

    bool legal(Move m, bool checkUchifuzume = true);
    bool gives_check(Move m);
    bool has_legal_move();
    bool can_declare_win() const; // 入玉 nyugyoku, 27-point declaration rule

    RepetitionState is_repetition() const;

    // conversion helpers (defined in usi.cpp)
    std::string move_to_usi(Move m) const;

private:
    void put_piece(Piece pc, Square s);
    void remove_piece(Square s);

    Piece    board[SQ_NB];
    Bitboard byColor[COLOR_NB];
    Bitboard byType[PIECE_TYPE_NB];
    Bitboard occupied;
    Hand     hands[COLOR_NB];
    Square   kingSquare[COLOR_NB];
    Color    sideToMove = BLACK;
    int      gamePly = 0;
    StateInfo* st = nullptr;
};

#endif // UMATAKE_POSITION_H
