// Chess against GeistAI. You play white.
// Knob: turn to pick one of your pieces, press, turn to pick the target, press.
// Back undoes the piece choice. Arrow keys move a free cursor instead.
// The engine: legal move generation (castling, en passant, promotion) and a
// small alpha-beta search with piece-square tables - strong enough to be fun.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../assets/chess.h"
#include "draw.h"

namespace games {

void drawChessPiece(Canvas& c, int x, int y, char piece, int size, uint16_t fill, uint16_t line) {
  static const char ORDER[] = "KQRBNP";
  static const uint8_t* const F18[6] = {chess18_K_fill, chess18_Q_fill, chess18_R_fill,
                                        chess18_B_fill, chess18_N_fill, chess18_P_fill};
  static const uint8_t* const L18[6] = {chess18_K_line, chess18_Q_line, chess18_R_line,
                                        chess18_B_line, chess18_N_line, chess18_P_line};
  static const uint8_t* const F14[6] = {chess14_K_fill, chess14_Q_fill, chess14_R_fill,
                                        chess14_B_fill, chess14_N_fill, chess14_P_fill};
  static const uint8_t* const L14[6] = {chess14_K_line, chess14_Q_line, chess14_R_line,
                                        chess14_B_line, chess14_N_line, chess14_P_line};
  const char* p = strchr(ORDER, piece);
  if (!p || !*p) return;
  int i = (int)(p - ORDER);
  bool big = size >= 18;
  int sz = big ? 18 : 14;
  c.drawMask8(x, y, big ? F18[i] : F14[i], sz, sz, fill);
  if (line != fill) c.drawMask8(x, y, big ? L18[i] : L14[i], sz, sz, line);
}

namespace {

// pieces: + white, - black
enum : int8_t { EMPTY = 0, P = 1, N = 2, B = 3, R = 4, Q = 5, K = 6 };
const char LETTER[7] = {' ', 'P', 'N', 'B', 'R', 'Q', 'K'};
const int VALUE[7] = {0, 100, 320, 330, 500, 900, 0};

// piece-square tables from white's point of view, a8 = index 0
const int8_t PST[7][64] = {
    {0},
    {0,  0,  0,  0,  0,  0,  0,  0,  50, 50, 50, 50, 50, 50, 50, 50, 10, 10, 20, 30, 30, 20,
     10, 10, 5,  5,  10, 25, 25, 10, 5,  5,  0,  0,  0,  20, 20, 0,  0,  0,  5,  -5, -10, 0,
     0,  -10, -5, 5,  5,  10, 10, -20, -20, 10, 10, 5,  0,  0,  0,  0,  0,  0,  0,  0},
    {-50, -40, -30, -30, -30, -30, -40, -50, -40, -20, 0,   0,   0,   0,   -20, -40,
     -30, 0,   10,  15,  15,  10,  0,   -30, -30, 5,   15,  20,  20,  15,  5,   -30,
     -30, 0,   15,  20,  20,  15,  0,   -30, -30, 5,   10,  15,  15,  10,  5,   -30,
     -40, -20, 0,   5,   5,   0,   -20, -40, -50, -40, -30, -30, -30, -30, -40, -50},
    {-20, -10, -10, -10, -10, -10, -10, -20, -10, 0,   0,   0,   0,   0,   0,   -10,
     -10, 0,   5,   10,  10,  5,   0,   -10, -10, 5,   5,   10,  10,  5,   5,   -10,
     -10, 0,   10,  10,  10,  10,  0,   -10, -10, 10,  10,  10,  10,  10,  10,  -10,
     -10, 5,   0,   0,   0,   0,   5,   -10, -20, -10, -10, -10, -10, -10, -10, -20},
    {0,  0,  0,  0,  0,  0,  0,  0,  5,  10, 10, 10, 10, 10, 10, 5,  -5, 0, 0, 0, 0, 0,
     0,  -5, -5, 0,  0,  0,  0,  0,  0,  -5, -5, 0,  0,  0,  0,  0,  0,  -5, -5, 0, 0, 0,
     0,  0,  0,  -5, -5, 0,  0,  0,  0,  0,  0,  -5, 0,  0,  0,  5,  5,  0,  0,  0},
    {-20, -10, -10, -5, -5, -10, -10, -20, -10, 0,  0,   0,  0,  0,   0,   -10,
     -10, 0,   5,   5,  5,  5,   0,   -10, -5,  0,  5,   5,  5,  5,   0,   -5,
     0,   0,   5,   5,  5,  5,   0,   -5,  -10, 5,  5,   5,  5,  5,   0,   -10,
     -10, 0,   5,   0,  0,  0,   0,   -10, -20, -10, -10, -5, -5, -10, -10, -20},
    {-30, -40, -40, -50, -50, -40, -40, -30, -30, -40, -40, -50, -50, -40, -40, -30,
     -30, -40, -40, -50, -50, -40, -40, -30, -30, -40, -40, -50, -50, -40, -40, -30,
     -20, -30, -30, -40, -40, -30, -30, -20, -10, -20, -20, -20, -20, -20, -20, -10,
     20,  20,  0,   0,   0,   0,   20,  20,  20,  30,  10,  0,   0,   10,  30,  20},
};

struct Move {
  int8_t from, to, promo;  // promo: piece type or 0
  int8_t flag;             // 1 = castle, 2 = en passant, 3 = double pawn push
};

struct Undo {
  int8_t captured, castle, ep, half;
};

constexpr int MAXMOVES = 128;

class Board {
 public:
  int8_t sq[64];
  int side = 1;       // 1 white, -1 black
  int8_t castle = 15; // 1 K, 2 Q, 4 k, 8 q
  int8_t ep = -1;     // en-passant target square
  int8_t half = 0;    // moves since the last capture or pawn move
  bool underpromote = false;

  void start() {
    static const int8_t back[8] = {R, N, B, Q, K, B, N, R};
    memset(sq, 0, sizeof(sq));
    for (int i = 0; i < 8; i++) {
      sq[i] = -back[i];
      sq[8 + i] = -P;
      sq[48 + i] = P;
      sq[56 + i] = back[i];
    }
    side = 1;
    castle = 15;
    ep = -1;
    half = 0;
  }

  // minimal FEN reader (tests only): pieces, side, castling, en passant
  void fen(const char* f) {
    memset(sq, 0, sizeof(sq));
    int i = 0;
    for (; *f && *f != ' '; f++) {
      if (*f == '/') continue;
      if (*f >= '1' && *f <= '8') {
        i += *f - '0';
        continue;
      }
      const char* t = strchr("pnbrqk", *f | 32);
      if (t) sq[i++] = (int8_t)(((t - "pnbrqk") + 1) * (*f < 'a' ? 1 : -1));
    }
    side = f[1] == 'w' ? 1 : -1;
    f += 3;
    castle = 0;
    for (; *f && *f != ' '; f++) {
      if (*f == 'K') castle |= 1;
      if (*f == 'Q') castle |= 2;
      if (*f == 'k') castle |= 4;
      if (*f == 'q') castle |= 8;
    }
    ep = -1;
    if (*f == ' ' && f[1] != '-' && f[1]) ep = (int8_t)((8 - (f[2] - '0')) * 8 + (f[1] - 'a'));
    half = 0;
  }

  int king(int s) const {
    for (int i = 0; i < 64; i++)
      if (sq[i] == K * s) return i;
    return -1;
  }

  // is square `t` attacked by side `by`?
  bool attacked(int t, int by) const {
    int tr = t / 8, tc = t % 8;
    // pawns
    int pr = tr + (by == 1 ? 1 : -1);
    if (pr >= 0 && pr < 8) {
      if (tc > 0 && sq[pr * 8 + tc - 1] == P * by) return true;
      if (tc < 7 && sq[pr * 8 + tc + 1] == P * by) return true;
    }
    static const int KN[8][2] = {{1, 2}, {2, 1}, {-1, 2}, {-2, 1}, {1, -2}, {2, -1}, {-1, -2}, {-2, -1}};
    for (auto& d : KN) {
      int r = tr + d[0], c = tc + d[1];
      if (r >= 0 && r < 8 && c >= 0 && c < 8 && sq[r * 8 + c] == N * by) return true;
    }
    static const int DIR[8][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
    for (int k = 0; k < 8; k++) {
      int r = tr + DIR[k][0], c = tc + DIR[k][1], dist = 1;
      while (r >= 0 && r < 8 && c >= 0 && c < 8) {
        int v = sq[r * 8 + c];
        if (v) {
          if (v * by > 0) {
            int t2 = abs(v);
            if (dist == 1 && t2 == K) return true;
            if (t2 == Q) return true;
            if (k < 4 && t2 == R) return true;
            if (k >= 4 && t2 == B) return true;
          }
          break;
        }
        r += DIR[k][0];
        c += DIR[k][1];
        dist++;
      }
    }
    return false;
  }

  bool inCheck(int s) const {
    int k = king(s);
    return k >= 0 && attacked(k, -s);
  }

  int pseudo(Move* out, bool capturesOnly = false) const {
    int n = 0;
    auto add = [&](int f, int t, int flag = 0) {
      if (capturesOnly && !sq[t] && flag != 2) return;
      int pr = abs(sq[f]) == P && (t < 8 || t >= 56) ? Q : 0;
      out[n++] = Move{(int8_t)f, (int8_t)t, (int8_t)pr, (int8_t)flag};
      if (pr && underpromote)  // the game always takes a queen; perft counts all four
        for (int8_t u : {(int8_t)R, (int8_t)B, (int8_t)N})
          out[n++] = Move{(int8_t)f, (int8_t)t, u, (int8_t)flag};
    };
    for (int f = 0; f < 64; f++) {
      int v = sq[f];
      if (v * side <= 0) continue;
      int t = abs(v), r = f / 8, c = f % 8;
      if (t == P) {
        int dir = side == 1 ? -8 : 8, startRow = side == 1 ? 6 : 1;
        int one = f + dir;
        if (one >= 0 && one < 64 && !sq[one]) {
          add(f, one);
          if (r == startRow && !sq[one + dir]) add(f, one + dir, 3);
        }
        for (int dc : {-1, 1}) {
          if (c + dc < 0 || c + dc > 7 || one < 0 || one >= 64) continue;
          int tt = one + dc;
          if (sq[tt] * side < 0) add(f, tt);
          else if (tt == ep) add(f, tt, 2);
        }
      } else if (t == N || t == K) {
        static const int KN[8][2] = {{1, 2}, {2, 1}, {-1, 2}, {-2, 1}, {1, -2}, {2, -1}, {-1, -2}, {-2, -1}};
        static const int KG[8][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
        auto& D = t == N ? KN : KG;
        for (auto& d : D) {
          int rr = r + d[0], cc = c + d[1];
          if (rr < 0 || rr > 7 || cc < 0 || cc > 7) continue;
          int tt = rr * 8 + cc;
          if (sq[tt] * side <= 0) add(f, tt);
        }
        if (t == K && !capturesOnly) {  // castling
          int home = side == 1 ? 60 : 4;
          if (f == home && !attacked(f, -side)) {
            int ks = side == 1 ? 1 : 4, qs = side == 1 ? 2 : 8;
            if ((castle & ks) && !sq[f + 1] && !sq[f + 2] && sq[f + 3] == R * side &&
                !attacked(f + 1, -side) && !attacked(f + 2, -side))
              add(f, f + 2, 1);
            if ((castle & qs) && !sq[f - 1] && !sq[f - 2] && !sq[f - 3] && sq[f - 4] == R * side &&
                !attacked(f - 1, -side) && !attacked(f - 2, -side))
              add(f, f - 2, 1);
          }
        }
      } else {
        static const int DIR[8][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
        int k0 = t == B ? 4 : 0, k1 = t == R ? 4 : 8;
        for (int k = k0; k < k1; k++) {
          int rr = r + DIR[k][0], cc = c + DIR[k][1];
          while (rr >= 0 && rr < 8 && cc >= 0 && cc < 8) {
            int tt = rr * 8 + cc;
            if (sq[tt] * side > 0) break;
            add(f, tt);
            if (sq[tt]) break;
            rr += DIR[k][0];
            cc += DIR[k][1];
          }
        }
      }
    }
    return n;
  }

  Undo make(const Move& m) {
    Undo u{sq[m.to], castle, ep, half};
    int v = sq[m.from];
    if (m.flag == 2) {  // en passant: the pawn behind the target
      int cap = m.to + (side == 1 ? 8 : -8);
      u.captured = sq[cap];
      sq[cap] = 0;
    }
    sq[m.to] = m.promo ? (int8_t)(m.promo * side) : (int8_t)v;
    sq[m.from] = 0;
    if (m.flag == 1) {  // move the rook too
      if (m.to > m.from) {
        sq[m.to - 1] = sq[m.to + 1];
        sq[m.to + 1] = 0;
      } else {
        sq[m.to + 1] = sq[m.to - 2];
        sq[m.to - 2] = 0;
      }
    }
    ep = m.flag == 3 ? (int8_t)((m.from + m.to) / 2) : (int8_t)-1;
    half = (abs(v) == P || u.captured) ? 0 : half + 1;
    // castling rights disappear when king or rooks move / are taken
    auto clear = [&](int s) {
      if (s == 60) castle &= ~3;
      if (s == 4) castle &= ~12;
      if (s == 63) castle &= ~1;
      if (s == 56) castle &= ~2;
      if (s == 7) castle &= ~4;
      if (s == 0) castle &= ~8;
    };
    clear(m.from);
    clear(m.to);
    side = -side;
    return u;
  }

  void unmake(const Move& m, const Undo& u) {
    side = -side;
    int v = sq[m.to];
    sq[m.from] = m.promo ? (int8_t)(P * side) : (int8_t)v;
    sq[m.to] = m.flag == 2 ? 0 : u.captured;
    if (m.flag == 2) sq[m.to + (side == 1 ? 8 : -8)] = u.captured;
    if (m.flag == 1) {
      if (m.to > m.from) {
        sq[m.to + 1] = sq[m.to - 1];
        sq[m.to - 1] = 0;
      } else {
        sq[m.to - 2] = sq[m.to + 1];
        sq[m.to + 1] = 0;
      }
    }
    castle = u.castle;
    ep = u.ep;
    half = u.half;
  }

  // legal moves, filtered in place (keeps the stack small during the search)
  int legal(Move* out) {
    int n = pseudo(out), k = 0;
    for (int i = 0; i < n; i++) {
      int me = side;
      Move m = out[i];
      Undo u = make(m);
      if (!inCheck(me)) out[k++] = m;
      unmake(m, u);
    }
    return k;
  }

  // + good for the side to move
  int eval() const {
    int s = 0;
    for (int i = 0; i < 64; i++) {
      int v = sq[i];
      if (!v) continue;
      int t = abs(v);
      int pst = v > 0 ? PST[t][i] : PST[t][(7 - i / 8) * 8 + i % 8];
      s += (v > 0 ? 1 : -1) * (VALUE[t] + pst);
    }
    return s * side;
  }
};

// ------------------------------------------------------------ search
class Engine {
 public:
  explicit Engine(Board& b) : b_(b) {}

  Move best(int depth, uint32_t nodeLimit) {
    nodes_ = 0;
    limit_ = nodeLimit;
    Move moves[MAXMOVES];
    int n = b_.legal(moves);
    order(moves, n);
    Move bestMove = moves[0];
    for (int d = 1; d <= depth; d++) {  // iterative deepening
      int bestScore = -1000000;
      Move bestThis = bestMove;
      // search the previous best first
      for (int i = 0; i < n; i++)
        if (moves[i].from == bestMove.from && moves[i].to == bestMove.to) {
          Move t = moves[0];
          moves[0] = moves[i];
          moves[i] = t;
        }
      bool aborted = false;
      for (int i = 0; i < n; i++) {
        Undo u = b_.make(moves[i]);
        int v = -search(d - 1, -1000000, -bestScore, 1);
        b_.unmake(moves[i], u);
        v += rng().range(7) - 3;  // a pinch of variety between equal moves
        if (v > bestScore) {
          bestScore = v;
          bestThis = moves[i];
        }
        if (nodes_ > limit_) {
          aborted = true;
          break;
        }
      }
      if (!aborted || d == 1) bestMove = bestThis;
      if (aborted) break;
    }
    return bestMove;
  }

 private:
  int search(int depth, int alpha, int beta, int ply) {
    nodes_++;
    if (depth <= 0) return quiesce(alpha, beta, 0);
    Move moves[MAXMOVES];
    int n = b_.legal(moves);
    if (n == 0) return b_.inCheck(b_.side) ? -100000 + ply : 0;
    if (b_.half >= 100) return 0;
    order(moves, n);
    for (int i = 0; i < n; i++) {
      Undo u = b_.make(moves[i]);
      int v = -search(depth - 1, -beta, -alpha, ply + 1);
      b_.unmake(moves[i], u);
      if (v >= beta) return beta;
      if (v > alpha) alpha = v;
      if (nodes_ > limit_) break;
    }
    return alpha;
  }

  int quiesce(int alpha, int beta, int qd) {
    nodes_++;
    int stand = b_.eval();
    if (stand >= beta) return beta;
    if (stand > alpha) alpha = stand;
    if (qd >= 4 || nodes_ > limit_) return alpha;
    Move moves[MAXMOVES];  // captures only, but promotions/en passant can add a few
    int n = b_.pseudo(moves, true);
    order(moves, n);
    for (int i = 0; i < n; i++) {
      int me = b_.side;
      Undo u = b_.make(moves[i]);
      if (b_.inCheck(me)) {
        b_.unmake(moves[i], u);
        continue;
      }
      int v = -quiesce(-beta, -alpha, qd + 1);
      b_.unmake(moves[i], u);
      if (v >= beta) return beta;
      if (v > alpha) alpha = v;
    }
    return alpha;
  }

  // captures first: most valuable victim, least valuable attacker
  void order(Move* m, int n) {
    int key[MAXMOVES];
    for (int i = 0; i < n; i++) {
      int victim = abs(b_.sq[m[i].to]), attacker = abs(b_.sq[m[i].from]);
      key[i] = victim ? VALUE[victim] * 10 - VALUE[attacker] / 10 : 0;
      if (m[i].promo) key[i] += 8000;
    }
    for (int i = 1; i < n; i++)  // insertion sort, n is small
      for (int j = i; j > 0 && key[j] > key[j - 1]; j--) {
        int tk = key[j];
        key[j] = key[j - 1];
        key[j - 1] = tk;
        Move tm = m[j];
        m[j] = m[j - 1];
        m[j - 1] = tm;
      }
  }

  Board& b_;
  uint32_t nodes_ = 0, limit_ = 0;
};

// ------------------------------------------------------------ the game
class Chess : public Game {
 public:
  geist::Str name() const override { return geist::S_G_CHESS; }
  geist::Str hint() const override { return geist::S_H_CHESS; }
  bool hasScore() const override { return false; }

  void reset(uint32_t) override {
    b_.start();
    over = won = false;
    result = geist::S_DRAW;
    sel_ = -1;
    lastFrom_ = lastTo_ = -1;
    aiAt_ = 0;
    refresh();
    cursor_ = choices_[0];
  }

  void key(Key k, uint32_t now) override {
    if (aiAt_ || over) return;
    switch (k) {
      case Key::RotR: step(1); break;
      case Key::RotL: step(-1); break;
      case Key::Up: if (cursor_ >= 8) cursor_ -= 8; break;
      case Key::Down: if (cursor_ < 56) cursor_ += 8; break;
      case Key::Left: if (cursor_ % 8) cursor_--; break;
      case Key::Right: if (cursor_ % 8 < 7) cursor_++; break;
      case Key::Press: press(now); break;
      default: break;
    }
  }

  bool back() override {
    if (sel_ < 0) return false;
    cursor_ = sel_;
    sel_ = -1;
    refresh();
    return true;
  }

  void tick(uint32_t now) override {
    if (!aiAt_ || now < aiAt_) return;
    aiAt_ = 0;
    Engine e(b_);
    Move m = e.best(4, 40000);  // about half a second on the ESP32
    lastFrom_ = m.from;
    lastTo_ = m.to;
    b_.make(m);
    if (!checkEnd()) {
      refresh();
      if (choices_[0] >= 0) cursor_ = nearestChoice(cursor_);
    }
  }

  void draw(Canvas& c, const Area& a, uint32_t now) override {
    Palette p = palette();
    using namespace geist::theme;
    int s = a.h >= 150 ? 18 : 14;
    int bs = 8 * s;
    int ox = a.x + 2, oy = a.y + (a.h - bs) / 2;
    uint16_t light = geist::mix(BG_TOP, SNOW, 150), dark = geist::mix(BG_BOTTOM, INK, 110);
    c.fillRoundRect(ox - 2, oy - 2, bs + 4, bs + 4, 4, INK);
    for (int i = 0; i < 64; i++) {
      int x = ox + (i % 8) * s, y = oy + (i / 8) * s;
      bool lightSq = ((i / 8) + (i % 8)) % 2 == 0;
      c.fillRect(x, y, s, s, lightSq ? light : dark);
      if (i == lastFrom_ || i == lastTo_) c.blendRect(x, y, s, s, geist::rgb(255, 214, 64), 110);
      if (i == sel_) c.blendRect(x, y, s, s, geist::rgb(80, 220, 120), 150);
      bool target = sel_ >= 0 && isChoice(i);
      if (target) c.fillCircle(x + s / 2.f, y + s / 2.f, s * (b_.sq[i] ? 0.45f : 0.16f),
                               geist::rgb(40, 160, 90), b_.sq[i] ? 90 : 200);
      int v = b_.sq[i];
      if (v) {
        bool white = v > 0;
        drawChessPiece(c, x + (s - (s >= 18 ? 18 : 14)) / 2, y, LETTER[abs(v)], s,
                       white ? SNOW : geist::rgb(28, 28, 32),
                       white ? geist::rgb(20, 20, 24) : geist::rgb(200, 200, 205));
      }
      if (i == cursor_ && !over && !aiAt_) {
        uint8_t al = (now / 300) % 2 ? 255 : 160;
        uint16_t cc = geist::rgb(255, 60, 120);
        c.fillRect(x, y, s, 2, cc, al);
        c.fillRect(x, y + s - 2, s, 2, cc, al);
        c.fillRect(x, y, 2, s, cc, al);
        c.fillRect(x + s - 2, y, 2, s, cc, al);
      }
    }
    // status next to the board
    int sx = ox + bs + 10, sw = a.x + a.w - sx - 2;
    Area side{sx, oy - 2, sw, bs + 4};
    drawPanel(c, side);
    const geist::Font& f = geist::fontBody();
    uint8_t L = lang();
    const char* status = aiAt_ ? geist::tr(L, geist::S_THINKING)
                               : geist::tr(L, geist::S_YOUR_MOVE);
    static geist::Line lines[3];
    int nl = geist::wrapText(f, status, sw - 10, lines, 3);
    for (int i = 0; i < nl; i++)
      c.drawText(f, sx + 6, side.y + 6 + i * f.lineHeight, status + lines[i].start, p.text, 255,
                 lines[i].len);
    if (!aiAt_ && b_.inCheck(1) && !over)
      c.drawText(f, sx + 6, side.y + 8 + nl * f.lineHeight, geist::tr(L, geist::S_CHECK),
                 p.danger);
    // GeistAI's ghost thinking in the corner
    float bob = aiAt_ ? (now % 600) / 600.f * 3 : 0;
    drawGhost(c, sx + sw - 16, side.y + side.h - 18 - bob, 9, SNOW, INK, 0);
    // material balance
    int bal = 0;
    for (int i = 0; i < 64; i++)
      if (b_.sq[i]) bal += (b_.sq[i] > 0 ? 1 : -1) * VALUE[abs(b_.sq[i])];
    if (bal) {
      char buf[12];
      snprintf(buf, sizeof(buf), "%+d", bal / 100);
      c.drawText(geist::fontSmall(), sx + 6, side.y + side.h - 16, buf, p.soft);
    }
  }

 private:
  // choices: own pieces with moves (no selection) or target squares (selection)
  void refresh() {
    nm_ = b_.legal(moves_);
    nc_ = 0;
    for (int i = 0; i < 64; i++) choices_[i] = -1;
    if (sel_ < 0) {
      for (int sq = 0; sq < 64; sq++) {
        for (int m = 0; m < nm_; m++)
          if (moves_[m].from == sq) {
            choices_[nc_++] = sq;
            break;
          }
      }
    } else {
      for (int m = 0; m < nm_; m++)
        if (moves_[m].from == sel_) choices_[nc_++] = moves_[m].to;
    }
  }

  bool isChoice(int sq) const {
    for (int i = 0; i < nc_; i++)
      if (choices_[i] == sq) return true;
    return false;
  }

  int nearestChoice(int from) const {
    int best = choices_[0], bd = 99;
    for (int i = 0; i < nc_; i++) {
      int d = abs(choices_[i] / 8 - from / 8) + abs(choices_[i] % 8 - from % 8);
      if (d < bd) bd = d, best = choices_[i];
    }
    return best;
  }

  void step(int d) {
    if (!nc_) return;
    int idx = 0;
    for (int i = 0; i < nc_; i++)
      if (choices_[i] == cursor_) idx = i;
    if (!isChoice(cursor_)) {
      cursor_ = nearestChoice(cursor_);
      return;
    }
    cursor_ = choices_[(idx + d + nc_) % nc_];
  }

  void press(uint32_t now) {
    if (sel_ < 0) {
      if (!isChoice(cursor_)) return;
      sel_ = cursor_;
      refresh();
      cursor_ = choices_[0];
      return;
    }
    if (cursor_ == sel_) {  // pressing the piece again puts it back
      back();
      return;
    }
    for (int m = 0; m < nm_; m++) {
      if (moves_[m].from == sel_ && moves_[m].to == cursor_) {
        lastFrom_ = sel_;
        lastTo_ = cursor_;
        b_.make(moves_[m]);
        sel_ = -1;
        if (!checkEnd()) aiAt_ = now + 60;  // draw "thinking" first, then search
        return;
      }
    }
    // pressed another own piece: switch to it
    if (b_.sq[cursor_] > 0) {
      sel_ = -1;
      refresh();
      if (isChoice(cursor_)) {
        sel_ = cursor_;
        refresh();
        cursor_ = choices_[0];
      }
    }
  }

  bool checkEnd() {
    Move tmp[MAXMOVES];
    int n = b_.legal(tmp);
    bool onlyKings = true;
    for (int i = 0; i < 64; i++)
      if (b_.sq[i] && abs(b_.sq[i]) != K) onlyKings = false;
    if (n && !onlyKings && b_.half < 100) return false;
    over = true;
    if (n == 0 && b_.inCheck(b_.side)) {
      won = b_.side == -1;  // black to move and mated
      result = won ? geist::S_YOU_WIN : geist::S_YOU_LOSE;
    } else {
      result = geist::S_DRAW;
    }
    return true;
  }

  Board b_;
  Move moves_[MAXMOVES];
  int nm_ = 0;
  int choices_[64], nc_ = 0;
  int cursor_ = 52, sel_ = -1, lastFrom_ = -1, lastTo_ = -1;
  uint32_t aiAt_ = 0;
};

}  // namespace

Game* makeChess() { return new Chess(); }

// Test hooks (used by tools/sim/games_test.cpp)
int depthForTest = 3;
static long perftRec(Board& b, int d) {
  Move m[MAXMOVES];
  int n = b.legal(m);
  if (d == 1) return n;
  long t = 0;
  for (int i = 0; i < n; i++) {
    Undo u = b.make(m[i]);
    t += perftRec(b, d - 1);
    b.unmake(m[i], u);
  }
  return t;
}

long chessPerft(const char* fen, int depth) {
  Board b;
  if (fen) b.fen(fen);
  else b.start();
  b.underpromote = true;
  return perftRec(b, depth);
}

// Plays the engine against itself; returns the number of half-moves played.
int chessSelfPlay(int maxPlies, uint32_t nodes) {
  Board b;
  b.start();
  int ply = 0;
  for (; ply < maxPlies; ply++) {
    Move m[MAXMOVES];
    if (!b.legal(m)) break;
    Engine e(b);
    Move best = e.best(depthForTest, nodes);
    int8_t before[64];
    memcpy(before, b.sq, 64);
    b.make(best);
  }
  return ply;
}

}  // namespace games
