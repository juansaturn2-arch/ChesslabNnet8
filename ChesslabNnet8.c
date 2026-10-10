/*
 * ChesslabNnet8 -- K7 ChessLab multi-brain engine
 * 10x12 Mailbox + K7CustomNetV2 search + UCI, single file C
 *
 * Built from K7CustomNetV2 (search, K7NN nets) + ChesslabHB7 (Homebrew eval,
 * its weights, search margins, speed and log options).
 *
 * BRAINS  ("Brain" drop-down list)
 *   Homebrew   HB7's tunable hand-written eval (all its UCI weights)
 *   21k 22k 23k 24k 25k 31k   the six K7 nets, built into the exe (default 31k)
 *   NetFile    any other K7NN net (.k7nn / .bin), set with NetFile
 *   (numbers 0-7 work too: 0 Homebrew, 1-6 the nets, 7 NetFile)
 *   Type "brains" in the console to list them with their sizes.
 *
 * BRAIN AGAINST BRAIN IN ARENA (one exe):
 *   tick "SelfPlay" and play ChesslabNnet8 against ChesslabNnet8.
 *   White then plays with "Brain", Black with "SelfPlayOpponent"
 *   (swap the two lists to swap colours). Each move starts with
 *   "info string playing with Brain ..." so you can see it in Arena.
 *   Optional: a copy named ChesslabNnet8_Homebrew.exe (or _25k, ...)
 *   starts on that brain and shows it in its engine name.
 *
 * BUILT-IN SELF-PLAY without a GUI (Brain A = "Brain", Brain B = "SelfPlayOpponent")
 *   In the console:   selfplay [A] [B] [games] [ms per move]
 *                     e.g.  selfplay 31k Homebrew 10 1000
 *   or from Windows:  ChesslabNnet8.exe selfplay 31k Homebrew 10 1000
 *   (SelfPlay.bat asks for the values). Colours alternate every game, the
 *   book gives the openings, games go to SelfPlayPGN. Type "stop" to end.
 *
 * CONSOLE COMMANDS: brains | eval | book | weights | bench [d] | perft n | selfplay | help
 *
 * BUILD  (MSYS2 MinGW64, inside D:\CLCoreGCC\ChesslabNet8 next to the nets
 *         and k7Opennings.bin -- or just run build_nnet8.bat there):
 *   gcc -O3 -march=native -static -o ChesslabNnet8.exe ChesslabNnet8.c
 * Load nets/book from files next to the exe instead of building them in:
 *   add -DN8_NO_EMBED      (no book at all: -DN8_NO_BOOK)
 * Verify : ChesslabNnet8.exe   then type:  perft 5   (expect 4865609)
 */

#ifndef __USE_MINGW_ANSI_STDIO
#  define __USE_MINGW_ANSI_STDIO 1     /* C99 printf (%lld) on MinGW */
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <limits.h>
#include <stdarg.h>
#include <math.h>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#else
#  include <sys/select.h>
#  include <sys/time.h>
#  include <unistd.h>
#endif

#define MAX_PLY         64
#define MAX_MOVES       256
#define INF_SCORE       100000
#define MATE_SCORE      90000
#define TT_DEFAULT_MB   256
#define TT_MAX_MB       4096
#define SCORE_NONE      (-INT_MAX)
#define HIST_SIZE       2048
#define START_FEN       "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"

#define TT_EXACT 0
#define TT_ALPHA 1
#define TT_BETA  2

#define QUIET            0
#define DOUBLE_PUSH      1
#define KING_CASTLE      2
#define QUEEN_CASTLE     3
#define CAPTURE          4
#define EP_CAPTURE       5
#define KNIGHT_PROMO     8
#define BISHOP_PROMO     9
#define ROOK_PROMO      10
#define QUEEN_PROMO     11
#define KNIGHT_PROMO_CAP 12
#define BISHOP_PROMO_CAP 13
#define ROOK_PROMO_CAP   14
#define QUEEN_PROMO_CAP  15

#define IS_PROMO(t)   ((t) >= KNIGHT_PROMO)
#define IS_CAP(t)     ((t) == CAPTURE || (t) == EP_CAPTURE || (t) >= KNIGHT_PROMO_CAP)

/* 32-bit move: (from:7 | to:7 | type:4) */
typedef unsigned int MOVE;
typedef unsigned long long U64;

#define MV(f,t,ty)  (((MOVE)(f) << 11) | ((MOVE)(t) << 4) | (MOVE)(ty))
#define MV_FROM(m)  (((m) >> 11) & 0x7F)
#define MV_TO(m)    (((m) >> 4)  & 0x7F)
#define MV_TYPE(m)  ((m) & 0x0F)

enum {
    A1=21, B1, C1, D1, E1, F1, G1, H1,
    A2=31, B2, C2, D2, E2, F2, G2, H2,
    A3=41, B3, C3, D3, E3, F3, G3, H3,
    A4=51, B4, C4, D4, E4, F4, G4, H4,
    A5=61, B5, C5, D5, E5, F5, G5, H5,
    A6=71, B6, C6, D6, E6, F6, G6, H6,
    A7=81, B7, C7, D7, E7, F7, G7, H7,
    A8=91, B8, C8, D8, E8, F8, G8, H8
};

enum { UP = 10, DOWN = -10 };
enum { WKCA=8, WQCA=4, BKCA=2, BQCA=1 };
enum { WHITE, BLACK };
enum { EMPTY, wP, wN, wB, wR, wQ, wK, bP, bN, bB, bR, bQ, bK };
enum { PAWN = 1, KNIGHT, BISHOP, ROOK, QUEEN, KING };   /* base types */

#define IS_BLACK(p) ((p) > 6)
#define COLOR_OF(p) (IS_BLACK(p) ? BLACK : WHITE)
#define BASE(p)     (IS_BLACK(p) ? (p) - 6 : (p))
#define MAKE_PC(side, base) ((base) + ((side) == BLACK ? 6 : 0))

typedef struct {
    MOVE move;
    int  enPas;
    int  castlePermission;
    int  capturedPiece;
    int  fiftyMove;
    U64  hash;
    /* net feature delta of this move (kings included) */
    signed char   nRem, nAdd;
    unsigned char remP[2], remS[2], addP[2], addS[2];
} MOVE_UNDO;

typedef struct {
    int board[120];
    int hisPly;          /* total plies from game start -- indexes history[] */
    int stm;
    int enPas;
    int castlePermission;
    int fiftyMove;
    int kingSquare[2];
    int pcount[13];      /* number of each piece type on the board */
    U64 hash;
    MOVE_UNDO history[HIST_SIZE];
} BOARD;

typedef struct {
    U64  key;
    int  depth;
    int  score;
    int  flag;
    MOVE bestMove;
} TT_ENTRY;

typedef struct {
    int    depth;
    long long nodes;
    long long nodeLimit;             /* NodeLimit / go nodes, 0 = none */
    MOVE   killers[MAX_PLY][2];
    int    history[13][120];
    MOVE   pv[MAX_PLY][MAX_PLY];
    int    pvLen[MAX_PLY];
    clock_t startTime;
    int    timeLimit;
    int    stopped;
    int    evalStack[MAX_PLY + 4];   /* static eval per ply ("improving") */
    MOVE   counter[13][120];         /* reply that refuted [piece][to]    */
} SEARCH_INFO;

static const char pieceChar[] = ".PNBRQKpnbrqk";
static const int  pieceValue[] = {0,100,320,330,500,900,20000,100,320,330,500,900,20000};

static const int knightDirs[8] = {-21,-19,-12,-8,8,12,19,21};
static const int bishopDirs[4] = {-11,-9,9,11};
static const int rookDirs[4]   = {-10,-1,1,10};
static const int kingDirs[8]   = {-11,-10,-9,-1,1,9,10,11};
static const int queenDirs[8]  = {-10,-1,1,10,-11,-9,9,11};

static BOARD*    gboard = NULL;
static TT_ENTRY* tt      = NULL;
static U64       ttMask  = 0;   /* entries-1, always a power of two */
static U64       ttCount = 0;

static U64 zobristPiece[13][120];
static U64 zobristCastle[16];
static U64 zobristEP[120];
static U64 zobristSide;
static U64 rand64_state = 0x9E3779B97F4A7C15ULL;

/* ================================================================
 *  OUTPUT + LOGGING  (from ChesslabHB7)
 *  Every line sent to the GUI goes through say(), so the log sees
 *  exactly what the GUI sees.
 * ================================================================ */

static double nowMs(void) {
#ifdef _WIN32
    return (double)GetTickCount64();
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000.0 + tv.tv_usec / 1000.0;
#endif
}

static FILE*  g_log = NULL;
static int    g_logOn = 0;
static char   g_logPath[1024] = "ChesslabNnet8.log";
static double g_t0 = 0.0;

static void logOpen(void) {
    if (g_log) return;
    g_log = fopen(g_logPath, "a");
    if (g_log) {
        time_t now = time(NULL);
        fprintf(g_log, "\n===== ChesslabNnet8 log opened %s", ctime(&now));
        fflush(g_log);
    }
}

static void logClose(void) {
    if (g_log) { fclose(g_log); g_log = NULL; }
}

/* GUIs send options in menu order, so "Log true" can arrive before
   "LogFile". While setoption commands stream in, lines are held in memory;
   the file opens at the first other command, when LogFile is final. */
#define LOG_PENDING_MAX 256
static char* g_logPending[LOG_PENDING_MAX];
static int   g_logPendingN = 0;
static int   g_logReady = 0;

static void logLine(const char* dir, const char* text) {
    char buf[8300];
    if (!g_logOn) return;
    snprintf(buf, sizeof(buf), "[%10.3f] %s %s", (nowMs() - g_t0) / 1000.0, dir, text);
    if (!g_log && !g_logReady) {
        if (g_logPendingN < LOG_PENDING_MAX) {
            size_t n = strlen(buf) + 1;
            char* c = (char*)malloc(n);
            if (c) { memcpy(c, buf, n); g_logPending[g_logPendingN++] = c; }
        }
        return;
    }
    if (!g_log) logOpen();
    if (!g_log) return;
    if (g_logPendingN) {
        int i;
        for (i = 0; i < g_logPendingN; i++) { fprintf(g_log, "%s\n", g_logPending[i]); free(g_logPending[i]); }
        g_logPendingN = 0;
    }
    fprintf(g_log, "%s\n", buf);
    fflush(g_log);
}

static void setLogging(int on) {
    g_logOn = on;
    if (!on) {
        int i;
        logClose();
        for (i = 0; i < g_logPendingN; i++) free(g_logPending[i]);
        g_logPendingN = 0;
    }
}

/* printf to stdout, mirrored line by line to the log as "<<". */
static void say(const char* fmt, ...) {
    char buf[8192];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    fputs(buf, stdout);
    fflush(stdout);
    if (g_logOn) {
        char* p = buf;
        while (*p) {
            char* nl = strchr(p, '\n');
            if (nl) *nl = 0;
            if (*p) logLine("<<", p);
            if (!nl) break;
            p = nl + 1;
        }
    }
}

/* ------------------------------------------- K7 net accumulator flags */

static char g_accComp[HIST_SIZE + 1][2];   /* is ply's accumulator valid? */

static void accInvalidate(int ply) {
    if (ply >= 0 && ply <= HIST_SIZE) g_accComp[ply][0] = g_accComp[ply][1] = 0;
}

/* ------------------------------------------------------------------ utils */

static U64 rand64(void) {
    rand64_state ^= rand64_state << 13;
    rand64_state ^= rand64_state >> 7;
    rand64_state ^= rand64_state << 17;
    return rand64_state;
}

static void initZobrist(void) {
    int i, sq;
    for (i = 0; i < 13; i++) for (sq = 0; sq < 120; sq++) zobristPiece[i][sq] = rand64();
    for (i = 0; i < 16; i++) zobristCastle[i] = rand64();
    for (sq = 0; sq < 120; sq++) zobristEP[sq] = rand64();
    zobristSide = rand64();
}

static int sq_valid(int sq) {
    int file;
    if (sq < 21 || sq > 98) return 0;
    file = sq % 10;
    return file >= 1 && file <= 8;
}

static int sq_rank(int sq) { return (sq / 10) - 2; }
static int sq_file(int sq) { return (sq % 10) - 1; }
static int sq_to64(int sq) { return sq_rank(sq) * 8 + sq_file(sq); }

static void sq_to_str(int sq, char* str) {
    if (!sq_valid(sq)) { strcpy(str, "??"); return; }
    str[0] = (char)('a' + sq_file(sq));
    str[1] = (char)('1' + sq_rank(sq));
    str[2] = '\0';
}

static int str_to_sq(const char* str) {
    int file, rank;
    if (!str[0] || !str[1]) return -1;
    file = str[0] - 'a';
    rank = str[1] - '1';
    if (file < 0 || file > 7 || rank < 0 || rank > 7) return -1;
    return 21 + rank * 10 + file;
}

/* ------------------------------------------------------------------ board */

static void boardClear(BOARD* b) {
    int i;
    for (i = 0; i < 120; i++) b->board[i] = EMPTY;
    b->stm = WHITE;
    b->hisPly = 0;
    b->enPas = -1;
    b->castlePermission = 0;
    b->fiftyMove = 0;
    b->kingSquare[WHITE] = E1;
    b->kingSquare[BLACK] = E8;
    b->hash = 0;
    memset(b->pcount, 0, sizeof(b->pcount));
    accInvalidate(0);
}

static U64 computeHash(BOARD* b) {
    U64 h = 0;
    int sq;
    for (sq = 21; sq <= 98; sq++) {
        if (!sq_valid(sq)) continue;
        if (b->board[sq] != EMPTY) h ^= zobristPiece[b->board[sq]][sq];
    }
    if (b->stm == BLACK) h ^= zobristSide;
    if (b->enPas >= 0)   h ^= zobristEP[b->enPas];
    h ^= zobristCastle[b->castlePermission];
    return h;
}

static void parseFEN(BOARD* b, const char* fen) {
    int sq = A8;
    const char* p = fen;

    boardClear(b);

    while (*p && *p != ' ') {
        if (*p >= '1' && *p <= '8') {
            sq += (*p - '0');
        } else if (*p == '/') {
            sq -= 18;
        } else {
            const char* pc = strchr(pieceChar, *p);
            if (pc && *p != '.') {
                int piece = (int)(pc - pieceChar);
                if (sq_valid(sq)) {
                    b->board[sq] = piece;
                    b->pcount[piece]++;
                    if (piece == wK) b->kingSquare[WHITE] = sq;
                    if (piece == bK) b->kingSquare[BLACK] = sq;
                }
                sq++;
            }
        }
        p++;
    }

    while (*p == ' ') p++;
    b->stm = (*p == 'b') ? BLACK : WHITE;
    while (*p && *p != ' ') p++;
    while (*p == ' ') p++;

    b->castlePermission = 0;
    while (*p && *p != ' ') {
        if (*p == 'K') b->castlePermission |= WKCA;
        if (*p == 'Q') b->castlePermission |= WQCA;
        if (*p == 'k') b->castlePermission |= BKCA;
        if (*p == 'q') b->castlePermission |= BQCA;
        p++;
    }
    while (*p == ' ') p++;

    b->enPas = -1;
    if (*p && *p != '-') {
        int e = str_to_sq(p);
        if (e > 0) b->enPas = e;
    }
    while (*p && *p != ' ') p++;
    while (*p == ' ') p++;
    if (*p >= '0' && *p <= '9') b->fiftyMove = atoi(p);

    b->hash = computeHash(b);
}

/* ------------------------------------------------------- attack detection */

/* Is `sq` attacked by any piece of colour `bySide`? */
static int isSquareAttacked(BOARD* b, int sq, int bySide) {
    int i, d, t, p;

    if (bySide == WHITE) {
        if (b->board[sq - 11] == wP) return 1;
        if (b->board[sq - 9]  == wP) return 1;
    } else {
        if (b->board[sq + 11] == bP) return 1;
        if (b->board[sq + 9]  == bP) return 1;
    }

    for (i = 0; i < 8; i++) {
        t = sq + knightDirs[i];
        if (!sq_valid(t)) continue;
        p = b->board[t];
        if (p != EMPTY && BASE(p) == KNIGHT && COLOR_OF(p) == bySide) return 1;
    }

    for (i = 0; i < 8; i++) {
        t = sq + kingDirs[i];
        if (!sq_valid(t)) continue;
        p = b->board[t];
        if (p != EMPTY && BASE(p) == KING && COLOR_OF(p) == bySide) return 1;
    }

    for (i = 0; i < 4; i++) {
        d = rookDirs[i];
        t = sq + d;
        while (sq_valid(t)) {
            p = b->board[t];
            if (p != EMPTY) {
                if (COLOR_OF(p) == bySide && (BASE(p) == ROOK || BASE(p) == QUEEN)) return 1;
                break;
            }
            t += d;
        }
    }

    for (i = 0; i < 4; i++) {
        d = bishopDirs[i];
        t = sq + d;
        while (sq_valid(t)) {
            p = b->board[t];
            if (p != EMPTY) {
                if (COLOR_OF(p) == bySide && (BASE(p) == BISHOP || BASE(p) == QUEEN)) return 1;
                break;
            }
            t += d;
        }
    }

    return 0;
}

static int inCheck(BOARD* b, int side) {
    return isSquareAttacked(b, b->kingSquare[side], 1 - side);
}

/* ------------------------------------------------------- move generation */

/* Pseudo-legal generation. Legality is filtered in genLegal(). */
static int genMoves(BOARD* b, MOVE* moves) {
    int count = 0;
    int stm = b->stm, enemy = 1 - stm;
    int sq, i, dx;

    for (sq = 21; sq <= 98; sq++) {
        int piece, baseType;
        if (!sq_valid(sq)) continue;
        piece = b->board[sq];
        if (piece == EMPTY || COLOR_OF(piece) != stm) continue;
        baseType = BASE(piece);

        if (baseType == PAWN) {
            int dir        = (stm == WHITE) ? UP : DOWN;
            int start_rank = (stm == WHITE) ? 1 : 6;
            int promo_rank = (stm == WHITE) ? 7 : 0;
            int to = sq + dir;

            if (sq_valid(to) && b->board[to] == EMPTY) {
                if (sq_rank(to) == promo_rank) {
                    moves[count++] = MV(sq, to, QUEEN_PROMO);
                    moves[count++] = MV(sq, to, ROOK_PROMO);
                    moves[count++] = MV(sq, to, BISHOP_PROMO);
                    moves[count++] = MV(sq, to, KNIGHT_PROMO);
                } else {
                    moves[count++] = MV(sq, to, QUIET);
                }
                if (sq_rank(sq) == start_rank) {
                    int to2 = to + dir;
                    if (sq_valid(to2) && b->board[to2] == EMPTY)
                        moves[count++] = MV(sq, to2, DOUBLE_PUSH);
                }
            }

            for (dx = -1; dx <= 1; dx += 2) {
                int capto = sq + dir + dx, target;
                if (!sq_valid(capto)) continue;
                target = b->board[capto];
                if (target != EMPTY && COLOR_OF(target) == enemy) {
                    if (sq_rank(capto) == promo_rank) {
                        moves[count++] = MV(sq, capto, QUEEN_PROMO_CAP);
                        moves[count++] = MV(sq, capto, ROOK_PROMO_CAP);
                        moves[count++] = MV(sq, capto, BISHOP_PROMO_CAP);
                        moves[count++] = MV(sq, capto, KNIGHT_PROMO_CAP);
                    } else {
                        moves[count++] = MV(sq, capto, CAPTURE);
                    }
                } else if (target == EMPTY && b->enPas >= 0 && capto == b->enPas) {
                    moves[count++] = MV(sq, capto, EP_CAPTURE);
                }
            }
        }
        else if (baseType == KNIGHT) {
            for (i = 0; i < 8; i++) {
                int to = sq + knightDirs[i], target;
                if (!sq_valid(to)) continue;
                target = b->board[to];
                if (target == EMPTY)                 moves[count++] = MV(sq, to, QUIET);
                else if (COLOR_OF(target) == enemy)  moves[count++] = MV(sq, to, CAPTURE);
            }
        }
        else if (baseType >= BISHOP && baseType <= QUEEN) {
            int ndirs = (baseType == QUEEN) ? 8 : 4;
            for (i = 0; i < ndirs; i++) {
                int dir, to;
                if (baseType == BISHOP)    dir = bishopDirs[i];
                else if (baseType == ROOK) dir = rookDirs[i];
                else                       dir = queenDirs[i];

                to = sq + dir;
                while (sq_valid(to)) {
                    int target = b->board[to];
                    if (target == EMPTY) {
                        moves[count++] = MV(sq, to, QUIET);
                    } else {
                        if (COLOR_OF(target) == enemy) moves[count++] = MV(sq, to, CAPTURE);
                        break;
                    }
                    to += dir;
                }
            }
        }
        else if (baseType == KING) {
            for (i = 0; i < 8; i++) {
                int to = sq + kingDirs[i], target;
                if (!sq_valid(to)) continue;
                target = b->board[to];
                if (target == EMPTY)                 moves[count++] = MV(sq, to, QUIET);
                else if (COLOR_OF(target) == enemy)  moves[count++] = MV(sq, to, CAPTURE);
            }

            if (stm == WHITE && sq == E1) {
                if ((b->castlePermission & WKCA) &&
                    b->board[F1] == EMPTY && b->board[G1] == EMPTY &&
                    b->board[H1] == wR &&
                    !isSquareAttacked(b, E1, BLACK) &&
                    !isSquareAttacked(b, F1, BLACK) &&
                    !isSquareAttacked(b, G1, BLACK))
                    moves[count++] = MV(E1, G1, KING_CASTLE);

                if ((b->castlePermission & WQCA) &&
                    b->board[D1] == EMPTY && b->board[C1] == EMPTY && b->board[B1] == EMPTY &&
                    b->board[A1] == wR &&
                    !isSquareAttacked(b, E1, BLACK) &&
                    !isSquareAttacked(b, D1, BLACK) &&
                    !isSquareAttacked(b, C1, BLACK))
                    moves[count++] = MV(E1, C1, QUEEN_CASTLE);
            }
            else if (stm == BLACK && sq == E8) {
                if ((b->castlePermission & BKCA) &&
                    b->board[F8] == EMPTY && b->board[G8] == EMPTY &&
                    b->board[H8] == bR &&
                    !isSquareAttacked(b, E8, WHITE) &&
                    !isSquareAttacked(b, F8, WHITE) &&
                    !isSquareAttacked(b, G8, WHITE))
                    moves[count++] = MV(E8, G8, KING_CASTLE);

                if ((b->castlePermission & BQCA) &&
                    b->board[D8] == EMPTY && b->board[C8] == EMPTY && b->board[B8] == EMPTY &&
                    b->board[A8] == bR &&
                    !isSquareAttacked(b, E8, WHITE) &&
                    !isSquareAttacked(b, D8, WHITE) &&
                    !isSquareAttacked(b, C8, WHITE))
                    moves[count++] = MV(E8, C8, QUEEN_CASTLE);
            }
        }
    }
    return count;
}

/* --------------------------------------------------------- make / unmake */

static void deltaRem(MOVE_UNDO* u, int piece, int sq) {
    if (u->nRem >= 2) return;
    u->remP[u->nRem] = (unsigned char)piece;
    u->remS[u->nRem] = (unsigned char)sq_to64(sq);
    u->nRem++;
}

static void deltaAdd(MOVE_UNDO* u, int piece, int sq) {
    if (u->nAdd >= 2) return;
    u->addP[u->nAdd] = (unsigned char)piece;
    u->addS[u->nAdd] = (unsigned char)sq_to64(sq);
    u->nAdd++;
}

static void makeMove(BOARD* b, MOVE move) {
    int from = MV_FROM(move), to = MV_TO(move), type = MV_TYPE(move);
    int piece = b->board[from];
    int captured = b->board[to];
    int oldCastle;
    MOVE_UNDO* undo = &b->history[b->hisPly];

    undo->move             = move;
    undo->enPas            = b->enPas;
    undo->castlePermission = b->castlePermission;
    undo->capturedPiece    = captured;
    undo->fiftyMove        = b->fiftyMove;
    undo->hash             = b->hash;
    undo->nRem = undo->nAdd = 0;

    if (b->enPas >= 0) b->hash ^= zobristEP[b->enPas];
    b->enPas = -1;

    deltaRem(undo, piece, from);
    b->hash ^= zobristPiece[piece][from];
    b->board[from] = EMPTY;

    if (captured != EMPTY) {
        deltaRem(undo, captured, to);
        b->hash ^= zobristPiece[captured][to];
        b->pcount[captured]--;
    }
    b->board[to] = piece;
    b->hash ^= zobristPiece[piece][to];

    if (BASE(piece) == PAWN || captured != EMPTY) b->fiftyMove = 0;
    else b->fiftyMove++;

    if (type == DOUBLE_PUSH) {
        b->enPas = from + ((b->stm == WHITE) ? UP : DOWN);
        b->hash ^= zobristEP[b->enPas];
    }

    if (type == EP_CAPTURE) {
        int capSq  = to + ((b->stm == WHITE) ? DOWN : UP);
        int capPce = (b->stm == WHITE) ? bP : wP;
        deltaRem(undo, capPce, capSq);
        b->hash ^= zobristPiece[capPce][capSq];
        b->board[capSq] = EMPTY;
        b->pcount[capPce]--;
    }

    if (type == KING_CASTLE || type == QUEEN_CASTLE) {
        int rook, rf, rt;
        if (b->stm == WHITE) { rook = wR; if (type == KING_CASTLE) { rf = H1; rt = F1; } else { rf = A1; rt = D1; } }
        else                 { rook = bR; if (type == KING_CASTLE) { rf = H8; rt = F8; } else { rf = A8; rt = D8; } }
        deltaRem(undo, rook, rf);
        deltaAdd(undo, rook, rt);
        b->hash ^= zobristPiece[rook][rf];
        b->board[rf] = EMPTY;
        b->board[rt] = rook;
        b->hash ^= zobristPiece[rook][rt];
    }

    if (IS_PROMO(type)) {
        static const int promo[5] = {0, wN, wB, wR, wQ};
        int promoPiece = promo[(type & 3) + 1];
        if (b->stm == BLACK) promoPiece += 6;
        b->hash ^= zobristPiece[piece][to];
        b->board[to] = promoPiece;
        b->hash ^= zobristPiece[promoPiece][to];
        b->pcount[piece]--;
        b->pcount[promoPiece]++;
    }
    deltaAdd(undo, b->board[to], to);

    if (BASE(piece) == KING) b->kingSquare[b->stm] = to;

    oldCastle = b->castlePermission;
    if (from == E1 || to == E1) b->castlePermission &= ~(WKCA | WQCA);
    if (from == E8 || to == E8) b->castlePermission &= ~(BKCA | BQCA);
    if (from == H1 || to == H1) b->castlePermission &= ~WKCA;
    if (from == A1 || to == A1) b->castlePermission &= ~WQCA;
    if (from == H8 || to == H8) b->castlePermission &= ~BKCA;
    if (from == A8 || to == A8) b->castlePermission &= ~BQCA;
    if (oldCastle != b->castlePermission) {
        b->hash ^= zobristCastle[oldCastle];
        b->hash ^= zobristCastle[b->castlePermission];
    }

    b->hash ^= zobristSide;
    b->stm = 1 - b->stm;
    b->hisPly++;
    accInvalidate(b->hisPly);
}

static void undoMove(BOARD* b) {
    MOVE_UNDO* undo;
    MOVE move;
    int from, to, type, piece;

    b->hisPly--;
    undo = &b->history[b->hisPly];
    move = undo->move;
    from = MV_FROM(move);
    to   = MV_TO(move);
    type = MV_TYPE(move);

    b->stm  = 1 - b->stm;
    b->hash = undo->hash;
    b->enPas            = undo->enPas;
    b->castlePermission = undo->castlePermission;
    b->fiftyMove        = undo->fiftyMove;

    piece = b->board[to];
    b->board[to] = undo->capturedPiece;
    if (undo->capturedPiece != EMPTY) b->pcount[undo->capturedPiece]++;

    if (IS_PROMO(type)) {
        int pawn = (b->stm == WHITE) ? wP : bP;
        b->board[from] = pawn;
        b->pcount[piece]--;
        b->pcount[pawn]++;
    }
    else b->board[from] = piece;

    if (type == KING_CASTLE || type == QUEEN_CASTLE) {
        int rook, rf, rt;
        if (b->stm == WHITE) { rook = wR; if (type == KING_CASTLE) { rf = H1; rt = F1; } else { rf = A1; rt = D1; } }
        else                 { rook = bR; if (type == KING_CASTLE) { rf = H8; rt = F8; } else { rf = A8; rt = D8; } }
        b->board[rt] = EMPTY;
        b->board[rf] = rook;
    }

    if (type == EP_CAPTURE) {
        int capSq = to + ((b->stm == WHITE) ? DOWN : UP);
        b->board[capSq] = (b->stm == WHITE) ? bP : wP;
        b->pcount[b->board[capSq]]++;
    }

    if (BASE(b->board[from]) == KING) b->kingSquare[b->stm] = from;
}

/* ---------------------------------------------------- draw / null move */

static int isRepetition(BOARD* b) {
    int i, start = b->hisPly - b->fiftyMove;
    if (start < 0) start = 0;
    for (i = start; i < b->hisPly; i++)
        if (b->history[i].hash == b->hash) return 1;
    return 0;
}

static int hasNonPawnMaterial(BOARD* b, int side) {
    const int* c = b->pcount + (side == BLACK ? 6 : 0);
    return (c[KNIGHT] + c[BISHOP] + c[ROOK] + c[QUEEN]) > 0;
}

#ifndef K7_V1_EXACT
/* K v K, K+minor v K: nobody can ever mate -> dead draw */
static int insufficientMaterial(BOARD* b) {
    const int* c = b->pcount;
    if (c[wP] + c[bP] + c[wR] + c[bR] + c[wQ] + c[bQ]) return 0;
    return (c[wN] + c[wB] + c[bN] + c[bB]) <= 1;
}
#endif

static void makeNullMove(BOARD* b) {
    MOVE_UNDO* undo = &b->history[b->hisPly];
    undo->move             = 0;
    undo->enPas            = b->enPas;
    undo->castlePermission = b->castlePermission;
    undo->capturedPiece    = EMPTY;
    undo->fiftyMove        = b->fiftyMove;
    undo->hash             = b->hash;
    undo->nRem = undo->nAdd = 0;

    if (b->enPas >= 0) b->hash ^= zobristEP[b->enPas];
    b->enPas = -1;
    b->hash ^= zobristSide;
    b->stm = 1 - b->stm;
    b->hisPly++;
    accInvalidate(b->hisPly);
}

static void undoNullMove(BOARD* b) {
    MOVE_UNDO* undo;
    b->hisPly--;
    undo = &b->history[b->hisPly];
    b->stm              = 1 - b->stm;
    b->hash             = undo->hash;
    b->enPas            = undo->enPas;
    b->castlePermission = undo->castlePermission;
    b->fiftyMove        = undo->fiftyMove;
}

static int genLegal(BOARD* b, MOVE* moves) {
    MOVE pseudo[MAX_MOVES];
    int n = genMoves(b, pseudo);
    int count = 0, i, us = b->stm;
    for (i = 0; i < n; i++) {
        makeMove(b, pseudo[i]);
        if (!inCheck(b, us)) moves[count++] = pseudo[i];
        undoMove(b);
    }
    return count;
}

/* ------------------------------------------------------------------ perft */

static long perft(BOARD* b, int depth) {
    MOVE moves[MAX_MOVES];
    int n, i, us;
    long total = 0;

    if (depth == 0) return 1;

    n  = genMoves(b, moves);
    us = b->stm;
    for (i = 0; i < n; i++) {
        makeMove(b, moves[i]);
        if (!inCheck(b, us)) total += perft(b, depth - 1);
        undoMove(b);
    }
    return total;
}

/* ================================================================
 *  EVALUATION: K7 CUSTOM NET (K7NN format from k7_train)
 *
 *  768 inputs -> H x2 (SCReLU) -> 1
 *  feature = colour*384 + type*64 + square      (White's view, a1 = 0)
 *            (colour^1)*384 + type*64 + (sq^56)  (Black's view, board flipped)
 *  Kings are ordinary features here, so a king move is just two row
 *  updates -- the accumulator never needs a full refresh during search.
 * ================================================================ */

/* ---------------------------------------------------------------- the active net
 * The search reads these globals. activateBrain() points them at one of the
 * net slots below, so switching brains costs nothing at search time.
 * ------------------------------------------------------------------------------ */
static int      nnH       = 0;       /* hidden size of the active net    */
static int      nnQA      = 255;
static int      nnQB      = 64;
static int      nnScale   = 400;
static short*   nnW1      = NULL;    /* [768][H]                         */
static short*   nnB1      = NULL;    /* [H]                              */
static short*   nnW2      = NULL;    /* [2H]  side to move, then other   */
static int      nnB2      = 0;
static int      nnLoaded  = 0;       /* active brain has a usable net    */
static short*   nnAccMem  = NULL;    /* (HIST_SIZE+1) x 2 x H            */
static int      nnAccH    = 0;       /* H that nnAccMem was sized for    */

static short* accPtr(int ply, int p) { return nnAccMem + ((size_t)ply * 2 + (size_t)p) * (size_t)nnH; }

static unsigned int rd32(const unsigned char* p) {
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8) |
           ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24);
}

/* ---------------------------------------------------------------- built-in files
 * The assembler's .incbin directive copies each file into the exe's
 * read-only data (the trick Stockfish uses for its net). The files are
 * looked up in the folder you compile in. Change a name with
 * -DN8_NET1="other.k7nn" etc., or build with -DN8_NO_EMBED to read the
 * files next to the exe at startup instead.
 * ------------------------------------------------------------------------------ */
#ifndef N8_NET1
#  define N8_NET1 "k7net21k.k7nn"
#endif
#ifndef N8_NET2
#  define N8_NET2 "k7net22k.k7nn"
#endif
#ifndef N8_NET3
#  define N8_NET3 "k7net23k.k7nn"
#endif
#ifndef N8_NET4
#  define N8_NET4 "k7net24k.k7nn"
#endif
#ifndef N8_NET5
#  define N8_NET5 "k7net25k.k7nn"
#endif
#ifndef N8_NET6
#  define N8_NET6 "k7net31k.k7nn"
#endif
#ifndef N8_BOOK
#  define N8_BOOK "k7Opennings.bin"
#endif

static const char* const n8NetFiles[7] = { "", N8_NET1, N8_NET2, N8_NET3, N8_NET4, N8_NET5, N8_NET6 };

#ifndef N8_NO_EMBED
#  if defined(_WIN32)
#    define K7_RODATA ".section .rdata,\"dr\"\n"
#  elif defined(__APPLE__)
#    define K7_RODATA ".const_data\n"
#  else
#    define K7_RODATA ".section .rodata\n"
#  endif
#  if defined(__APPLE__) || (defined(_WIN32) && !defined(_WIN64))
#    define K7_SYM(name) "_" #name          /* 32-bit Windows / macOS prefix */
#  else
#    define K7_SYM(name) #name
#  endif
#  define K7_INCBIN(sym, file)                                         \
    __asm__(K7_RODATA ".balign 64\n"                                   \
            ".globl " K7_SYM(sym) "\n" K7_SYM(sym) ":\n"               \
            ".incbin \"" file "\"\n"                                   \
            ".globl " K7_SYM(sym##End) "\n" K7_SYM(sym##End) ":\n"     \
            ".byte 0\n.text\n");                                       \
    extern const unsigned char sym[];                                  \
    extern const unsigned char sym##End[];

K7_INCBIN(n8Net1, N8_NET1)
K7_INCBIN(n8Net2, N8_NET2)
K7_INCBIN(n8Net3, N8_NET3)
K7_INCBIN(n8Net4, N8_NET4)
K7_INCBIN(n8Net5, N8_NET5)
K7_INCBIN(n8Net6, N8_NET6)
#  ifndef N8_NO_BOOK
K7_INCBIN(n8Book, N8_BOOK)
#  endif
static const unsigned char* const n8NetData[7] = { 0, n8Net1, n8Net2, n8Net3, n8Net4, n8Net5, n8Net6 };
static const unsigned char* const n8NetEnd[7]  = { 0, n8Net1End, n8Net2End, n8Net3End, n8Net4End, n8Net5End, n8Net6End };
#endif

/* ---------------------------------------------------------------- net slots */
#define N8_BRAINS 8                     /* 0 Homebrew, 1-6 built in, 7 NetFile */
typedef struct {
    int    ok, H, QA, QB, scale, B2;
    short *W1, *B1, *W2;
    char   label[48];                   /* "31k", "Homebrew", "NetFile"      */
    char   src[2300];                   /* where it came from                */
} K7NET;

static K7NET g_net[N8_BRAINS];
static int   g_brain = 6;               /* the brain that plays (UCI "Brain") */
static char  nnPath[1024] = "<empty>";  /* NetFile for Brain 7               */
static char  g_exeDir[1024] = "";       /* folder of the exe, ends in a slash */
static char  g_exeBase[256] = "";       /* exe file name without folder/.exe  */
static char  g_engineName[300] = "ChesslabNnet8";

static void netFree(K7NET* n) {
    free(n->W1); free(n->B1); free(n->W2);
    n->W1 = n->B1 = n->W2 = NULL;
    n->ok = 0;
}

/* "D:\nets\k7net31k.k7nn" -> "31k" */
static void netLabel(const char* file, char* out, size_t cap) {
    const char *s1 = strrchr(file, '/'), *s2 = strrchr(file, '\\');
    const char* b = s1 > s2 ? s1 : s2;
    size_t n;
    b = b ? b + 1 : file;
    if (!strncmp(b, "k7net", 5) && b[5]) b += 5;
    n = strcspn(b, ".");
    if (n == 0) n = strlen(b);
    if (n >= cap) n = cap - 1;
    memcpy(out, b, n);
    out[n] = 0;
}

/* Parse a K7NN net (768 -> H x2 -> 1, from k7_train) out of memory. */
static int netParse(K7NET* n, const unsigned char* d, size_t fsz, const char* src) {
    size_t off, i, nW1, need;
    int H, QA, QB, sc;
    char label[48];

    strncpy(label, n->label, sizeof(label) - 1);
    label[sizeof(label) - 1] = 0;
    netFree(n);
    if (fsz < 24 || memcmp(d, "K7NN", 4) != 0 || rd32(d + 4) != 1u) {
        say("info string %s is not a K7NN net (make it with k7_train)\n", src);
        return 0;
    }
    H  = (int)rd32(d + 8);
    QA = (int)rd32(d + 12);
    QB = (int)rd32(d + 16);
    sc = (int)rd32(d + 20);
    nW1  = (size_t)768 * (size_t)(H > 0 ? H : 0);
    need = 24 + 2 * (nW1 + 3 * (size_t)(H > 0 ? H : 0)) + 4;
    if (H <= 0 || H > 4096 || QA <= 0 || QB <= 0 || fsz != need) {
        say("info string %s has a bad header or size\n", src);
        return 0;
    }
    n->W1 = (short*)malloc(sizeof(short) * nW1);
    n->B1 = (short*)malloc(sizeof(short) * (size_t)H);
    n->W2 = (short*)malloc(sizeof(short) * 2 * (size_t)H);
    if (!n->W1 || !n->B1 || !n->W2) { netFree(n); say("info string %s: out of memory\n", src); return 0; }

    off = 24;
    for (i = 0; i < nW1; i++, off += 2)            n->W1[i] = (short)(d[off] | (d[off + 1] << 8));
    for (i = 0; i < (size_t)H; i++, off += 2)      n->B1[i] = (short)(d[off] | (d[off + 1] << 8));
    for (i = 0; i < 2 * (size_t)H; i++, off += 2)  n->W2[i] = (short)(d[off] | (d[off + 1] << 8));
    n->B2 = (int)rd32(d + off);
    n->H = H; n->QA = QA; n->QB = QB; n->scale = sc;
    n->ok = 1;
    snprintf(n->label, sizeof(n->label), "%s", label);
    snprintf(n->src, sizeof(n->src), "%s", src);
    return 1;
}

/* Whole file into memory (caller frees). */
static unsigned char* readWholeFile(const char* path, size_t* size) {
    FILE* f = fopen(path, "rb");
    long fsz;
    unsigned char* d;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    fsz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (fsz <= 0) { fclose(f); return NULL; }
    d = (unsigned char*)malloc((size_t)fsz);
    if (d && fread(d, 1, (size_t)fsz, f) != (size_t)fsz) { free(d); d = NULL; }
    fclose(f);
    if (d) *size = (size_t)fsz;
    return d;
}

/* A file next to the exe first, then the current folder. */
static unsigned char* readNearExe(const char* name, size_t* size, char* usedPath, size_t cap) {
    unsigned char* d = NULL;
    if (g_exeDir[0] && !strchr(name, '/') && !strchr(name, '\\') && strchr(name, ':') == NULL) {
        snprintf(usedPath, cap, "%s%s", g_exeDir, name);
        d = readWholeFile(usedPath, size);
    }
    if (!d) {
        snprintf(usedPath, cap, "%s", name);
        d = readWholeFile(usedPath, size);
    }
    return d;
}

static int netLoadFile(K7NET* n, const char* path) {
    size_t sz = 0;
    char used[2200];
    unsigned char* d = readNearExe(path, &sz, used, sizeof(used));
    int ok;
    if (!d) { netFree(n); say("info string cannot open net %s\n", path); return 0; }
    ok = netParse(n, d, sz, used);
    free(d);
    return ok;
}

/* Built-in nets (or the same files next to the exe with -DN8_NO_EMBED). */
static void loadBuiltinNets(void) {
    int i;
    strcpy(g_net[0].label, "Homebrew");
    strcpy(g_net[7].label, "NetFile");
    for (i = 1; i <= 6; i++) {
        netLabel(n8NetFiles[i], g_net[i].label, sizeof(g_net[i].label));
#ifndef N8_NO_EMBED
        {
            char src[200];
            snprintf(src, sizeof(src), "built-in %s", n8NetFiles[i]);
            netParse(&g_net[i], n8NetData[i], (size_t)(n8NetEnd[i] - n8NetData[i]), src);
        }
#else
        netLoadFile(&g_net[i], n8NetFiles[i]);
#endif
    }
}

static int ensureAccMem(int H) {
    short* m;
    if (H <= nnAccH && nnAccMem) return 1;
    m = (short*)realloc(nnAccMem, sizeof(short) * (size_t)(HIST_SIZE + 1) * 2 * (size_t)H);
    if (!m) return 0;
    nnAccMem = m;
    nnAccH = H;
    return 1;
}

/* Point the evaluator at brain br. No TT/cache clearing here: setBrain()
   does that for the GUI, self-play keeps one table per player instead. */
static void activateBrain(int br) {
    if (br < 0 || br >= N8_BRAINS) br = 6;
    g_brain = br;
    nnLoaded = 0;
    if (br >= 1 && g_net[br].ok && ensureAccMem(g_net[br].H)) {
        K7NET* n = &g_net[br];
        nnH = n->H; nnQA = n->QA; nnQB = n->QB; nnScale = n->scale;
        nnW1 = n->W1; nnB1 = n->B1; nnW2 = n->W2; nnB2 = n->B2;
        nnLoaded = 1;
    }
    memset(g_accComp, 0, sizeof(g_accComp));      /* accumulators belong to the old net */
}

static const char* brainName(int br) {
    if (br < 0 || br >= N8_BRAINS) return "?";
    return g_net[br].label[0] ? g_net[br].label : "?";
}

static int nnFeature(int persp, int piece, int s64) {
    int color = IS_BLACK(piece) ? BLACK : WHITE, type = BASE(piece) - 1;
    if (persp == WHITE) return color * 384 + type * 64 + s64;
    return (color ^ 1) * 384 + type * 64 + (s64 ^ 56);
}

static void nnAddRow(short* acc, int f) {
    const short* w = nnW1 + (size_t)f * (size_t)nnH;
    int i;
    for (i = 0; i < nnH; i++) acc[i] = (short)(acc[i] + w[i]);
}

static void nnSubRow(short* acc, int f) {
    const short* w = nnW1 + (size_t)f * (size_t)nnH;
    int i;
    for (i = 0; i < nnH; i++) acc[i] = (short)(acc[i] - w[i]);
}

static void nnRefresh(BOARD* b, int ply, int p) {
    short* acc = accPtr(ply, p);
    int sq;
    memcpy(acc, nnB1, sizeof(short) * (size_t)nnH);
    for (sq = 21; sq <= 98; sq++) {
        int pc;
        if (!sq_valid(sq)) continue;
        pc = b->board[sq];
        if (pc != EMPTY) nnAddRow(acc, nnFeature(p, pc, sq_to64(sq)));
    }
    g_accComp[ply][p] = 1;
}

#if defined(__GNUC__) || defined(__clang__)
#  define K7R __restrict
#else
#  define K7R
#endif

static const short* nnRow(int p, int piece, int s64) {
    return nnW1 + (size_t)nnFeature(p, piece, s64) * (size_t)nnH;
}

/* dst = src - removed rows + added rows in ONE pass over the accumulator.
   Simple loops like these are auto-vectorized (AVX2 with -march=native).
   short arithmetic wraps exactly like the old row-by-row version. */
static void nnApplyDelta(short* K7R dst, const short* K7R src, const MOVE_UNDO* u, int p) {
    int i, H = nnH;
    if (u->nRem == 1 && u->nAdd == 1) {                 /* quiet move */
        const short* K7R r0 = nnRow(p, u->remP[0], u->remS[0]);
        const short* K7R a0 = nnRow(p, u->addP[0], u->addS[0]);
        for (i = 0; i < H; i++) dst[i] = (short)(src[i] - r0[i] + a0[i]);
    } else if (u->nRem == 2 && u->nAdd == 1) {          /* capture / promotion-capture / ep */
        const short* K7R r0 = nnRow(p, u->remP[0], u->remS[0]);
        const short* K7R r1 = nnRow(p, u->remP[1], u->remS[1]);
        const short* K7R a0 = nnRow(p, u->addP[0], u->addS[0]);
        for (i = 0; i < H; i++) dst[i] = (short)(src[i] - r0[i] - r1[i] + a0[i]);
    } else if (u->nRem == 2 && u->nAdd == 2) {          /* castling */
        const short* K7R r0 = nnRow(p, u->remP[0], u->remS[0]);
        const short* K7R r1 = nnRow(p, u->remP[1], u->remS[1]);
        const short* K7R a0 = nnRow(p, u->addP[0], u->addS[0]);
        const short* K7R a1 = nnRow(p, u->addP[1], u->addS[1]);
        for (i = 0; i < H; i++) dst[i] = (short)(src[i] - r0[i] - r1[i] + a0[i] + a1[i]);
    } else {                                            /* null move or anything else */
        int r;
        memcpy(dst, src, sizeof(short) * (size_t)H);
        for (r = 0; r < u->nRem; r++) nnSubRow(dst, nnFeature(p, u->remP[r], u->remS[r]));
        for (r = 0; r < u->nAdd; r++) nnAddRow(dst, nnFeature(p, u->addP[r], u->addS[r]));
    }
}

/* Bring this ply's accumulator up to date by replaying move deltas from the
   nearest computed ancestor (a full refresh only if none is close). */
static void nnUpdate(BOARD* b, int p) {
    int h = b->hisPly, j = h, steps = 0, k;
    if (g_accComp[h][p]) return;
    while (!g_accComp[j][p]) {
        if (j == 0 || ++steps > 32) { nnRefresh(b, h, p); return; }
        j--;
    }
    for (k = j; k < h; k++) {
        nnApplyDelta(accPtr(k + 1, p), accPtr(k, p), &b->history[k], p);
        g_accComp[k + 1][p] = 1;
    }
}

/* Network output in centipawns, side to move. */
static int evalNet(BOARD* b) {
    const short *as, *ao;
    long long sum = 0, out;
    int i, H = nnH, QA = nnQA;

    nnUpdate(b, WHITE);
    nnUpdate(b, BLACK);
    as = accPtr(b->hisPly, b->stm);
    ao = accPtr(b->hisPly, 1 - b->stm);

    /* SCReLU output. Blocks of 64 keep every partial sum inside int32
       (|w2| <= 127 from k7_train, so 64*2*255*255*127 < 2^31), which lets the
       compiler vectorize the loop; the total is exactly the old int64 sum. */
    {
        const short* K7R w2s = nnW2;
        const short* K7R w2o = nnW2 + H;
        int blk;
        for (blk = 0; blk < H; blk += 64) {
            int end = blk + 64 < H ? blk + 64 : H, s32 = 0;
            for (i = blk; i < end; i++) {
                int cs = as[i] < 0 ? 0 : (as[i] > QA ? QA : as[i]);
                int co = ao[i] < 0 ? 0 : (ao[i] > QA ? QA : ao[i]);
                s32 += cs * cs * w2s[i] + co * co * w2o[i];
            }
            sum += s32;
        }
    }
    out = sum / QA + nnB2;
    out = out * nnScale / ((long long)QA * nnQB);
    if (out >  20000) out =  20000;
    if (out < -20000) out = -20000;
    return (int)out;
}

/* Used only when no net is loaded, so the engine still plays legal chess. */
static int evalMaterial(BOARD* b) {
    int sq, s = 0;
    for (sq = 21; sq <= 98; sq++) {
        int p;
        if (!sq_valid(sq)) continue;
        p = b->board[sq];
        if (p == EMPTY || BASE(p) == KING) continue;
        s += IS_BLACK(p) ? -pieceValue[p] : pieceValue[p];
    }
    return (b->stm == WHITE) ? s : -s;
}


/* ================================================================
 *  BRAIN 0: HOMEBREW EVAL  (ChesslabHB7 / K7Homebrew v5.2, unchanged)
 * ================================================================ */

/* ================================================================
 *  v5.2 WEIGHTS  (Homebrew eval + Homebrew-only search margins)
 *  X(Name, default, min, max). Names starting with S_ are search.
 * ================================================================ */
#define WEIGHT_LIST \
    X(PawnMG,          100,   70,  130) \
    X(PawnEG,          120,   80,  170) \
    X(KnightMG,        320,  250,  400) \
    X(KnightEG,        300,  230,  380) \
    X(BishopMG,        330,  260,  410) \
    X(BishopEG,        320,  250,  400) \
    X(RookMG,          500,  400,  620) \
    X(RookEG,          530,  420,  650) \
    X(QueenMG,         900,  750, 1100) \
    X(QueenEG,         950,  780, 1150) \
    X(PSTScaleMG,      100,    0,  200) \
    X(PSTScaleEG,      100,    0,  200) \
    X(DoubledMG,        15,    0,   40) \
    X(DoubledEG,        15,    0,   40) \
    X(IsolatedMG,       18,    0,   40) \
    X(IsolatedEG,       18,    0,   40) \
    X(BackwardMG,        8,    0,   30) \
    X(BackwardEG,       10,    0,   30) \
    X(PassedMG,        100,    0,  250) \
    X(PassedEG,        130,    0,  300) \
    X(ProtectedPasser,  25,    0,  100) \
    X(BishopPairMG,     35,    0,   80) \
    X(BishopPairEG,     45,    0,  100) \
    X(KnightPair,        8,    0,   30) \
    X(RookPair,         16,    0,   40) \
    X(KnightPawnAdj,     4,    0,   10) \
    X(RookPawnAdj,       4,    0,   10) \
    X(RookOpen,         20,    0,   50) \
    X(RookHalfOpen,     10,    0,   40) \
    X(Rook7thMG,        20,    0,   50) \
    X(Rook7thEG,        30,    0,   70) \
    X(Queen7thMG,        5,    0,   30) \
    X(Queen7thEG,       10,    0,   40) \
    X(MobKnightMG,       4,    0,   10) \
    X(MobKnightEG,       4,    0,   10) \
    X(MobBishopMG,       3,    0,   10) \
    X(MobBishopEG,       3,    0,   10) \
    X(MobRookMG,         2,    0,    8) \
    X(MobRookEG,         4,    0,   10) \
    X(MobQueenMG,        1,    0,    5) \
    X(MobQueenEG,        2,    0,    6) \
    X(ShieldRank2,      12,    0,   30) \
    X(ShieldRank3,       6,    0,   25) \
    X(KingAttack,      100,    0,  250) \
    X(Tempo,            10,    0,   30) \
    X(S_AspWindow,      35,   10,  100) \
    X(S_RFPMargin,     120,    0,  300) \
    X(S_FutilityMargin,200,    0,  500) \
    X(S_DeltaMargin,   200,    0,  600)

enum {
#define X(n, d, lo, hi) W_##n,
    WEIGHT_LIST
#undef X
    NUM_W
};

typedef struct { const char* name; int def, lo, hi; } WEIGHT_DEF;
static const WEIGHT_DEF wdef[NUM_W] = {
#define X(n, d, lo, hi) { #n, d, lo, hi },
    WEIGHT_LIST
#undef X
};
static int W[NUM_W];

static char weightsPath[1024] = "weights.txt";
static int  weightsLoaded = -1;

static void evalCacheClear(void);

static void weightsReset(void) {
    int i;
    for (i = 0; i < NUM_W; i++) W[i] = wdef[i].def;
}

static int weightFind(const char* name) {
    int i;
    for (i = 0; i < NUM_W; i++) {
        const char *a = wdef[i].name, *c = name;
        while (*a && *c && tolower((unsigned char)*a) == tolower((unsigned char)*c)) { a++; c++; }
        if (!*a && !*c) return i;
    }
    return -1;
}

/* Out-of-range values are clamped -- a GUI rejects a spin value outside
   the range it was given, so the engine must never hold one. */
static int weightSet(const char* name, int v) {
    int i = weightFind(name);
    if (i < 0) return 0;
    if (v < wdef[i].lo) v = wdef[i].lo;
    if (v > wdef[i].hi) v = wdef[i].hi;
    if (W[i] != v) { W[i] = v; evalCacheClear(); }
    return 1;
}

/* "Name value" or "Name=value" per line; # starts a comment. */
static int weightsLoad(const char* path) {
    FILE* fp = fopen(path, "r");
    char line[256], name[128];
    int v, n = 0;
    if (!fp) return -1;
    while (fgets(line, sizeof(line), fp)) {
        char* p = line;
        char* hash = strchr(line, '#');
        if (hash) *hash = 0;
        while (*p) { if (*p == '=' || *p == ':' || *p == ',') *p = ' '; p++; }
        if (sscanf(line, "%127s %d", name, &v) == 2 && weightSet(name, v)) n++;
    }
    fclose(fp);
    evalCacheClear();
    return n;
}

static void weightsPrint(void) {
    int i;
    for (i = 0; i < NUM_W; i++) say("%s %d\n", wdef[i].name, W[i]);
}

/* PST indexed [baseType-1][0..63], written from White's a1 as index 0. */
static const int pstTable[6][64] = {
{  0,  0,  0,  0,  0,  0,  0,  0,
   5, 10, 10,-20,-20, 10, 10,  5,
   5, -5,-10,  0,  0,-10, -5,  5,
   0,  0,  0, 20, 20,  0,  0,  0,
   5,  5, 10, 25, 25, 10,  5,  5,
  10, 10, 20, 30, 30, 20, 10, 10,
  50, 50, 50, 50, 50, 50, 50, 50,
   0,  0,  0,  0,  0,  0,  0,  0 },
{-50,-40,-30,-30,-30,-30,-40,-50,
 -40,-20,  0,  5,  5,  0,-20,-40,
 -30,  5, 10, 15, 15, 10,  5,-30,
 -30,  0, 15, 20, 20, 15,  0,-30,
 -30,  5, 15, 20, 20, 15,  5,-30,
 -30,  0, 10, 15, 15, 10,  0,-30,
 -40,-20,  0,  0,  0,  0,-20,-40,
 -50,-40,-30,-30,-30,-30,-40,-50 },
{-20,-10,-10,-10,-10,-10,-10,-20,
 -10,  5,  0,  0,  0,  0,  5,-10,
 -10, 10, 10, 10, 10, 10, 10,-10,
 -10,  0, 10, 10, 10, 10,  0,-10,
 -10,  5,  5, 10, 10,  5,  5,-10,
 -10,  0,  5, 10, 10,  5,  0,-10,
 -10,  0,  0,  0,  0,  0,  0,-10,
 -20,-10,-10,-10,-10,-10,-10,-20 },
{  0,  0,  0,  5,  5,  0,  0,  0,
  -5,  0,  0,  0,  0,  0,  0, -5,
  -5,  0,  0,  0,  0,  0,  0, -5,
  -5,  0,  0,  0,  0,  0,  0, -5,
  -5,  0,  0,  0,  0,  0,  0, -5,
  -5,  0,  0,  0,  0,  0,  0, -5,
   5, 10, 10, 10, 10, 10, 10,  5,
   0,  0,  0,  0,  0,  0,  0,  0 },
{-20,-10,-10, -5, -5,-10,-10,-20,
 -10,  0,  5,  0,  0,  0,  0,-10,
 -10,  5,  5,  5,  5,  5,  0,-10,
   0,  0,  5,  5,  5,  5,  0, -5,
  -5,  0,  5,  5,  5,  5,  0, -5,
 -10,  0,  5,  5,  5,  5,  0,-10,
 -10,  0,  0,  0,  0,  0,  0,-10,
 -20,-10,-10, -5, -5,-10,-10,-20 },
{ 20, 30, 10,  0,  0, 10, 30, 20,
  20, 20,  0,  0,  0,  0, 20, 20,
 -10,-20,-20,-20,-20,-20,-20,-10,
 -20,-30,-30,-40,-40,-30,-30,-20,
 -30,-40,-40,-50,-50,-40,-40,-30,
 -30,-40,-40,-50,-50,-40,-40,-30,
 -30,-40,-40,-50,-50,-40,-40,-30,
 -30,-40,-40,-50,-50,-40,-40,-30 }
};

static const int kingEG[64] = {
 -50,-30,-30,-30,-30,-30,-30,-50,
 -30,-30,  0,  0,  0,  0,-30,-30,
 -30,-10, 20, 30, 30, 20,-10,-30,
 -30,-10, 30, 40, 40, 30,-10,-30,
 -30,-10, 30, 40, 40, 30,-10,-30,
 -30,-10, 20, 30, 30, 20,-10,-30,
 -30,-20,-10,  0,  0,-10,-20,-30,
 -50,-40,-30,-20,-20,-30,-40,-50
};

/* Passed-pawn bonus by rank from the owner's side, scaled by PassedMG/EG %. */
static const int passedBonus[8] = {0, 5, 10, 20, 35, 60, 100, 0};

/* Attack units per piece type landing in the enemy king zone. */
static const int hbAttackUnit[7] = {0, 0, 2, 2, 3, 5, 0};

static int hbKingZone(int sq, int ksq) {
    int df = sq_file(sq) - sq_file(ksq), dr = sq_rank(sq) - sq_rank(ksq);
    return df >= -1 && df <= 1 && dr >= -1 && dr <= 1;
}

static int evalHomebrew(BOARD* b) {
    int mg[2] = {0, 0}, eg[2] = {0, 0};
    int pawnCnt[2][10], pawnMin[2][10], pawnMax[2][10];
    unsigned char pAtt[2][120];
    int knights[2] = {0,0}, bishops[2] = {0,0}, rooks[2] = {0,0}, queens[2] = {0,0};
    int pawns[2] = {0,0}, attackers[2] = {0,0}, attUnits[2] = {0,0};
    int matMG[7], matEG[7];
    int phaseUnits = 0, phase, sq, f, r, c, score;

    matMG[0] = matEG[0] = 0;
    matMG[1] = W[W_PawnMG];   matEG[1] = W[W_PawnEG];
    matMG[2] = W[W_KnightMG]; matEG[2] = W[W_KnightEG];
    matMG[3] = W[W_BishopMG]; matEG[3] = W[W_BishopEG];
    matMG[4] = W[W_RookMG];   matEG[4] = W[W_RookEG];
    matMG[5] = W[W_QueenMG];  matEG[5] = W[W_QueenEG];
    matMG[6] = matEG[6] = 0;

    for (c = 0; c < 2; c++) for (f = 0; f < 10; f++) {
        pawnCnt[c][f] = 0; pawnMax[c][f] = -1; pawnMin[c][f] = 8;
    }
    memset(pAtt, 0, sizeof(pAtt));

    /* Pass 1: pawn skeleton, pawn attacks, piece counts. */
    for (sq = 21; sq <= 98; sq++) {
        int piece, base;
        if (!sq_valid(sq)) continue;
        piece = b->board[sq];
        if (piece == EMPTY) continue;
        base = BASE(piece);
        c = COLOR_OF(piece);
        f = sq_file(sq) + 1;
        r = sq_rank(sq);
        switch (base) {
        case PAWN:
            pawns[c]++;
            pawnCnt[c][f]++;
            if (r > pawnMax[c][f]) pawnMax[c][f] = r;
            if (r < pawnMin[c][f]) pawnMin[c][f] = r;
            if (c == WHITE) { pAtt[WHITE][sq + 9] = 1;  pAtt[WHITE][sq + 11] = 1; }
            else            { pAtt[BLACK][sq - 9] = 1;  pAtt[BLACK][sq - 11] = 1; }
            break;
        case KNIGHT: knights[c]++; phaseUnits += 1; break;
        case BISHOP: bishops[c]++; phaseUnits += 1; break;
        case ROOK:   rooks[c]++;   phaseUnits += 2; break;
        case QUEEN:  queens[c]++;  phaseUnits += 4; break;
        }
    }

    /* Phase from piece counts: 256 = all pieces on, 0 = kings and pawns. */
    if (phaseUnits > 24) phaseUnits = 24;
    phase = phaseUnits * 256 / 24;

    /* Pass 2: everything else. */
    for (sq = 21; sq <= 98; sq++) {
        int piece, base, idx, them, rel, pst;
        if (!sq_valid(sq)) continue;
        piece = b->board[sq];
        if (piece == EMPTY) continue;
        base = BASE(piece);
        c    = COLOR_OF(piece);
        them = 1 - c;
        f    = sq_file(sq) + 1;
        r    = sq_rank(sq);
        rel  = (c == WHITE) ? r : 7 - r;
        idx  = sq_to64(sq);
        if (c == BLACK) idx = ((7 - (idx / 8)) * 8) + (idx % 8);

        mg[c] += matMG[base];
        eg[c] += matEG[base];

        if (base == KING) {
            mg[c] += pstTable[5][idx];
            eg[c] += kingEG[idx];
        } else {
            pst = pstTable[base - 1][idx];
            mg[c] += pst * W[W_PSTScaleMG] / 100;
            eg[c] += pst * W[W_PSTScaleEG] / 100;
        }

        if (base == PAWN) {
            int passed = 1, ff, isolated;
            int ahead = (c == WHITE) ? sq + 10 : sq - 10;

            if (pawnCnt[c][f] > 1) { mg[c] -= W[W_DoubledMG]; eg[c] -= W[W_DoubledEG]; }

            isolated = (pawnCnt[c][f - 1] == 0 && pawnCnt[c][f + 1] == 0);
            if (isolated) {
                mg[c] -= W[W_IsolatedMG]; eg[c] -= W[W_IsolatedEG];
            } else {
                /* Backward: no neighbour pawn level or behind, and the stop
                   square is covered by an enemy pawn. */
                int support = 0;
                for (ff = f - 1; ff <= f + 1; ff += 2) {
                    if (!pawnCnt[c][ff]) continue;
                    if (c == WHITE ? pawnMin[c][ff] <= r : pawnMax[c][ff] >= r) support = 1;
                }
                if (!support && sq_valid(ahead) && pAtt[them][ahead]) {
                    mg[c] -= W[W_BackwardMG]; eg[c] -= W[W_BackwardEG];
                }
            }

            for (ff = f - 1; ff <= f + 1; ff++) {
                if (ff < 1 || ff > 8 || !pawnCnt[them][ff]) continue;
                if (c == WHITE ? pawnMax[BLACK][ff] > r : pawnMin[WHITE][ff] < r) { passed = 0; break; }
            }
            if (passed) {
                int bonus = passedBonus[rel];
                if (pAtt[c][sq]) bonus += bonus * W[W_ProtectedPasser] / 100;
                mg[c] += bonus * W[W_PassedMG] / 100;
                eg[c] += bonus * W[W_PassedEG] / 100;
            }
        }
        else if (base == KNIGHT) {
            int i, mob = 0, hit = 0;
            for (i = 0; i < 8; i++) {
                int t = sq + knightDirs[i], p;
                if (!sq_valid(t)) continue;
                p = b->board[t];
                if (p != EMPTY && COLOR_OF(p) == c) continue;
                if (hbKingZone(t, b->kingSquare[them])) hit = 1;
                if (!pAtt[them][t]) mob++;
            }
            mg[c] += (mob - 4) * W[W_MobKnightMG];
            eg[c] += (mob - 4) * W[W_MobKnightEG];
            mg[c] += (pawns[c] - 5) * W[W_KnightPawnAdj];
            eg[c] += (pawns[c] - 5) * W[W_KnightPawnAdj];
            if (hit) { attackers[c]++; attUnits[c] += hbAttackUnit[KNIGHT]; }
        }
        else if (base >= BISHOP && base <= QUEEN) {
            int i, nd = (base == QUEEN) ? 8 : 4, mob = 0, hit = 0, ctr;
            for (i = 0; i < nd; i++) {
                int d, t;
                if (base == BISHOP)    d = bishopDirs[i];
                else if (base == ROOK) d = rookDirs[i];
                else                   d = queenDirs[i];
                for (t = sq + d; sq_valid(t); t += d) {
                    int p = b->board[t];
                    if (p != EMPTY && COLOR_OF(p) == c) break;
                    if (hbKingZone(t, b->kingSquare[them])) hit = 1;
                    if (!pAtt[them][t]) mob++;
                    if (p != EMPTY) break;
                }
            }
            if (base == BISHOP) {
                ctr = 7;
                mg[c] += (mob - ctr) * W[W_MobBishopMG];
                eg[c] += (mob - ctr) * W[W_MobBishopEG];
            } else {
                int enemy7 = (them == WHITE) ? 1 : 6;       /* their pawn start rank */
                int k8 = (them == WHITE) ? sq_rank(b->kingSquare[WHITE]) == 0
                                         : sq_rank(b->kingSquare[BLACK]) == 7;
                int pawnsOn7 = 0, ff;
                for (ff = 1; ff <= 8; ff++) {
                    int s = 21 + enemy7 * 10 + (ff - 1);
                    if (b->board[s] == (them == WHITE ? wP : bP)) { pawnsOn7 = 1; break; }
                }
                if (base == ROOK) {
                    ctr = 7;
                    mg[c] += (mob - ctr) * W[W_MobRookMG];
                    eg[c] += (mob - ctr) * W[W_MobRookEG];
                    mg[c] -= (pawns[c] - 5) * W[W_RookPawnAdj];
                    eg[c] -= (pawns[c] - 5) * W[W_RookPawnAdj];
                    if (pawnCnt[c][f] == 0) {
                        int v = (pawnCnt[them][f] == 0) ? W[W_RookOpen] : W[W_RookHalfOpen];
                        mg[c] += v; eg[c] += v;
                    }
                    if (rel == 6 && (pawnsOn7 || k8)) { mg[c] += W[W_Rook7thMG]; eg[c] += W[W_Rook7thEG]; }
                } else {
                    ctr = 14;
                    mg[c] += (mob - ctr) * W[W_MobQueenMG];
                    eg[c] += (mob - ctr) * W[W_MobQueenEG];
                    if (rel == 6 && (pawnsOn7 || k8)) { mg[c] += W[W_Queen7thMG]; eg[c] += W[W_Queen7thEG]; }
                }
            }
            if (hit) { attackers[c]++; attUnits[c] += hbAttackUnit[base]; }
        }
        else if (base == KING) {
            int ff, dir = (c == WHITE) ? 10 : -10, own = (c == WHITE) ? wP : bP;
            for (ff = f - 1; ff <= f + 1; ff++) {
                int s1, s2;
                if (ff < 1 || ff > 8) continue;
                s1 = sq + dir + (ff - f);
                s2 = sq + 2 * dir + (ff - f);
                if (sq_valid(s1) && b->board[s1] == own)      mg[c] += W[W_ShieldRank2];
                else if (sq_valid(s2) && b->board[s2] == own) mg[c] += W[W_ShieldRank3];
            }
        }
    }

    for (c = 0; c < 2; c++) {
        if (bishops[c] >= 2) { mg[c] += W[W_BishopPairMG]; eg[c] += W[W_BishopPairEG]; }
        if (knights[c] >= 2) { mg[c] -= W[W_KnightPair];   eg[c] -= W[W_KnightPair]; }
        if (rooks[c]   >= 2) { mg[c] -= W[W_RookPair];     eg[c] -= W[W_RookPair]; }
        /* King attack: only counts with two attackers and a queen. */
        if (attackers[c] >= 2 && queens[c] > 0) {
            int u = attUnits[c] > 30 ? 30 : attUnits[c];
            mg[c] += u * u * W[W_KingAttack] / 200;
        }
    }

    score = ((mg[WHITE] - mg[BLACK]) * phase + (eg[WHITE] - eg[BLACK]) * (256 - phase)) / 256;

    /* A side with no pawns and at most one minor piece cannot win. */
    if (score > 0 && pawns[WHITE] == 0 && rooks[WHITE] + queens[WHITE] == 0 &&
        knights[WHITE] + bishops[WHITE] <= 1) score = 0;
    if (score < 0 && pawns[BLACK] == 0 && rooks[BLACK] + queens[BLACK] == 0 &&
        knights[BLACK] + bishops[BLACK] <= 1) score = 0;

    score = (b->stm == WHITE) ? score : -score;
    return score + W[W_Tempo];
}

/* ================================================================
 *  EVAL + CACHE
 * ================================================================ */

typedef struct { U64 key; int score; } EVAL_ENTRY;
#define EVAL_CACHE_SIZE (1 << 20)
static EVAL_ENTRY* evalCache = NULL;

static void evalCacheClear(void) {
    if (evalCache) memset(evalCache, 0, sizeof(EVAL_ENTRY) * EVAL_CACHE_SIZE);
}

static int eval(BOARD* b) {
    EVAL_ENTRY* e;
    int s;
    if (g_brain != 0 && !nnLoaded) return evalMaterial(b);
    e = evalCache ? &evalCache[b->hash & (EVAL_CACHE_SIZE - 1)] : NULL;
    if (e && e->key == b->hash) return e->score;
    s = (g_brain == 0) ? evalHomebrew(b) : evalNet(b);
    if (e) { e->key = b->hash; e->score = s; }
    return s;
}

/* ------------------------------------------------------------------- TT */

static void ttClear(void) {
    if (tt) memset(tt, 0, sizeof(TT_ENTRY) * (size_t)ttCount);
}

static void ttResize(int mb) {
    U64 bytes, entries = 1;
    if (mb < 1) mb = 1;
    if (mb > TT_MAX_MB) mb = TT_MAX_MB;
    bytes = (U64)mb * 1024ULL * 1024ULL;
    while ((entries * 2ULL) * sizeof(TT_ENTRY) <= bytes) entries *= 2ULL;
    if (tt) free(tt);
    tt = (TT_ENTRY*)malloc(sizeof(TT_ENTRY) * (size_t)entries);
    while (!tt && entries > 1024) {
        entries /= 2ULL;
        tt = (TT_ENTRY*)malloc(sizeof(TT_ENTRY) * (size_t)entries);
    }
    ttCount = entries;
    ttMask  = entries - 1ULL;
    ttClear();
}

static int ttHashfull(void) {
    int used = 0, i, n = (ttCount < 1000) ? (int)ttCount : 1000;
    for (i = 0; i < n; i++) if (tt[i].key != 0) used++;
    return n ? (used * 1000 / n) : 0;
}

static int ttProbe(U64 hash, int depth, int ply, int alpha, int beta,
                  MOVE* bestMove, int* found) {
    TT_ENTRY* e = &tt[hash & ttMask];
    int sc;
    *found = 0;
    if (e->key != hash) return SCORE_NONE;
    *bestMove = e->bestMove;
    if (e->depth < depth) return SCORE_NONE;

    sc = e->score;
    if (sc >  MATE_SCORE - MAX_PLY) sc -= ply;
    else if (sc < -MATE_SCORE + MAX_PLY) sc += ply;

    if (e->flag == TT_EXACT) { *found = 1; return sc; }
    if (e->flag == TT_ALPHA && sc <= alpha) { *found = 1; return alpha; }
    if (e->flag == TT_BETA  && sc >= beta)  { *found = 1; return beta;  }
    return SCORE_NONE;
}

static void ttStore(U64 hash, int depth, int ply, int score, int flag, MOVE move) {
    TT_ENTRY* e = &tt[hash & ttMask];
    if (score >  MATE_SCORE - MAX_PLY) score += ply;
    else if (score < -MATE_SCORE + MAX_PLY) score -= ply;
    if (e->key == 0 || e->key == hash || depth >= e->depth) {
        e->key      = hash;
        e->depth    = depth;
        e->score    = score;
        e->flag     = flag;
        e->bestMove = move;
    }
}

/* =====================================================================
 *  SEARCH OPTIONS  (UCI check boxes; defaults = K7CustomNetV2,
 *                   all off = the V1 search)
 * ===================================================================== */
static int optRFP      = 1;   /* reverse futility pruning                    */
static int optNullPlus = 1;   /* adaptive null move, only when eval >= beta  */
static int optFutility = 1;   /* futility pruning of quiet moves             */
static int optLMP      = 1;   /* late move pruning                           */
static int optLMRPlus  = 1;   /* log-table late move reductions              */
static int optSEE      = 1;   /* static exchange eval: ordering + pruning    */
static int optDelta    = 0;   /* delta pruning in quiescence (off: tested -40 Elo) */
static int optIIR      = 1;   /* internal iterative reduction                */
static int optOrdering = 1;   /* counter-moves + history gravity/malus       */

#define MATE_BOUND (MATE_SCORE - MAX_PLY)
#define EVAL_NONE  (-INF_SCORE - 1)

static int lmrTable[64][64];

/* natural log without libm (only used once, to fill the LMR table) */
static double k7log(double x) {
    double y, y2, sum = 0.0, term;
    int k = 0, n;
    while (x > 2.0) { x *= 0.5; k++; }
    y = (x - 1.0) / (x + 1.0);
    y2 = y * y;
    term = y;
    for (n = 1; n < 40; n += 2) { sum += term / n; term *= y2; }
    return 2.0 * sum + k * 0.69314718055994531;
}

static void initLMR(void) {
    int d, m;
    for (d = 0; d < 64; d++)
        for (m = 0; m < 64; m++)
            lmrTable[d][m] = (d < 1 || m < 1) ? 0
                           : (int)(0.75 + k7log((double)d) * k7log((double)m) / 2.25);
}

/* ---------------------------------------------------------------- SEE
 * Static exchange evaluation: play out all captures on the target square,
 * least valuable attacker first, and return the material result for the
 * side making the move. Slider x-rays work automatically because captured
 * pieces are taken off a scratch copy of the board.
 * ------------------------------------------------------------------- */
static const int seeVal[13] = { 0, 100, 320, 330, 500, 900, 20000,
                                   100, 320, 330, 500, 900, 20000 };

static int lvaSquare(const int* bd, int sq, int side) {
    int i, t, p, d;
    int pn = MAKE_PC(side, PAWN), kn = MAKE_PC(side, KNIGHT), bi = MAKE_PC(side, BISHOP);
    int rk = MAKE_PC(side, ROOK), qu = MAKE_PC(side, QUEEN), kg = MAKE_PC(side, KING);
    int qsq = 0;

    if (side == WHITE) { if (bd[sq - 11] == pn) return sq - 11; if (bd[sq - 9] == pn) return sq - 9; }
    else               { if (bd[sq + 11] == pn) return sq + 11; if (bd[sq + 9] == pn) return sq + 9; }

    for (i = 0; i < 8; i++) {
        t = sq + knightDirs[i];
        if (sq_valid(t) && bd[t] == kn) return t;
    }
    for (i = 0; i < 4; i++) {                     /* diagonals: bishop first */
        d = bishopDirs[i];
        for (t = sq + d; sq_valid(t); t += d) {
            p = bd[t];
            if (p == EMPTY) continue;
            if (p == bi) return t;
            if (p == qu && !qsq) qsq = t;
            break;
        }
    }
    for (i = 0; i < 4; i++) {                     /* lines: rook before queen */
        d = rookDirs[i];
        for (t = sq + d; sq_valid(t); t += d) {
            p = bd[t];
            if (p == EMPTY) continue;
            if (p == rk) return t;
            if (p == qu && !qsq) qsq = t;
            break;
        }
    }
    if (qsq) return qsq;
    for (i = 0; i < 8; i++) {
        t = sq + kingDirs[i];
        if (sq_valid(t) && bd[t] == kg) return t;
    }
    return 0;
}

static int see(BOARD* b, MOVE m) {
    int bd[120], gain[40], d = 0;
    int from = MV_FROM(m), to = MV_TO(m), type = MV_TYPE(m);
    int piece = b->board[from], side = COLOR_OF(piece), captured, fromSq;

    memcpy(bd, b->board, sizeof(bd));
    if (type == EP_CAPTURE) {
        captured = (side == WHITE) ? bP : wP;
        bd[to + ((side == WHITE) ? DOWN : UP)] = EMPTY;
    } else {
        captured = bd[to];
    }
    gain[0] = (captured != EMPTY) ? seeVal[captured] : 0;
    if (IS_PROMO(type)) {
        static const int promoBase[4] = { KNIGHT, BISHOP, ROOK, QUEEN };
        piece = MAKE_PC(side, promoBase[type & 3]);
        gain[0] += seeVal[piece] - 100;
    }
    fromSq = from;

    for (;;) {
        d++;
        gain[d] = seeVal[piece] - gain[d - 1];        /* if this piece gets taken */
        bd[fromSq] = EMPTY;
        side ^= 1;
        fromSq = lvaSquare(bd, to, side);
        if (!fromSq) break;
        piece = bd[fromSq];
        if (BASE(piece) == KING) {                    /* king may not take a defended piece */
            bd[fromSq] = EMPTY;
            if (lvaSquare(bd, to, side ^ 1)) { bd[fromSq] = piece; break; }
            bd[fromSq] = piece;
        }
        if (d >= 38) break;
    }
    while (--d)
        gain[d - 1] = -(-gain[d - 1] > gain[d] ? -gain[d - 1] : gain[d]);
    return gain[0];
}

/* -------------------------------------------------------------- ordering */

#define SCORE_TT        3000000
#define SCORE_GOODCAP   2000000
#define SCORE_QPROMO    1900000
#define SCORE_KILLER1    800000
#define SCORE_KILLER2    700000
#define SCORE_COUNTER    600000
#define SCORE_BADCAP   (-1000000)
#define SCORE_UNDERPRO (-1500000)
#define HIST_MAX          16384

static int scoreMove(BOARD* b, MOVE m, MOVE ttMove, SEARCH_INFO* info, int ply, MOVE counterMove) {
    int type, victim, attacker, mvvlva;
    if (m == ttMove) return optSEE || optOrdering ? SCORE_TT : 2000000;
    type = MV_TYPE(m);
    if (IS_CAP(type)) {
        victim   = (type == EP_CAPTURE) ? wP : b->board[MV_TO(m)];
        attacker = b->board[MV_FROM(m)];
        mvvlva   = pieceValue[victim] * 10 - pieceValue[attacker];
        if (!optSEE) return 1000000 + mvvlva;                      /* V1 */
        if (IS_PROMO(type) && (type & 3) == 3) return SCORE_GOODCAP + 9000 + mvvlva;
        return (see(b, m) >= 0 ? SCORE_GOODCAP : SCORE_BADCAP) + mvvlva;
    }
    if (IS_PROMO(type)) {
        if (!optSEE && !optOrdering) return 900000;                  /* V1 */
        return (type & 3) == 3 ? SCORE_QPROMO : SCORE_UNDERPRO;
    }
    if (m == info->killers[ply][0]) return SCORE_KILLER1;
    if (m == info->killers[ply][1]) return SCORE_KILLER2;
    if (optOrdering && m && m == counterMove) return SCORE_COUNTER;
    return info->history[b->board[MV_FROM(m)]][MV_TO(m)];
}

static void sortMoves(BOARD* b, MOVE* moves, int n, MOVE ttMove, SEARCH_INFO* info, int ply) {
    int scores[MAX_MOVES], i, j;
    for (i = 0; i < n; i++) scores[i] = scoreMove(b, moves[i], ttMove, info, ply, 0);
    for (i = 1; i < n; i++) {
        MOVE m = moves[i];
        int  s = scores[i];
        j = i - 1;
        while (j >= 0 && scores[j] < s) {
            scores[j + 1] = scores[j];
            moves[j + 1]  = moves[j];
            j--;
        }
        scores[j + 1] = s;
        moves[j + 1]  = m;
    }
}

/* history with "gravity": stays inside +-HIST_MAX, recent results count most */
static void histAdd(int* h, int bonus) {
    int ab = bonus < 0 ? -bonus : bonus;
    *h += bonus - *h * ab / HIST_MAX;
}

/* Lazy ordering: score once, then hand out the best remaining move only when
   it is needed (most nodes cut off after one or two moves). Ties keep their
   original order, so the sequence is exactly the old full sort's. */
static void scoreMoves(BOARD* b, const MOVE* moves, int* scores, int n, MOVE ttMove,
                       SEARCH_INFO* info, int ply, MOVE counterMove) {
    int i;
    for (i = 0; i < n; i++) scores[i] = scoreMove(b, moves[i], ttMove, info, ply, counterMove);
}

static void pickNext(MOVE* moves, int* scores, int n, int i) {
    int j, bi = i, bs = scores[i];
    MOVE m;
    for (j = i + 1; j < n; j++) if (scores[j] > bs) { bs = scores[j]; bi = j; }
    if (bi == i) return;
    m = moves[bi];
    memmove(moves + i + 1, moves + i, sizeof(MOVE) * (size_t)(bi - i));
    memmove(scores + i + 1, scores + i, sizeof(int) * (size_t)(bi - i));
    moves[i] = m;
    scores[i] = bs;
}

/* --------------------------------------------------------------- search */

static int timeUp(SEARCH_INFO* info) {
    double ms;
    if (info->timeLimit <= 0) return 0;
    ms = (double)(clock() - info->startTime) * 1000.0 / CLOCKS_PER_SEC;
    return ms >= info->timeLimit;
}

/* ----------------------------------------------- GUI commands during search
 * Without this the engine cannot hear "stop" while thinking, so analysis
 * ("go infinite", used when you edit or analyse a position in the GUI)
 * never ends. stdin is unbuffered (setbuf in uciLoop), so peeking the pipe
 * or console tells us reliably whether a command is waiting.
 * ------------------------------------------------------------------------ */
static int  g_silent = 0;        /* bench: no info lines                      */
static int  g_quit = 0;          /* "quit" arrived during a search            */
static char g_pending[8192];     /* other command that arrived mid-search:     */
                                 /* run by uciLoop once the search is over     */

static int inputWaiting(void) {
#ifdef _WIN32
    static int init = 0, pipe = 0;
    static HANDLE inh;
    DWORD dw;
    if (!init) {
        init = 1;
        inh = GetStdHandle(STD_INPUT_HANDLE);
        pipe = !GetConsoleMode(inh, &dw);
        if (!pipe) {
            SetConsoleMode(inh, dw & ~(ENABLE_MOUSE_INPUT | ENABLE_WINDOW_INPUT));
            FlushConsoleInputBuffer(inh);
        }
    }
    if (pipe) {
        if (!PeekNamedPipe(inh, NULL, 0, NULL, &dw, NULL))
            return GetLastError() == ERROR_BROKEN_PIPE;     /* GUI closed the pipe */
        return dw > 0;
    }
    GetNumberOfConsoleInputEvents(inh, &dw);
    return dw > 1;
#else
    fd_set readfds;
    struct timeval tv;
    FD_ZERO(&readfds);
    FD_SET(fileno(stdin), &readfds);
    tv.tv_sec = 0;
    tv.tv_usec = 0;
    select(fileno(stdin) + 1, &readfds, NULL, NULL, &tv);
    return FD_ISSET(fileno(stdin), &readfds);
#endif
}

/* Handle one command that arrived while thinking. Returns 1 if it means "stop". */
static int handleSearchCommand(void) {
    char cmd[8192];
    if (!fgets(cmd, sizeof(cmd), stdin)) { g_quit = 1; return 1; }    /* stdin closed */
    cmd[strcspn(cmd, "\r\n")] = 0;
    logLine(">>", cmd);
    if (!strncmp(cmd, "stop", 4))  return 1;
    if (!strncmp(cmd, "quit", 4))  { g_quit = 1; return 1; }
    if (!strncmp(cmd, "isready", 7)) { say("readyok\n"); fflush(stdout); return 0; }
    if (cmd[0] && !g_pending[0]) {                 /* keep it for later */
        strncpy(g_pending, cmd, sizeof(g_pending) - 1);
        g_pending[sizeof(g_pending) - 1] = 0;
    }
    return 0;
}

/* Called every 2048 nodes: time limit, then any waiting GUI command. */
static int checkUp(SEARCH_INFO* info) {
    if (info->stopped) return 1;                  /* already stopping: read nothing more */
    if (timeUp(info)) info->stopped = 1;
    else if (!g_silent && !g_pending[0] && inputWaiting() && handleSearchCommand()) info->stopped = 1;
    return info->stopped;
}

static int quiesce(BOARD* b, int alpha, int beta, SEARCH_INFO* info) {
    MOVE moves[MAX_MOVES];
    int scores[MAX_MOVES];
    int n, i, us, stand, deltaMargin;

    if (info->stopped || ((info->nodes & 2047) == 0 && checkUp(info))) return 0;
    info->nodes++;
    if (info->nodeLimit && info->nodes >= info->nodeLimit) info->stopped = 1;

    stand = eval(b);
    if (stand >= beta) return beta;
    if (stand > alpha) alpha = stand;
    if (b->hisPly >= HIST_SIZE - 2) return alpha;

    deltaMargin = (g_brain == 0) ? W[W_S_DeltaMargin] : (optDelta ? 200 : 0);
    n  = genMoves(b, moves);
    us = b->stm;
    {                                           /* captures and promotions only */
        int k, m = 0;
        for (k = 0; k < n; k++) {
            int t = MV_TYPE(moves[k]);
            if (IS_CAP(t) || IS_PROMO(t)) moves[m++] = moves[k];
        }
        n = m;
    }
    scoreMoves(b, moves, scores, n, 0, info, 0, 0);

    for (i = 0; i < n; i++) {
        int score, type;
        pickNext(moves, scores, n, i);
        type = MV_TYPE(moves[i]);

        /* SEE: the rest are losing captures / under-promotions -> skip them */
        if (optSEE && scores[i] < 0) break;

        /* delta pruning: even winning this piece cannot lift us to alpha */
        if (deltaMargin > 0 && !IS_PROMO(type)) {
            int victim = (type == EP_CAPTURE) ? wP : b->board[MV_TO(moves[i])];
            if (stand + seeVal[victim] + deltaMargin <= alpha) continue;
        }

        makeMove(b, moves[i]);
        if (inCheck(b, us)) { undoMove(b); continue; }
        score = -quiesce(b, -beta, -alpha, info);
        undoMove(b);
        if (info->stopped) return 0;
        if (score >= beta) return beta;
        if (score > alpha) alpha = score;
    }
    return alpha;
}

static int alphabeta(BOARD* b, int depth, int ply, int alpha, int beta, SEARCH_INFO* info,
                     int allowNull) {
    MOVE moves[MAX_MOVES], ttMove = 0, bestMove = 0, counterMove = 0;
    MOVE quietsTried[MAX_MOVES];
    int scores[MAX_MOVES];
    int n, i, us, legal = 0, searched = 0, nQuiets = 0, best = -INF_SCORE;
    int checked, found = 0, ttScore, staticEval = EVAL_NONE, improving = 0;
    int alphaOrig;
    int isPV = (beta - alpha) > 1;

    if (info->stopped || ((info->nodes & 2047) == 0 && checkUp(info))) return 0;

    if (ply > 0) {
        if (isRepetition(b) || b->fiftyMove >= 100) return 0;
#ifndef K7_V1_EXACT
        if (insufficientMaterial(b)) return 0;

        /* mate distance pruning: a shorter mate was already found */
        if (alpha < -MATE_SCORE + ply)    alpha = -MATE_SCORE + ply;
        if (beta  >  MATE_SCORE - ply - 1) beta =  MATE_SCORE - ply - 1;
        if (alpha >= beta) return alpha;
#endif
        ttScore = ttProbe(b->hash, depth, ply, alpha, beta, &ttMove, &found);
        if (found && !isPV) return ttScore;
    }
    alphaOrig = alpha;

    checked = inCheck(b, b->stm);
    if (checked) depth++;

    if (depth <= 0) return quiesce(b, alpha, beta, info);

    info->nodes++;
    if (info->nodeLimit && info->nodes >= info->nodeLimit) info->stopped = 1;
    info->pvLen[ply] = ply;

    if (ply > MAX_PLY - 2 || b->hisPly >= HIST_SIZE - 2) return eval(b);

    /* static eval of this node; "improving" = better than 2 plies ago */
    if (!checked && (g_brain == 0 || optRFP || optNullPlus || optFutility || optLMP || optLMRPlus)) {
        staticEval = eval(b);
        improving  = ply >= 2 && info->evalStack[ply - 2] != EVAL_NONE
                     && staticEval > info->evalStack[ply - 2];
    }
    info->evalStack[ply] = staticEval;

    /* reverse futility pruning: far above beta, a quiet position stays there */
    {   /* nets: RFP box, margin 80; Homebrew: S_RFPMargin (0 = off) */
        int rfpM = (g_brain == 0) ? W[W_S_RFPMargin] : (optRFP ? 80 : 0);
        if (rfpM > 0 && !isPV && !checked && ply > 0 && depth <= 7
            && beta > -MATE_BOUND && beta < MATE_BOUND
            && staticEval - (rfpM * depth - (improving ? rfpM / 2 : 0)) >= beta)
            return staticEval;
    }

    /* null move: give the opponent a free move; still >= beta -> prune */
    if (!isPV && !checked && depth >= 3 && ply > 0 && hasNonPawnMaterial(b, b->stm)
        && (!optNullPlus || (allowNull && staticEval >= beta))) {
        int R = optNullPlus ? 3 + depth / 4 + ((staticEval - beta) / 200 < 3 ? (staticEval - beta) / 200 : 3)
                            : 2 + (depth > 6 ? 1 : 0);
        int nullScore;
        makeNullMove(b);
        nullScore = -alphabeta(b, depth - 1 - R, ply + 1, -beta, -beta + 1, info, 0);
        undoNullMove(b);
        if (info->stopped) return 0;
        if (nullScore >= beta && nullScore < MATE_BOUND) return beta;
    }

    /* internal iterative reduction: no hash move -> search a bit shallower */
    if (optIIR && depth >= 4 && !ttMove) depth--;

    /* counter-move: our known best reply to the opponent's last move */
    if (optOrdering && b->hisPly > 0) {
        MOVE prev = b->history[b->hisPly - 1].move;
        if (prev) counterMove = info->counter[b->board[MV_TO(prev)]][MV_TO(prev)];
    }

    n  = genMoves(b, moves);
    us = b->stm;
    scoreMoves(b, moves, scores, n, ttMove, info, ply, counterMove);

    for (i = 0; i < n; i++) {
        int score, type, quiet, reduction = 0, givesCheck, isKiller;
        MOVE m;

        pickNext(moves, scores, n, i);
        m = moves[i];
        type  = MV_TYPE(m);
        quiet = !IS_CAP(type) && !IS_PROMO(type);
        isKiller = (m == info->killers[ply][0] || m == info->killers[ply][1]);

        /* SEE pruning of clearly losing captures near the leaves */
        if (optSEE && !isPV && !checked && searched > 0 && best > -MATE_BOUND
            && depth <= 6 && IS_CAP(type) && scores[i] < 0 && see(b, m) < -100 * depth)
            continue;

        makeMove(b, m);
        if (inCheck(b, us)) { undoMove(b); continue; }
        legal++;
        givesCheck = inCheck(b, b->stm);

        /* prune hopeless quiet moves once one move has been searched */
        if (ply > 0 && quiet && !checked && !givesCheck && searched > 0 && best > -MATE_BOUND) {
            if (optLMP && depth <= 7
                && nQuiets >= (3 + depth * depth) / (improving ? 1 : 2)) {
                undoMove(b);
                continue;
            }
            if (depth <= 6 && !isKiller && staticEval != EVAL_NONE
                && (g_brain == 0
                    ? (W[W_S_FutilityMargin] > 0                  /* Homebrew: S_FutilityMargin */
                       && staticEval + W[W_S_FutilityMargin] + 100 * (depth - 1) <= alpha)
                    : (optFutility && staticEval + 110 + 100 * depth <= alpha))) {
                undoMove(b);
                continue;
            }
        }

        searched++;
        if (quiet && nQuiets < MAX_MOVES) quietsTried[nQuiets++] = m;

        if (searched == 1) {
            score = -alphabeta(b, depth - 1, ply + 1, -beta, -alpha, info, 1);
        } else {
            if (optLMRPlus) {
                if (depth >= 3 && !checked && (quiet || scores[i] < 0)) {
                    int hist = quiet ? info->history[b->board[MV_TO(m)]][MV_TO(m)] : 0;
                    reduction = lmrTable[depth < 64 ? depth : 63][searched < 64 ? searched : 63];
                    if (isPV)          reduction--;
                    if (isKiller || (counterMove && m == counterMove)) reduction--;
                    if (givesCheck)    reduction--;
                    if (!improving)    reduction++;
                    reduction -= hist / 6000;
                    if (reduction < 0) reduction = 0;
                    if (reduction > depth - 2) reduction = depth - 2;
                }
            } else if (depth >= 3 && searched > 3 && quiet && !checked && !givesCheck) {  /* V1 */
                reduction = 1;
                if (searched > 6) reduction++;
                if (depth > 6 && searched > 10) reduction++;
                if (reduction > depth - 2) reduction = depth - 2;
            }

            score = -alphabeta(b, depth - 1 - reduction, ply + 1, -alpha - 1, -alpha, info, 1);

            if (score > alpha && reduction > 0)
                score = -alphabeta(b, depth - 1, ply + 1, -alpha - 1, -alpha, info, 1);

            if (score > alpha && score < beta)
                score = -alphabeta(b, depth - 1, ply + 1, -beta, -alpha, info, 1);
        }

        undoMove(b);
        if (info->stopped) return 0;

        if (score > best) { best = score; bestMove = m; }
        if (score > alpha) {
            alpha = score;
            if (ply < MAX_PLY - 1) {
                int j;
                info->pv[ply][ply] = m;
                for (j = ply + 1; j < info->pvLen[ply + 1]; j++)
                    info->pv[ply][j] = info->pv[ply + 1][j];
                info->pvLen[ply] = info->pvLen[ply + 1];
            }
            if (alpha >= beta) {
                if (quiet && ply < MAX_PLY) {
                    int piece = b->board[MV_FROM(m)];
                    info->killers[ply][1] = info->killers[ply][0];
                    info->killers[ply][0] = m;
                    if (optOrdering) {
                        int bonus = depth * depth < 1200 ? depth * depth : 1200, q;
                        histAdd(&info->history[piece][MV_TO(m)], bonus);
                        for (q = 0; q < nQuiets - 1; q++)          /* tried before, failed */
                            histAdd(&info->history[b->board[MV_FROM(quietsTried[q])]][MV_TO(quietsTried[q])], -bonus);
                        if (b->hisPly > 0 && b->history[b->hisPly - 1].move) {
                            MOVE prev = b->history[b->hisPly - 1].move;
                            info->counter[b->board[MV_TO(prev)]][MV_TO(prev)] = m;
                        }
                    } else {
                        info->history[piece][MV_TO(m)] += depth * depth;   /* V1 */
                    }
                }
                ttStore(b->hash, depth, ply, beta, TT_BETA, m);
                return beta;
            }
        }
    }

    if (legal == 0) return checked ? (-MATE_SCORE + ply) : 0;
    if (searched == 0) return alpha;

    ttStore(b->hash, depth, ply, best, (alpha > alphaOrig) ? TT_EXACT : TT_ALPHA, bestMove);
    return best;
}

static void moveToStr(MOVE move, char* str) {
    int type = MV_TYPE(move);
    sq_to_str(MV_FROM(move), str);
    sq_to_str(MV_TO(move), str + 2);
    if (IS_PROMO(type)) { str[4] = "nbrq"[type & 3]; str[5] = '\0'; }
    else str[4] = '\0';
}

static MOVE searchPosition(BOARD* b, SEARCH_INFO* info) {
    MOVE rootMoves[MAX_MOVES], bestMove = 0;
    MOVE bestPv[MAX_PLY], lastPv[MAX_PLY];
    int nRoot, d, prevScore = 0, bestPvLen = 0, lastPvLen = 0;

    info->nodes = 0;
    info->stopped = 0;
    info->startTime = clock();
    memset(info->killers, 0, sizeof(info->killers));
    memset(info->history, 0, sizeof(info->history));
    memset(info->counter, 0, sizeof(info->counter));
    info->evalStack[0] = EVAL_NONE;

    nRoot = genLegal(b, rootMoves);
    if (nRoot == 0) return 0;
    bestMove = rootMoves[0];

    for (d = 1; d <= info->depth; d++) {
        MOVE iterBest = 0;
        int bestScore = -INF_SCORE;
        int window = (g_brain == 0) ? W[W_S_AspWindow] : 35, alpha, beta;
        double ms;

        if (d >= 4 && prevScore > -MATE_SCORE + MAX_PLY
                   && prevScore <  MATE_SCORE - MAX_PLY) {
            alpha = prevScore - window;
            beta  = prevScore + window;
        } else {
            alpha = -INF_SCORE;
            beta  =  INF_SCORE;
        }

        for (;;) {
            int i, curAlpha = alpha;
            iterBest = 0;
            bestScore = -INF_SCORE;
            bestPvLen = 0;

            sortMoves(b, rootMoves, nRoot, bestMove, info, 0);

            for (i = 0; i < nRoot; i++) {
                int score;
                double el = (double)(clock() - info->startTime) * 1000.0 / CLOCKS_PER_SEC;

                if (el > 400.0 && !g_silent) {
                    char cm[6];
                    moveToStr(rootMoves[i], cm);
                    say("info depth %d currmove %s currmovenumber %d\n",
                           d, cm, i + 1);
                    fflush(stdout);
                }

                makeMove(b, rootMoves[i]);
                score = -alphabeta(b, d - 1, 1, -beta, -curAlpha, info, 1);
                undoMove(b);
                if (info->stopped) break;
                if (score > bestScore) {
                    int j;
                    bestScore = score;
                    iterBest = rootMoves[i];
                    bestPvLen = 1;
                    bestPv[0] = rootMoves[i];
                    for (j = 1; j < info->pvLen[1] && j < MAX_PLY - 1; j++)
                        bestPv[bestPvLen++] = info->pv[1][j];
                }
                if (score > curAlpha) curAlpha = score;
            }

            if (info->stopped) break;

            if (bestScore <= alpha) {
                window *= 3;
                alpha = bestScore - window;
                if (alpha < -INF_SCORE) alpha = -INF_SCORE;
                continue;
            }
            if (bestScore >= beta) {
                window *= 3;
                beta = bestScore + window;
                if (beta > INF_SCORE) beta = INF_SCORE;
                continue;
            }
            break;
        }

        if (info->stopped && iterBest == 0) break;
        if (iterBest) bestMove = iterBest;
        prevScore = bestScore;

        ms = (double)(clock() - info->startTime) * 1000.0 / CLOCKS_PER_SEC;
        if (!g_silent) {
            long long nps = (long long)(info->nodes / ((ms / 1000.0) + 0.001));
            char buf[8];
            int k;

            if (bestPvLen > 0) {
                lastPvLen = bestPvLen;
                for (k = 0; k < bestPvLen; k++) lastPv[k] = bestPv[k];
            }

            say("info depth %d ", d);
            if (bestScore > MATE_SCORE - MAX_PLY)
                say("score mate %d ", (MATE_SCORE - bestScore + 1) / 2);
            else if (bestScore < -MATE_SCORE + MAX_PLY)
                say("score mate %d ", -((MATE_SCORE + bestScore + 1) / 2));
            else
                say("score cp %d ", bestScore);

            say("nodes %lld nps %lld time %d hashfull %d pv",
                   info->nodes, nps, (int)ms, ttHashfull());
            for (k = 0; k < lastPvLen; k++) {
                moveToStr(lastPv[k], buf);
                say(" %s", buf);
            }
            say("\n");
            fflush(stdout);
        }
        if (bestScore > MATE_SCORE - MAX_PLY) break;

        if (info->stopped) break;
        if (info->timeLimit > 0 && ms > info->timeLimit * 0.5) break;
    }
    return bestMove;
}

/* ------------------------------------------------------------------ UCI */

static MOVE strToMove(BOARD* b, const char* str) {
    MOVE moves[MAX_MOVES];
    int n, i, from, to;

    if (strlen(str) < 4) return 0;
    from = str_to_sq(str);
    to   = str_to_sq(str + 2);
    if (from < 0 || to < 0) return 0;

    n = genLegal(b, moves);
    for (i = 0; i < n; i++) {
        if ((int)MV_FROM(moves[i]) != from || (int)MV_TO(moves[i]) != to) continue;
        if (IS_PROMO(MV_TYPE(moves[i]))) {
            char want = (strlen(str) > 4) ? (char)tolower((unsigned char)str[4]) : 'q';
            if ("nbrq"[MV_TYPE(moves[i]) & 3] != want) continue;
        }
        return moves[i];
    }
    return 0;
}

static int strieq(const char* a, const char* b) {
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
        a++; b++;
    }
    return *a == *b;
}

static void trim(char* s) {
    size_t n = strlen(s), i = 0;
    while (n && isspace((unsigned char)s[n - 1])) s[--n] = 0;
    while (s[i] && isspace((unsigned char)s[i])) i++;
    if (i) memmove(s, s + i, n - i + 1);
}

/* ================================================================
 *  OPENING BOOK  (Polyglot .bin -- k7Opennings.bin is built in)
 *  Entry = 16 bytes big-endian: key 8, move 2, weight 2, learn 4.
 *  Sorted by key, so a binary search finds a position's moves.
 * ================================================================ */
static const U64 PG_RANDOM[781] = {
    0x9D39247E33776D41ULL, 0x2AF7398005AAA5C7ULL, 0x44DB015024623547ULL,
    0x9C15F73E62A76AE2ULL, 0x75834465489C0C89ULL, 0x3290AC3A203001BFULL,
    0x0FBBAD1F61042279ULL, 0xE83A908FF2FB60CAULL, 0x0D7E765D58755C10ULL,
    0x1A083822CEAFE02DULL, 0x9605D5F0E25EC3B0ULL, 0xD021FF5CD13A2ED5ULL,
    0x40BDF15D4A672E32ULL, 0x011355146FD56395ULL, 0x5DB4832046F3D9E5ULL,
    0x239F8B2D7FF719CCULL, 0x05D1A1AE85B49AA1ULL, 0x679F848F6E8FC971ULL,
    0x7449BBFF801FED0BULL, 0x7D11CDB1C3B7ADF0ULL, 0x82C7709E781EB7CCULL,
    0xF3218F1C9510786CULL, 0x331478F3AF51BBE6ULL, 0x4BB38DE5E7219443ULL,
    0xAA649C6EBCFD50FCULL, 0x8DBD98A352AFD40BULL, 0x87D2074B81D79217ULL,
    0x19F3C751D3E92AE1ULL, 0xB4AB30F062B19ABFULL, 0x7B0500AC42047AC4ULL,
    0xC9452CA81A09D85DULL, 0x24AA6C514DA27500ULL, 0x4C9F34427501B447ULL,
    0x14A68FD73C910841ULL, 0xA71B9B83461CBD93ULL, 0x03488B95B0F1850FULL,
    0x637B2B34FF93C040ULL, 0x09D1BC9A3DD90A94ULL, 0x3575668334A1DD3BULL,
    0x735E2B97A4C45A23ULL, 0x18727070F1BD400BULL, 0x1FCBACD259BF02E7ULL,
    0xD310A7C2CE9B6555ULL, 0xBF983FE0FE5D8244ULL, 0x9F74D14F7454A824ULL,
    0x51EBDC4AB9BA3035ULL, 0x5C82C505DB9AB0FAULL, 0xFCF7FE8A3430B241ULL,
    0x3253A729B9BA3DDEULL, 0x8C74C368081B3075ULL, 0xB9BC6C87167C33E7ULL,
    0x7EF48F2B83024E20ULL, 0x11D505D4C351BD7FULL, 0x6568FCA92C76A243ULL,
    0x4DE0B0F40F32A7B8ULL, 0x96D693460CC37E5DULL, 0x42E240CB63689F2FULL,
    0x6D2BDCDAE2919661ULL, 0x42880B0236E4D951ULL, 0x5F0F4A5898171BB6ULL,
    0x39F890F579F92F88ULL, 0x93C5B5F47356388BULL, 0x63DC359D8D231B78ULL,
    0xEC16CA8AEA98AD76ULL, 0x5355F900C2A82DC7ULL, 0x07FB9F855A997142ULL,
    0x5093417AA8A7ED5EULL, 0x7BCBC38DA25A7F3CULL, 0x19FC8A768CF4B6D4ULL,
    0x637A7780DECFC0D9ULL, 0x8249A47AEE0E41F7ULL, 0x79AD695501E7D1E8ULL,
    0x14ACBAF4777D5776ULL, 0xF145B6BECCDEA195ULL, 0xDABF2AC8201752FCULL,
    0x24C3C94DF9C8D3F6ULL, 0xBB6E2924F03912EAULL, 0x0CE26C0B95C980D9ULL,
    0xA49CD132BFBF7CC4ULL, 0xE99D662AF4243939ULL, 0x27E6AD7891165C3FULL,
    0x8535F040B9744FF1ULL, 0x54B3F4FA5F40D873ULL, 0x72B12C32127FED2BULL,
    0xEE954D3C7B411F47ULL, 0x9A85AC909A24EAA1ULL, 0x70AC4CD9F04F21F5ULL,
    0xF9B89D3E99A075C2ULL, 0x87B3E2B2B5C907B1ULL, 0xA366E5B8C54F48B8ULL,
    0xAE4A9346CC3F7CF2ULL, 0x1920C04D47267BBDULL, 0x87BF02C6B49E2AE9ULL,
    0x092237AC237F3859ULL, 0xFF07F64EF8ED14D0ULL, 0x8DE8DCA9F03CC54EULL,
    0x9C1633264DB49C89ULL, 0xB3F22C3D0B0B38EDULL, 0x390E5FB44D01144BULL,
    0x5BFEA5B4712768E9ULL, 0x1E1032911FA78984ULL, 0x9A74ACB964E78CB3ULL,
    0x4F80F7A035DAFB04ULL, 0x6304D09A0B3738C4ULL, 0x2171E64683023A08ULL,
    0x5B9B63EB9CEFF80CULL, 0x506AACF489889342ULL, 0x1881AFC9A3A701D6ULL,
    0x6503080440750644ULL, 0xDFD395339CDBF4A7ULL, 0xEF927DBCF00C20F2ULL,
    0x7B32F7D1E03680ECULL, 0xB9FD7620E7316243ULL, 0x05A7E8A57DB91B77ULL,
    0xB5889C6E15630A75ULL, 0x4A750A09CE9573F7ULL, 0xCF464CEC899A2F8AULL,
    0xF538639CE705B824ULL, 0x3C79A0FF5580EF7FULL, 0xEDE6C87F8477609DULL,
    0x799E81F05BC93F31ULL, 0x86536B8CF3428A8CULL, 0x97D7374C60087B73ULL,
    0xA246637CFF328532ULL, 0x043FCAE60CC0EBA0ULL, 0x920E449535DD359EULL,
    0x70EB093B15B290CCULL, 0x73A1921916591CBDULL, 0x56436C9FE1A1AA8DULL,
    0xEFAC4B70633B8F81ULL, 0xBB215798D45DF7AFULL, 0x45F20042F24F1768ULL,
    0x930F80F4E8EB7462ULL, 0xFF6712FFCFD75EA1ULL, 0xAE623FD67468AA70ULL,
    0xDD2C5BC84BC8D8FCULL, 0x7EED120D54CF2DD9ULL, 0x22FE545401165F1CULL,
    0xC91800E98FB99929ULL, 0x808BD68E6AC10365ULL, 0xDEC468145B7605F6ULL,
    0x1BEDE3A3AEF53302ULL, 0x43539603D6C55602ULL, 0xAA969B5C691CCB7AULL,
    0xA87832D392EFEE56ULL, 0x65942C7B3C7E11AEULL, 0xDED2D633CAD004F6ULL,
    0x21F08570F420E565ULL, 0xB415938D7DA94E3CULL, 0x91B859E59ECB6350ULL,
    0x10CFF333E0ED804AULL, 0x28AED140BE0BB7DDULL, 0xC5CC1D89724FA456ULL,
    0x5648F680F11A2741ULL, 0x2D255069F0B7DAB3ULL, 0x9BC5A38EF729ABD4ULL,
    0xEF2F054308F6A2BCULL, 0xAF2042F5CC5C2858ULL, 0x480412BAB7F5BE2AULL,
    0xAEF3AF4A563DFE43ULL, 0x19AFE59AE451497FULL, 0x52593803DFF1E840ULL,
    0xF4F076E65F2CE6F0ULL, 0x11379625747D5AF3ULL, 0xBCE5D2248682C115ULL,
    0x9DA4243DE836994FULL, 0x066F70B33FE09017ULL, 0x4DC4DE189B671A1CULL,
    0x51039AB7712457C3ULL, 0xC07A3F80C31FB4B4ULL, 0xB46EE9C5E64A6E7CULL,
    0xB3819A42ABE61C87ULL, 0x21A007933A522A20ULL, 0x2DF16F761598AA4FULL,
    0x763C4A1371B368FDULL, 0xF793C46702E086A0ULL, 0xD7288E012AEB8D31ULL,
    0xDE336A2A4BC1C44BULL, 0x0BF692B38D079F23ULL, 0x2C604A7A177326B3ULL,
    0x4850E73E03EB6064ULL, 0xCFC447F1E53C8E1BULL, 0xB05CA3F564268D99ULL,
    0x9AE182C8BC9474E8ULL, 0xA4FC4BD4FC5558CAULL, 0xE755178D58FC4E76ULL,
    0x69B97DB1A4C03DFEULL, 0xF9B5B7C4ACC67C96ULL, 0xFC6A82D64B8655FBULL,
    0x9C684CB6C4D24417ULL, 0x8EC97D2917456ED0ULL, 0x6703DF9D2924E97EULL,
    0xC547F57E42A7444EULL, 0x78E37644E7CAD29EULL, 0xFE9A44E9362F05FAULL,
    0x08BD35CC38336615ULL, 0x9315E5EB3A129ACEULL, 0x94061B871E04DF75ULL,
    0xDF1D9F9D784BA010ULL, 0x3BBA57B68871B59DULL, 0xD2B7ADEEDED1F73FULL,
    0xF7A255D83BC373F8ULL, 0xD7F4F2448C0CEB81ULL, 0xD95BE88CD210FFA7ULL,
    0x336F52F8FF4728E7ULL, 0xA74049DAC312AC71ULL, 0xA2F61BB6E437FDB5ULL,
    0x4F2A5CB07F6A35B3ULL, 0x87D380BDA5BF7859ULL, 0x16B9F7E06C453A21ULL,
    0x7BA2484C8A0FD54EULL, 0xF3A678CAD9A2E38CULL, 0x39B0BF7DDE437BA2ULL,
    0xFCAF55C1BF8A4424ULL, 0x18FCF680573FA594ULL, 0x4C0563B89F495AC3ULL,
    0x40E087931A00930DULL, 0x8CFFA9412EB642C1ULL, 0x68CA39053261169FULL,
    0x7A1EE967D27579E2ULL, 0x9D1D60E5076F5B6FULL, 0x3810E399B6F65BA2ULL,
    0x32095B6D4AB5F9B1ULL, 0x35CAB62109DD038AULL, 0xA90B24499FCFAFB1ULL,
    0x77A225A07CC2C6BDULL, 0x513E5E634C70E331ULL, 0x4361C0CA3F692F12ULL,
    0xD941ACA44B20A45BULL, 0x528F7C8602C5807BULL, 0x52AB92BEB9613989ULL,
    0x9D1DFA2EFC557F73ULL, 0x722FF175F572C348ULL, 0x1D1260A51107FE97ULL,
    0x7A249A57EC0C9BA2ULL, 0x04208FE9E8F7F2D6ULL, 0x5A110C6058B920A0ULL,
    0x0CD9A497658A5698ULL, 0x56FD23C8F9715A4CULL, 0x284C847B9D887AAEULL,
    0x04FEABFBBDB619CBULL, 0x742E1E651C60BA83ULL, 0x9A9632E65904AD3CULL,
    0x881B82A13B51B9E2ULL, 0x506E6744CD974924ULL, 0xB0183DB56FFC6A79ULL,
    0x0ED9B915C66ED37EULL, 0x5E11E86D5873D484ULL, 0xF678647E3519AC6EULL,
    0x1B85D488D0F20CC5ULL, 0xDAB9FE6525D89021ULL, 0x0D151D86ADB73615ULL,
    0xA865A54EDCC0F019ULL, 0x93C42566AEF98FFBULL, 0x99E7AFEABE000731ULL,
    0x48CBFF086DDF285AULL, 0x7F9B6AF1EBF78BAFULL, 0x58627E1A149BBA21ULL,
    0x2CD16E2ABD791E33ULL, 0xD363EFF5F0977996ULL, 0x0CE2A38C344A6EEDULL,
    0x1A804AADB9CFA741ULL, 0x907F30421D78C5DEULL, 0x501F65EDB3034D07ULL,
    0x37624AE5A48FA6E9ULL, 0x957BAF61700CFF4EULL, 0x3A6C27934E31188AULL,
    0xD49503536ABCA345ULL, 0x088E049589C432E0ULL, 0xF943AEE7FEBF21B8ULL,
    0x6C3B8E3E336139D3ULL, 0x364F6FFA464EE52EULL, 0xD60F6DCEDC314222ULL,
    0x56963B0DCA418FC0ULL, 0x16F50EDF91E513AFULL, 0xEF1955914B609F93ULL,
    0x565601C0364E3228ULL, 0xECB53939887E8175ULL, 0xBAC7A9A18531294BULL,
    0xB344C470397BBA52ULL, 0x65D34954DAF3CEBDULL, 0xB4B81B3FA97511E2ULL,
    0xB422061193D6F6A7ULL, 0x071582401C38434DULL, 0x7A13F18BBEDC4FF5ULL,
    0xBC4097B116C524D2ULL, 0x59B97885E2F2EA28ULL, 0x99170A5DC3115544ULL,
    0x6F423357E7C6A9F9ULL, 0x325928EE6E6F8794ULL, 0xD0E4366228B03343ULL,
    0x565C31F7DE89EA27ULL, 0x30F5611484119414ULL, 0xD873DB391292ED4FULL,
    0x7BD94E1D8E17DEBCULL, 0xC7D9F16864A76E94ULL, 0x947AE053EE56E63CULL,
    0xC8C93882F9475F5FULL, 0x3A9BF55BA91F81CAULL, 0xD9A11FBB3D9808E4ULL,
    0x0FD22063EDC29FCAULL, 0xB3F256D8ACA0B0B9ULL, 0xB03031A8B4516E84ULL,
    0x35DD37D5871448AFULL, 0xE9F6082B05542E4EULL, 0xEBFAFA33D7254B59ULL,
    0x9255ABB50D532280ULL, 0xB9AB4CE57F2D34F3ULL, 0x693501D628297551ULL,
    0xC62C58F97DD949BFULL, 0xCD454F8F19C5126AULL, 0xBBE83F4ECC2BDECBULL,
    0xDC842B7E2819E230ULL, 0xBA89142E007503B8ULL, 0xA3BC941D0A5061CBULL,
    0xE9F6760E32CD8021ULL, 0x09C7E552BC76492FULL, 0x852F54934DA55CC9ULL,
    0x8107FCCF064FCF56ULL, 0x098954D51FFF6580ULL, 0x23B70EDB1955C4BFULL,
    0xC330DE426430F69DULL, 0x4715ED43E8A45C0AULL, 0xA8D7E4DAB780A08DULL,
    0x0572B974F03CE0BBULL, 0xB57D2E985E1419C7ULL, 0xE8D9ECBE2CF3D73FULL,
    0x2FE4B17170E59750ULL, 0x11317BA87905E790ULL, 0x7FBF21EC8A1F45ECULL,
    0x1725CABFCB045B00ULL, 0x964E915CD5E2B207ULL, 0x3E2B8BCBF016D66DULL,
    0xBE7444E39328A0ACULL, 0xF85B2B4FBCDE44B7ULL, 0x49353FEA39BA63B1ULL,
    0x1DD01AAFCD53486AULL, 0x1FCA8A92FD719F85ULL, 0xFC7C95D827357AFAULL,
    0x18A6A990C8B35EBDULL, 0xCCCB7005C6B9C28DULL, 0x3BDBB92C43B17F26ULL,
    0xAA70B5B4F89695A2ULL, 0xE94C39A54A98307FULL, 0xB7A0B174CFF6F36EULL,
    0xD4DBA84729AF48ADULL, 0x2E18BC1AD9704A68ULL, 0x2DE0966DAF2F8B1CULL,
    0xB9C11D5B1E43A07EULL, 0x64972D68DEE33360ULL, 0x94628D38D0C20584ULL,
    0xDBC0D2B6AB90A559ULL, 0xD2733C4335C6A72FULL, 0x7E75D99D94A70F4DULL,
    0x6CED1983376FA72BULL, 0x97FCAACBF030BC24ULL, 0x7B77497B32503B12ULL,
    0x8547EDDFB81CCB94ULL, 0x79999CDFF70902CBULL, 0xCFFE1939438E9B24ULL,
    0x829626E3892D95D7ULL, 0x92FAE24291F2B3F1ULL, 0x63E22C147B9C3403ULL,
    0xC678B6D860284A1CULL, 0x5873888850659AE7ULL, 0x0981DCD296A8736DULL,
    0x9F65789A6509A440ULL, 0x9FF38FED72E9052FULL, 0xE479EE5B9930578CULL,
    0xE7F28ECD2D49EECDULL, 0x56C074A581EA17FEULL, 0x5544F7D774B14AEFULL,
    0x7B3F0195FC6F290FULL, 0x12153635B2C0CF57ULL, 0x7F5126DBBA5E0CA7ULL,
    0x7A76956C3EAFB413ULL, 0x3D5774A11D31AB39ULL, 0x8A1B083821F40CB4ULL,
    0x7B4A38E32537DF62ULL, 0x950113646D1D6E03ULL, 0x4DA8979A0041E8A9ULL,
    0x3BC36E078F7515D7ULL, 0x5D0A12F27AD310D1ULL, 0x7F9D1A2E1EBE1327ULL,
    0xDA3A361B1C5157B1ULL, 0xDCDD7D20903D0C25ULL, 0x36833336D068F707ULL,
    0xCE68341F79893389ULL, 0xAB9090168DD05F34ULL, 0x43954B3252DC25E5ULL,
    0xB438C2B67F98E5E9ULL, 0x10DCD78E3851A492ULL, 0xDBC27AB5447822BFULL,
    0x9B3CDB65F82CA382ULL, 0xB67B7896167B4C84ULL, 0xBFCED1B0048EAC50ULL,
    0xA9119B60369FFEBDULL, 0x1FFF7AC80904BF45ULL, 0xAC12FB171817EEE7ULL,
    0xAF08DA9177DDA93DULL, 0x1B0CAB936E65C744ULL, 0xB559EB1D04E5E932ULL,
    0xC37B45B3F8D6F2BAULL, 0xC3A9DC228CAAC9E9ULL, 0xF3B8B6675A6507FFULL,
    0x9FC477DE4ED681DAULL, 0x67378D8ECCEF96CBULL, 0x6DD856D94D259236ULL,
    0xA319CE15B0B4DB31ULL, 0x073973751F12DD5EULL, 0x8A8E849EB32781A5ULL,
    0xE1925C71285279F5ULL, 0x74C04BF1790C0EFEULL, 0x4DDA48153C94938AULL,
    0x9D266D6A1CC0542CULL, 0x7440FB816508C4FEULL, 0x13328503DF48229FULL,
    0xD6BF7BAEE43CAC40ULL, 0x4838D65F6EF6748FULL, 0x1E152328F3318DEAULL,
    0x8F8419A348F296BFULL, 0x72C8834A5957B511ULL, 0xD7A023A73260B45CULL,
    0x94EBC8ABCFB56DAEULL, 0x9FC10D0F989993E0ULL, 0xDE68A2355B93CAE6ULL,
    0xA44CFE79AE538BBEULL, 0x9D1D84FCCE371425ULL, 0x51D2B1AB2DDFB636ULL,
    0x2FD7E4B9E72CD38CULL, 0x65CA5B96B7552210ULL, 0xDD69A0D8AB3B546DULL,
    0x604D51B25FBF70E2ULL, 0x73AA8A564FB7AC9EULL, 0x1A8C1E992B941148ULL,
    0xAAC40A2703D9BEA0ULL, 0x764DBEAE7FA4F3A6ULL, 0x1E99B96E70A9BE8BULL,
    0x2C5E9DEB57EF4743ULL, 0x3A938FEE32D29981ULL, 0x26E6DB8FFDF5ADFEULL,
    0x469356C504EC9F9DULL, 0xC8763C5B08D1908CULL, 0x3F6C6AF859D80055ULL,
    0x7F7CC39420A3A545ULL, 0x9BFB227EBDF4C5CEULL, 0x89039D79D6FC5C5CULL,
    0x8FE88B57305E2AB6ULL, 0xA09E8C8C35AB96DEULL, 0xFA7E393983325753ULL,
    0xD6B6D0ECC617C699ULL, 0xDFEA21EA9E7557E3ULL, 0xB67C1FA481680AF8ULL,
    0xCA1E3785A9E724E5ULL, 0x1CFC8BED0D681639ULL, 0xD18D8549D140CAEAULL,
    0x4ED0FE7E9DC91335ULL, 0xE4DBF0634473F5D2ULL, 0x1761F93A44D5AEFEULL,
    0x53898E4C3910DA55ULL, 0x734DE8181F6EC39AULL, 0x2680B122BAA28D97ULL,
    0x298AF231C85BAFABULL, 0x7983EED3740847D5ULL, 0x66C1A2A1A60CD889ULL,
    0x9E17E49642A3E4C1ULL, 0xEDB454E7BADC0805ULL, 0x50B704CAB602C329ULL,
    0x4CC317FB9CDDD023ULL, 0x66B4835D9EAFEA22ULL, 0x219B97E26FFC81BDULL,
    0x261E4E4C0A333A9DULL, 0x1FE2CCA76517DB90ULL, 0xD7504DFA8816EDBBULL,
    0xB9571FA04DC089C8ULL, 0x1DDC0325259B27DEULL, 0xCF3F4688801EB9AAULL,
    0xF4F5D05C10CAB243ULL, 0x38B6525C21A42B0EULL, 0x36F60E2BA4FA6800ULL,
    0xEB3593803173E0CEULL, 0x9C4CD6257C5A3603ULL, 0xAF0C317D32ADAA8AULL,
    0x258E5A80C7204C4BULL, 0x8B889D624D44885DULL, 0xF4D14597E660F855ULL,
    0xD4347F66EC8941C3ULL, 0xE699ED85B0DFB40DULL, 0x2472F6207C2D0484ULL,
    0xC2A1E7B5B459AEB5ULL, 0xAB4F6451CC1D45ECULL, 0x63767572AE3D6174ULL,
    0xA59E0BD101731A28ULL, 0x116D0016CB948F09ULL, 0x2CF9C8CA052F6E9FULL,
    0x0B090A7560A968E3ULL, 0xABEEDDB2DDE06FF1ULL, 0x58EFC10B06A2068DULL,
    0xC6E57A78FBD986E0ULL, 0x2EAB8CA63CE802D7ULL, 0x14A195640116F336ULL,
    0x7C0828DD624EC390ULL, 0xD74BBE77E6116AC7ULL, 0x804456AF10F5FB53ULL,
    0xEBE9EA2ADF4321C7ULL, 0x03219A39EE587A30ULL, 0x49787FEF17AF9924ULL,
    0xA1E9300CD8520548ULL, 0x5B45E522E4B1B4EFULL, 0xB49C3B3995091A36ULL,
    0xD4490AD526F14431ULL, 0x12A8F216AF9418C2ULL, 0x001F837CC7350524ULL,
    0x1877B51E57A764D5ULL, 0xA2853B80F17F58EEULL, 0x993E1DE72D36D310ULL,
    0xB3598080CE64A656ULL, 0x252F59CF0D9F04BBULL, 0xD23C8E176D113600ULL,
    0x1BDA0492E7E4586EULL, 0x21E0BD5026C619BFULL, 0x3B097ADAF088F94EULL,
    0x8D14DEDB30BE846EULL, 0xF95CFFA23AF5F6F4ULL, 0x3871700761B3F743ULL,
    0xCA672B91E9E4FA16ULL, 0x64C8E531BFF53B55ULL, 0x241260ED4AD1E87DULL,
    0x106C09B972D2E822ULL, 0x7FBA195410E5CA30ULL, 0x7884D9BC6CB569D8ULL,
    0x0647DFEDCD894A29ULL, 0x63573FF03E224774ULL, 0x4FC8E9560F91B123ULL,
    0x1DB956E450275779ULL, 0xB8D91274B9E9D4FBULL, 0xA2EBEE47E2FBFCE1ULL,
    0xD9F1F30CCD97FB09ULL, 0xEFED53D75FD64E6BULL, 0x2E6D02C36017F67FULL,
    0xA9AA4D20DB084E9BULL, 0xB64BE8D8B25396C1ULL, 0x70CB6AF7C2D5BCF0ULL,
    0x98F076A4F7A2322EULL, 0xBF84470805E69B5FULL, 0x94C3251F06F90CF3ULL,
    0x3E003E616A6591E9ULL, 0xB925A6CD0421AFF3ULL, 0x61BDD1307C66E300ULL,
    0xBF8D5108E27E0D48ULL, 0x240AB57A8B888B20ULL, 0xFC87614BAF287E07ULL,
    0xEF02CDD06FFDB432ULL, 0xA1082C0466DF6C0AULL, 0x8215E577001332C8ULL,
    0xD39BB9C3A48DB6CFULL, 0x2738259634305C14ULL, 0x61CF4F94C97DF93DULL,
    0x1B6BACA2AE4E125BULL, 0x758F450C88572E0BULL, 0x959F587D507A8359ULL,
    0xB063E962E045F54DULL, 0x60E8ED72C0DFF5D1ULL, 0x7B64978555326F9FULL,
    0xFD080D236DA814BAULL, 0x8C90FD9B083F4558ULL, 0x106F72FE81E2C590ULL,
    0x7976033A39F7D952ULL, 0xA4EC0132764CA04BULL, 0x733EA705FAE4FA77ULL,
    0xB4D8F77BC3E56167ULL, 0x9E21F4F903B33FD9ULL, 0x9D765E419FB69F6DULL,
    0xD30C088BA61EA5EFULL, 0x5D94337FBFAF7F5BULL, 0x1A4E4822EB4D7A59ULL,
    0x6FFE73E81B637FB3ULL, 0xDDF957BC36D8B9CAULL, 0x64D0E29EEA8838B3ULL,
    0x08DD9BDFD96B9F63ULL, 0x087E79E5A57D1D13ULL, 0xE328E230E3E2B3FBULL,
    0x1C2559E30F0946BEULL, 0x720BF5F26F4D2EAAULL, 0xB0774D261CC609DBULL,
    0x443F64EC5A371195ULL, 0x4112CF68649A260EULL, 0xD813F2FAB7F5C5CAULL,
    0x660D3257380841EEULL, 0x59AC2C7873F910A3ULL, 0xE846963877671A17ULL,
    0x93B633ABFA3469F8ULL, 0xC0C0F5A60EF4CDCFULL, 0xCAF21ECD4377B28CULL,
    0x57277707199B8175ULL, 0x506C11B9D90E8B1DULL, 0xD83CC2687A19255FULL,
    0x4A29C6465A314CD1ULL, 0xED2DF21216235097ULL, 0xB5635C95FF7296E2ULL,
    0x22AF003AB672E811ULL, 0x52E762596BF68235ULL, 0x9AEBA33AC6ECC6B0ULL,
    0x944F6DE09134DFB6ULL, 0x6C47BEC883A7DE39ULL, 0x6AD047C430A12104ULL,
    0xA5B1CFDBA0AB4067ULL, 0x7C45D833AFF07862ULL, 0x5092EF950A16DA0BULL,
    0x9338E69C052B8E7BULL, 0x455A4B4CFE30E3F5ULL, 0x6B02E63195AD0CF8ULL,
    0x6B17B224BAD6BF27ULL, 0xD1E0CCD25BB9C169ULL, 0xDE0C89A556B9AE70ULL,
    0x50065E535A213CF6ULL, 0x9C1169FA2777B874ULL, 0x78EDEFD694AF1EEDULL,
    0x6DC93D9526A50E68ULL, 0xEE97F453F06791EDULL, 0x32AB0EDB696703D3ULL,
    0x3A6853C7E70757A7ULL, 0x31865CED6120F37DULL, 0x67FEF95D92607890ULL,
    0x1F2B1D1F15F6DC9CULL, 0xB69E38A8965C6B65ULL, 0xAA9119FF184CCCF4ULL,
    0xF43C732873F24C13ULL, 0xFB4A3D794A9A80D2ULL, 0x3550C2321FD6109CULL,
    0x371F77E76BB8417EULL, 0x6BFA9AAE5EC05779ULL, 0xCD04F3FF001A4778ULL,
    0xE3273522064480CAULL, 0x9F91508BFFCFC14AULL, 0x049A7F41061A9E60ULL,
    0xFCB6BE43A9F2FE9BULL, 0x08DE8A1C7797DA9BULL, 0x8F9887E6078735A1ULL,
    0xB5B4071DBFC73A66ULL, 0x230E343DFBA08D33ULL, 0x43ED7F5A0FAE657DULL,
    0x3A88A0FBBCB05C63ULL, 0x21874B8B4D2DBC4FULL, 0x1BDEA12E35F6A8C9ULL,
    0x53C065C6C8E63528ULL, 0xE34A1D250E7A8D6BULL, 0xD6B04D3B7651DD7EULL,
    0x5E90277E7CB39E2DULL, 0x2C046F22062DC67DULL, 0xB10BB459132D0A26ULL,
    0x3FA9DDFB67E2F199ULL, 0x0E09B88E1914F7AFULL, 0x10E8B35AF3EEAB37ULL,
    0x9EEDECA8E272B933ULL, 0xD4C718BC4AE8AE5FULL, 0x81536D601170FC20ULL,
    0x91B534F885818A06ULL, 0xEC8177F83F900978ULL, 0x190E714FADA5156EULL,
    0xB592BF39B0364963ULL, 0x89C350C893AE7DC1ULL, 0xAC042E70F8B383F2ULL,
    0xB49B52E587A1EE60ULL, 0xFB152FE3FF26DA89ULL, 0x3E666E6F69AE2C15ULL,
    0x3B544EBE544C19F9ULL, 0xE805A1E290CF2456ULL, 0x24B33C9D7ED25117ULL,
    0xE74733427B72F0C1ULL, 0x0A804D18B7097475ULL, 0x57E3306D881EDB4FULL,
    0x4AE7D6A36EB5DBCBULL, 0x2D8D5432157064C8ULL, 0xD1E649DE1E7F268BULL,
    0x8A328A1CEDFE552CULL, 0x07A3AEC79624C7DAULL, 0x84547DDC3E203C94ULL,
    0x990A98FD5071D263ULL, 0x1A4FF12616EEFC89ULL, 0xF6F7FD1431714200ULL,
    0x30C05B1BA332F41CULL, 0x8D2636B81555A786ULL, 0x46C9FEB55D120902ULL,
    0xCCEC0A73B49C9921ULL, 0x4E9D2827355FC492ULL, 0x19EBB029435DCB0FULL,
    0x4659D2B743848A2CULL, 0x963EF2C96B33BE31ULL, 0x74F85198B05A2E7DULL,
    0x5A0F544DD2B1FB18ULL, 0x03727073C2E134B1ULL, 0xC7F6AA2DE59AEA61ULL,
    0x352787BAA0D7C22FULL, 0x9853EAB63B5E0B35ULL, 0xABBDCDD7ED5C0860ULL,
    0xCF05DAF5AC8D77B0ULL, 0x49CAD48CEBF4A71EULL, 0x7A4C10EC2158C4A6ULL,
    0xD9E92AA246BF719EULL, 0x13AE978D09FE5557ULL, 0x730499AF921549FFULL,
    0x4E4B705B92903BA4ULL, 0xFF577222C14F0A3AULL, 0x55B6344CF97AAFAEULL,
    0xB862225B055B6960ULL, 0xCAC09AFBDDD2CDB4ULL, 0xDAF8E9829FE96B5FULL,
    0xB5FDFC5D3132C498ULL, 0x310CB380DB6F7503ULL, 0xE87FBB46217A360EULL,
    0x2102AE466EBB1148ULL, 0xF8549E1A3AA5E00DULL, 0x07A69AFDCC42261AULL,
    0xC4C118BFE78FEAAEULL, 0xF9F4892ED96BD438ULL, 0x1AF3DBE25D8F45DAULL,
    0xF5B4B0B0D2DEEEB4ULL, 0x962ACEEFA82E1C84ULL, 0x046E3ECAAF453CE9ULL,
    0xF05D129681949A4CULL, 0x964781CE734B3C84ULL, 0x9C2ED44081CE5FBDULL,
    0x522E23F3925E319EULL, 0x177E00F9FC32F791ULL, 0x2BC60A63A6F3B3F2ULL,
    0x222BBFAE61725606ULL, 0x486289DDCC3D6780ULL, 0x7DC7785B8EFDFC80ULL,
    0x8AF38731C02BA980ULL, 0x1FAB64EA29A2DDF7ULL, 0xE4D9429322CD065AULL,
    0x9DA058C67844F20CULL, 0x24C0E332B70019B0ULL, 0x233003B5A6CFE6ADULL,
    0xD586BD01C5C217F6ULL, 0x5E5637885F29BC2BULL, 0x7EBA726D8C94094BULL,
    0x0A56A5F0BFE39272ULL, 0xD79476A84EE20D06ULL, 0x9E4C1269BAA4BF37ULL,
    0x17EFEE45B0DEE640ULL, 0x1D95B0A5FCF90BC6ULL, 0x93CBE0B699C2585DULL,
    0x65FA4F227A2B6D79ULL, 0xD5F9E858292504D5ULL, 0xC2B5A03F71471A6FULL,
    0x59300222B4561E00ULL, 0xCE2F8642CA0712DCULL, 0x7CA9723FBB2E8988ULL,
    0x2785338347F2BA08ULL, 0xC61BB3A141E50E8CULL, 0x150F361DAB9DEC26ULL,
    0x9F6A419D382595F4ULL, 0x64A53DC924FE7AC9ULL, 0x142DE49FFF7A7C3DULL,
    0x0C335248857FA9E7ULL, 0x0A9C32D5EAE45305ULL, 0xE6C42178C4BBB92EULL,
    0x71F1CE2490D20B07ULL, 0xF1BCC3D275AFE51AULL, 0xE728E8C83C334074ULL,
    0x96FBF83A12884624ULL, 0x81A1549FD6573DA5ULL, 0x5FA7867CAF35E149ULL,
    0x56986E2EF3ED091BULL, 0x917F1DD5F8886C61ULL, 0xD20D8C88C8FFE65FULL,
    0x31D71DCE64B2C310ULL, 0xF165B587DF898190ULL, 0xA57E6339DD2CF3A0ULL,
    0x1EF6E6DBB1961EC9ULL, 0x70CC73D90BC26E24ULL, 0xE21A6B35DF0C3AD7ULL,
    0x003A93D8B2806962ULL, 0x1C99DED33CB890A1ULL, 0xCF3145DE0ADD4289ULL,
    0xD0E4427A5514FB72ULL, 0x77C621CC9FB3A483ULL, 0x67A34DAC4356550BULL,
    0xF8D626AAAF278509ULL,
};

static int                  optUseBook = 1;
static char                 bookPath[1024] = "<embedded>";
static const unsigned char* g_book = NULL;     /* entries, 16 bytes each   */
static size_t               g_bookN = 0;
static unsigned char*       g_bookFileBuf = NULL;
static U64                  g_rng = 0x2545F4914F6CDD1DULL;

static U64 rngNext(void) {
    g_rng ^= g_rng << 13; g_rng ^= g_rng >> 7; g_rng ^= g_rng << 17;
    return g_rng;
}

static U64 rdBE(const unsigned char* p, int n) {
    U64 v = 0;
    int i;
    for (i = 0; i < n; i++) v = (v << 8) | p[i];
    return v;
}

/* Polyglot hash of the position (independent of our own Zobrist keys). */
static U64 polyKey(BOARD* b) {
    U64 k = 0;
    int sq;
    for (sq = 21; sq <= 98; sq++) {
        int p, kind;
        if (!sq_valid(sq)) continue;
        p = b->board[sq];
        if (p == EMPTY) continue;
        kind = 2 * (BASE(p) - 1) + (IS_BLACK(p) ? 0 : 1);   /* black pawn 0, white pawn 1, ... */
        k ^= PG_RANDOM[64 * kind + sq_to64(sq)];
    }
    if (b->castlePermission & WKCA) k ^= PG_RANDOM[768];
    if (b->castlePermission & WQCA) k ^= PG_RANDOM[769];
    if (b->castlePermission & BKCA) k ^= PG_RANDOM[770];
    if (b->castlePermission & BQCA) k ^= PG_RANDOM[771];
    if (b->enPas >= 0) {                     /* only if a pawn can really take en passant */
        int ep = b->enPas, own = (b->stm == WHITE) ? wP : bP;
        int a1 = (b->stm == WHITE) ? ep - 9 : ep + 9, a2 = (b->stm == WHITE) ? ep - 11 : ep + 11;
        if ((sq_valid(a1) && b->board[a1] == own) || (sq_valid(a2) && b->board[a2] == own))
            k ^= PG_RANDOM[772 + sq_file(ep)];
    }
    if (b->stm == WHITE) k ^= PG_RANDOM[780];
    return k;
}

static void bookSet(const unsigned char* data, size_t bytes) {
    g_book  = data;
    g_bookN = data ? bytes / 16 : 0;
}

/* "<embedded>" = the built-in book, anything else = a Polyglot file. */
static void bookLoad(const char* path) {
    free(g_bookFileBuf);
    g_bookFileBuf = NULL;
    bookSet(NULL, 0);
    if (!path[0] || !strcmp(path, "<empty>")) return;
    if (!strcmp(path, "<embedded>")) {
#if !defined(N8_NO_EMBED) && !defined(N8_NO_BOOK)
        bookSet(n8Book, (size_t)(n8BookEnd - n8Book));
#elif !defined(N8_NO_BOOK)
        {
            size_t sz = 0;
            char used[2200];
            g_bookFileBuf = readNearExe(N8_BOOK, &sz, used, sizeof(used));
            if (g_bookFileBuf) bookSet(g_bookFileBuf, sz);
            else say("info string book %s not found\n", N8_BOOK);
        }
#endif
        return;
    }
    {
        size_t sz = 0;
        char used[2200];
        g_bookFileBuf = readNearExe(path, &sz, used, sizeof(used));
        if (!g_bookFileBuf) { say("info string cannot open book %s\n", path); return; }
        if (sz % 16) say("info string %s is not a whole number of Polyglot entries -- using it anyway\n", path);
        bookSet(g_bookFileBuf, sz);
        say("info string book %s: %u positions/moves\n", used, (unsigned)g_bookN);
    }
}

/* All legal book moves for this position (with weights). */
static int bookCandidates(BOARD* b, MOVE* cand, int* cw, int cap) {
    MOVE legal[MAX_MOVES];
    int nc = 0, nl;
    size_t lo = 0, hi = g_bookN;
    U64 key;

    if (!g_book || !g_bookN) return 0;
    key = polyKey(b);
    while (lo < hi) {                                   /* first entry with this key */
        size_t mid = (lo + hi) / 2;
        if (rdBE(g_book + mid * 16, 8) < key) lo = mid + 1; else hi = mid;
    }
    if (lo >= g_bookN || rdBE(g_book + lo * 16, 8) != key) return 0;

    nl = genLegal(b, legal);
    for (; lo < g_bookN && rdBE(g_book + lo * 16, 8) == key && nc < cap; lo++) {
        const unsigned char* e = g_book + lo * 16;
        int pm = (int)rdBE(e + 8, 2), w = (int)rdBE(e + 10, 2);
        int tf = pm & 7, tr = (pm >> 3) & 7, ff = (pm >> 6) & 7, fr = (pm >> 9) & 7, pr = (pm >> 12) & 7;
        int from = 21 + fr * 10 + ff, to = 21 + tr * 10 + tf, k;
        int pc = b->board[from];
        /* Polyglot writes castling as "king takes own rook" */
        if (pc == wK && from == E1 && to == H1) to = G1;
        if (pc == wK && from == E1 && to == A1) to = C1;
        if (pc == bK && from == E8 && to == H8) to = G8;
        if (pc == bK && from == E8 && to == A8) to = C8;
        for (k = 0; k < nl; k++) {
            int t = MV_TYPE(legal[k]);
            if ((int)MV_FROM(legal[k]) != from || (int)MV_TO(legal[k]) != to) continue;
            if (IS_PROMO(t) ? (pr == 0 || (t & 3) != pr - 1) : pr != 0) continue;
            cand[nc] = legal[k];
            cw[nc++] = w;
            break;
        }
    }
    return nc;
}

/* A book move for this position, or 0. bestOnly = always the top weight,
   otherwise a weighted random pick (so self-play games differ). */
static MOVE bookMove(BOARD* b, int bestOnly, int* weightOut, int* totalOut) {
    MOVE cand[64];
    int  cw[64], nc, i = 0, total = 0;
    nc = bookCandidates(b, cand, cw, 64);
    if (!nc) return 0;
    for (i = 0; i < nc; i++) total += cw[i];
    i = 0;
    if (bestOnly) {
        int j;
        for (j = 1; j < nc; j++) if (cw[j] > cw[i]) i = j;
    } else if (total > 0) {
        int r = (int)(rngNext() % (U64)total);
        while (r >= cw[i]) { r -= cw[i]; i++; }
    } else {
        i = (int)(rngNext() % (U64)nc);
    }
    if (weightOut) *weightOut = cw[i];
    if (totalOut)  *totalOut  = total;
    return cand[i];
}

/* ================================================================
 *  SAN  (for the self-play PGN)
 * ================================================================ */
static void moveToSAN(BOARD* b, MOVE m, char* out) {
    MOVE legal[MAX_MOVES];
    int type = MV_TYPE(m), from = MV_FROM(m), to = MV_TO(m);
    int piece = b->board[from], base = BASE(piece), n, i, len = 0;
    char sq[3];

    if (type == KING_CASTLE)       len = sprintf(out, "O-O");
    else if (type == QUEEN_CASTLE) len = sprintf(out, "O-O-O");
    else {
        sq_to_str(to, sq);
        if (base == PAWN) {
            if (IS_CAP(type)) len += sprintf(out + len, "%cx", 'a' + sq_file(from));
            len += sprintf(out + len, "%.2s", sq);
            if (IS_PROMO(type)) len += sprintf(out + len, "=%c", "NBRQ"[type & 3]);
        } else {
            int ambig = 0, sameFile = 0, sameRank = 0;
            out[len++] = "  NBRQK"[base];
            n = genLegal(b, legal);
            for (i = 0; i < n; i++) {
                int f2 = MV_FROM(legal[i]);
                if (legal[i] == m || (int)MV_TO(legal[i]) != to || b->board[f2] != piece) continue;
                ambig = 1;
                if (sq_file(f2) == sq_file(from)) sameFile = 1;
                if (sq_rank(f2) == sq_rank(from)) sameRank = 1;
            }
            if (ambig) {
                if (!sameFile)      out[len++] = (char)('a' + sq_file(from));
                else if (!sameRank) out[len++] = (char)('1' + sq_rank(from));
                else { out[len++] = (char)('a' + sq_file(from)); out[len++] = (char)('1' + sq_rank(from)); }
            }
            if (IS_CAP(type)) out[len++] = 'x';
            len += sprintf(out + len, "%.2s", sq);
        }
    }
    out[len] = 0;
    makeMove(b, m);                                   /* check or mate? */
    if (inCheck(b, b->stm)) {
        n = genLegal(b, legal);
        out[len++] = n ? '+' : '#';
        out[len] = 0;
    }
    undoMove(b);
}

/* ================================================================
 *  SELF-PLAY  -- Brain A vs Brain B inside one process.
 *  Each player keeps its own hash table and eval cache, so the two
 *  brains never see each other's scores.
 * ================================================================ */
static int   spOpponent = 0;                   /* "SelfPlayOpponent" = Brain B */
static int   spGames    = 10;
static int   spMoveTime = 1000;                /* ms per move                  */
static char  spPgnPath[1024] = "ChesslabNnet8_selfplay.pgn";
static int       g_maxDepth   = 0;             /* MaxDepth, 0 = unlimited      */
static long long g_optNodes   = 0;             /* NodeLimit, 0 = unlimited     */
static int       g_optTimeCap = 0;             /* MoveTimeCap ms, 0 = off      */

typedef struct {
    int         brain;
    TT_ENTRY*   tt;
    U64         ttMask, ttCount;
    EVAL_ENTRY* cache;
} PLAYER;

static void usePlayer(PLAYER* p) {
    tt = p->tt; ttMask = p->ttMask; ttCount = p->ttCount;
    evalCache = p->cache;
    activateBrain(p->brain);
}

/* Threefold repetition (the search itself already scores twofold as a draw). */
static int threefold(BOARD* b) {
    int i, count = 0, stop = b->hisPly - b->fiftyMove;
    if (stop < 0) stop = 0;
    for (i = b->hisPly - 2; i >= stop; i -= 2)
        if (b->history[i].hash == b->hash && ++count >= 2) return 1;
    return 0;
}

/* Typed "stop"/"quit" while self-play runs? */
static int spStdinDead = 0;
static int spInterrupted(void) {
    char cmd[512];
    if (spStdinDead || !inputWaiting()) return 0;
    if (!fgets(cmd, sizeof(cmd), stdin)) { spStdinDead = 1; return 0; }   /* no console: ignore */
    if (!strncmp(cmd, "quit", 4)) return 2;
    if (!strncmp(cmd, "stop", 4)) return 1;
    return 0;
}

static void spDate(char* out) {
    time_t now = time(NULL);
    struct tm* t = localtime(&now);
    if (t) strftime(out, 16, "%Y.%m.%d", t);
    else strcpy(out, "????.??.??");
}

/* Returns 2 if "quit" was typed. */
static int selfPlay(BOARD* b, int brainA, int brainB, int games, int moveMs) {
    PLAYER pl[2], saved;
    U64 entries = ttCount;
    double scoreA = 0.0;
    int g, quit = 0, played = 0, wA = 0, dA = 0, lA = 0;
    FILE* pgn;
    char date[16];

    if (brainA < 0 || brainA >= N8_BRAINS) brainA = g_brain;
    if (brainB < 0 || brainB >= N8_BRAINS) brainB = spOpponent;
    if (games < 1) games = 1;
    if (moveMs < 10) moveMs = 10;
    if ((brainA >= 1 && !g_net[brainA].ok) || (brainB >= 1 && !g_net[brainB].ok)) {
        say("info string self-play: brain %d or %d has no net loaded\n", brainA, brainB);
        return 0;
    }

    saved.brain = g_brain; saved.tt = tt; saved.ttMask = ttMask; saved.ttCount = ttCount; saved.cache = evalCache;
    pl[0].brain = brainA;
    pl[1].brain = brainB;
    for (g = 0; g < 2; g++) {                       /* one hash table + cache per player */
        U64 e = entries;
        pl[g].tt = NULL;
        while (!pl[g].tt && e >= 1024) {
            pl[g].tt = (TT_ENTRY*)calloc((size_t)e, sizeof(TT_ENTRY));
            if (!pl[g].tt) e /= 2;
        }
        pl[g].ttCount = e; pl[g].ttMask = e - 1;
        pl[g].cache = (EVAL_ENTRY*)calloc(EVAL_CACHE_SIZE, sizeof(EVAL_ENTRY));
        if (!pl[g].tt || !pl[g].cache) {
            say("info string self-play: not enough memory (lower Hash)\n");
            free(pl[0].tt); free(pl[0].cache);
            if (g == 1) { free(pl[1].tt); free(pl[1].cache); }
            return 0;
        }
    }

    pgn = fopen(spPgnPath, "a");
    spDate(date);
    say("Self-play: Brain %d (%s) vs Brain %d (%s), %d games, %d ms/move, book %s\n",
        brainA, brainName(brainA), brainB, brainName(brainB), games, moveMs,
        optUseBook && g_bookN ? "on" : "off");
    say("PGN -> %s%s\n", spPgnPath, pgn ? "" : "  (cannot open it -- games are not saved)");
    say("Type stop to end after the current move.\n\n");

    for (g = 0; g < games && !quit; g++) {
        static char text[65536];
        PLAYER *white = &pl[g % 2 == 0 ? 0 : 1], *black = &pl[g % 2 == 0 ? 1 : 0];
        const char *result = "*", *reason = "";
        int len = 0, lineLen = 0, plies = 0, inBook[2] = {1, 1}, k;

        parseFEN(b, START_FEN);
        for (k = 0; k < 2; k++) {
            memset(pl[k].tt, 0, sizeof(TT_ENTRY) * (size_t)pl[k].ttCount);
            memset(pl[k].cache, 0, sizeof(EVAL_ENTRY) * EVAL_CACHE_SIZE);
        }
        text[0] = 0;

        for (;;) {
            MOVE legal[MAX_MOVES], m = 0;
            PLAYER* me = (b->stm == WHITE) ? white : black;
            char san[16], tok[32];
            int n = genLegal(b, legal), stopNow;

            if (n == 0) {
                if (inCheck(b, b->stm)) { result = b->stm == WHITE ? "0-1" : "1-0"; reason = b->stm == WHITE ? "Black mates" : "White mates"; }
                else { result = "1/2-1/2"; reason = "stalemate"; }
                break;
            }
            if (b->fiftyMove >= 100)   { result = "1/2-1/2"; reason = "50-move rule"; break; }
            if (threefold(b))          { result = "1/2-1/2"; reason = "threefold repetition"; break; }
            if (insufficientMaterial(b)) { result = "1/2-1/2"; reason = "insufficient material"; break; }
            if (plies >= 600)          { result = "1/2-1/2"; reason = "adjudicated: 300 moves"; break; }

            stopNow = spInterrupted();
            if (stopNow) { quit = stopNow; reason = "stopped"; break; }

            usePlayer(me);
            if (optUseBook && inBook[b->stm]) {
                m = bookMove(b, 0, NULL, NULL);
                if (!m) inBook[b->stm] = 0;
            }
            if (!m) {
                SEARCH_INFO info;
                memset(&info, 0, sizeof(info));
                info.depth = (g_maxDepth > 0) ? g_maxDepth : MAX_PLY - 1;
                info.timeLimit = moveMs;
                info.nodeLimit = g_optNodes;
                g_silent = 1;
                m = searchPosition(b, &info);
                g_silent = 0;
            }
            if (!m) m = legal[0];

            moveToSAN(b, m, san);
            if (b->stm == WHITE) snprintf(tok, sizeof(tok), "%d. %s", plies / 2 + 1, san);
            else                 snprintf(tok, sizeof(tok), "%s", san);
            if (lineLen + (int)strlen(tok) + 1 > 79) { text[len++] = '\n'; lineLen = 0; }
            else if (lineLen) { text[len++] = ' '; lineLen++; }
            len += sprintf(text + len, "%s", tok);
            lineLen += (int)strlen(tok);
            makeMove(b, m);
            plies++;
        }

        if (!strcmp(result, "*") && quit) {           /* stopped mid-game: not counted */
            say("game %d stopped after %d moves -- not counted\n", g + 1, (plies + 1) / 2);
        } else {
            double sA;
            int aWhite = (white == &pl[0]);
            played++;
            if (!strcmp(result, "1/2-1/2")) sA = 0.5;
            else sA = ((!strcmp(result, "1-0")) == aWhite) ? 1.0 : 0.0;
            scoreA += sA;
            if (sA == 1.0) wA++; else if (sA == 0.5) dA++; else lA++;
            say("game %3d/%d  %-9s (W) vs %-9s (B)  %-7s  %-24s  Brain %d %s: +%d =%d -%d  (%.1f/%d)\n",
                g + 1, games, brainName(white->brain), brainName(black->brain), result, reason,
                brainA, brainName(brainA), wA, dA, lA, scoreA, played);
        }
        if (pgn) {
            fprintf(pgn, "[Event \"ChesslabNnet8 self-play\"]\n[Site \"?\"]\n[Date \"%s\"]\n[Round \"%d\"]\n"
                         "[White \"ChesslabNnet8 %s\"]\n[Black \"ChesslabNnet8 %s\"]\n[Result \"%s\"]\n"
                         "[TimeControl \"-\"]\n[Termination \"%s\"]\n\n",
                    date, g + 1, brainName(white->brain), brainName(black->brain), result,
                    strstr(reason, "adjudicated") ? "adjudication" : (quit ? "unterminated" : "normal"));
            fprintf(pgn, "%s {%s, %d ms/move} %s\n\n", text, reason, moveMs, result);
            fflush(pgn);
        }
    }

    if (played) {
        double s = scoreA / played;
        say("\nFinal: Brain %d (%s) vs Brain %d (%s): +%d =%d -%d  %.1f/%d (%.1f%%)",
            brainA, brainName(brainA), brainB, brainName(brainB), wA, dA, lA, scoreA, played, 100.0 * s);
        if (s > 0.0 && s < 1.0) {
            double elo = -400.0 * log10(1.0 / s - 1.0);
            say("  Elo %+.0f\n", fabs(elo) < 0.5 ? 0.0 : elo);
        }
        else say("  Elo n/a (one-sided)\n");
    }
    if (pgn) fclose(pgn);

    for (g = 0; g < 2; g++) { free(pl[g].tt); free(pl[g].cache); }
    usePlayer(&saved);
    ttClear();
    evalCacheClear();
    parseFEN(b, START_FEN);
    return quit == 2 ? 2 : 0;
}

/* ================================================================
 *  BRAIN SELECTION + UCI
 * ================================================================ */
/* "31k", "Homebrew", "NetFile" or a number 0-7 -> brain index, -1 if unknown */
static int brainFromString(const char* v) {
    int br;
    const char* d;
    while (*v == ' ') v++;
    for (d = v; isdigit((unsigned char)*d); d++) ;
    if (d != v && *d == 0) {                      /* all digits: a brain number */
        br = atoi(v);
        return (br >= 0 && br < N8_BRAINS) ? br : -1;
    }
    for (br = 0; br < N8_BRAINS; br++)
        if (g_net[br].label[0] && strieq(v, g_net[br].label)) return br;
    if (strieq(v, "HB") || strieq(v, "Hombrew")) return 0;
    return -1;
}

static int g_defaultBrain = 6;      /* from the exe name, else the strongest net */
static int g_optBrain = 6;          /* the "Brain" option                        */
static int optSideBrains = 0;       /* "SelfPlay" box: White = Brain, Black = SelfPlayOpponent */
static int g_announced = -1;        /* brain last reported at "go" */

static void setBrain(int br) {
    if (br < 0) br = 0;
    if (br >= N8_BRAINS) br = N8_BRAINS - 1;
    g_optBrain = br;
    if (br != g_brain) {
        activateBrain(br);
        ttClear();                      /* another brain scores on another scale */
        evalCacheClear();
    } else {
        activateBrain(br);
    }
    if (br == 0)
        say("info string Brain 0 = Homebrew (hand-written eval, HB7 weights)\n");
    else if (g_net[br].ok)
        say("info string Brain %d = %s (768 -> %dx2 -> 1)\n", br, brainName(br), g_net[br].H);
    else
        say("info string Brain %d (%s) has no net -- %s; playing on material only\n", br, brainName(br),
            br == 7 ? "set NetFile first" : "it was not found at build time");
}

static void printBrains(void) {
    int br;
    for (br = 0; br < N8_BRAINS; br++) {
        char what[400];
        if (br == 0) snprintf(what, sizeof(what), "hand-written eval (HB7 weights)");
        else if (g_net[br].ok) snprintf(what, sizeof(what), "768 -> %dx2 -> 1   %s", g_net[br].H, g_net[br].src);
        else snprintf(what, sizeof(what), br == 7 ? "empty (set NetFile)" : "not loaded");
        say("info string Brain %d  %-9s %s%s%s\n", br, brainName(br), what,
            br == g_optBrain ? (optSideBrains ? "   <- plays White" : "   <- plays") : "",
            br == spOpponent ? (optSideBrains ? "   <- plays Black" : "   <- self-play opponent") : "");
    }
    say("info string Book: %s, %s (%u entries)\n", optUseBook ? "on" : "off", bookPath, (unsigned)g_bookN);
}

/* Static eval of the current position for every brain. */
static void printEval(BOARD* b) {
    int sgn = (b->stm == WHITE) ? 1 : -1, saved = g_brain, br;
    char line[1024];
    int len = snprintf(line, sizeof(line), "info string eval (White's view, cp):");
    for (br = 0; br < N8_BRAINS; br++) {
        int v;
        if (br > 0 && !g_net[br].ok) continue;
        activateBrain(br);
        v = (br == 0) ? evalHomebrew(b) : evalNet(b);
        len += snprintf(line + len, sizeof(line) - len, "  %s %+d", brainName(br), sgn * v);
    }
    activateBrain(saved);
    snprintf(line + len, sizeof(line) - len, "   | plays: Brain %d (%s)", saved, brainName(saved));
    say("%s\n", line);
}

/* "book": Polyglot key of the position and its book moves. */
static void printBook(BOARD* b) {
    MOVE cand[64];
    int cw[64], n = bookCandidates(b, cand, cw, 64), i;
    char line[2048], mv[6];
    int len = snprintf(line, sizeof(line), "info string book key %016llx:", (unsigned long long)polyKey(b));
    if (!n) len += snprintf(line + len, sizeof(line) - len, " no book moves");
    for (i = 0; i < n && len < (int)sizeof(line) - 32; i++) {
        moveToStr(cand[i], mv);
        len += snprintf(line + len, sizeof(line) - len, " %s(%d)", mv, cw[i]);
    }
    say("%s\n", line);
}

static void printHelp(void) {
    say("ChesslabNnet8 console commands\n"
        "  uci / isready / position ... / go ...   normal UCI\n"
        "  brains                 list the brains (0 Homebrew, 1-6 built-in nets, 7 NetFile)\n"
        "  setoption name Brain value 31k     pick the brain that plays (name or 0-7)\n"
        "  eval                   static eval of the position for every brain\n"
        "  weights                Homebrew weights (Brain 0)\n"
        "  book                   book moves for the position (with weights)\n"
        "  selfplay [A] [B] [games] [ms]      e.g. selfplay 31k Homebrew 10 1000\n"
        "  bench [depth]          fixed-depth node count / speed\n"
        "  perft N                move generator check (perft 5 = 4865609)\n"
        "  quit\n");
}

static void handleSetOption(char* line) {
    char name[256] = {0}, value[1024] = {0};
    char* n = strstr(line, "name ");
    char* v = strstr(line, " value ");
    if (!n) return;
    n += 5;
    if (v) {
        size_t len = (size_t)(v - n);
        if (len >= sizeof(name)) len = sizeof(name) - 1;
        memcpy(name, n, len);
        strncpy(value, v + 7, sizeof(value) - 1);
    } else {
        strncpy(name, n, sizeof(name) - 1);
    }
    trim(name);
    trim(value);

    if (strieq(name, "Hash")) {
        int mb = atoi(value);
        if (mb > 0) ttResize(mb);
    }
    else if (strieq(name, "Brain")) {
        int br = brainFromString(value);
        if (br < 0) say("info string unknown brain %s\n", value);
        else setBrain(br);
    }
    else if (strieq(name, "SelfPlayOpponent")) {
        int o = brainFromString(value);
        if (o < 0) say("info string unknown brain %s\n", value);
        else spOpponent = o;
    }
    else if (strieq(name, "SelfPlay")) {
        optSideBrains = strieq(value, "true");
        if (optSideBrains)
            say("info string SelfPlay on: White plays %s, Black plays %s\n", brainName(g_optBrain), brainName(spOpponent));
    }
    else if (strieq(name, "SelfPlayGames"))    { spGames = atoi(value); if (spGames < 1) spGames = 1; }
    else if (strieq(name, "SelfPlayMoveTime")) { spMoveTime = atoi(value); if (spMoveTime < 10) spMoveTime = 10; }
    else if (strieq(name, "SelfPlayPGN")) {
        if (value[0] && strcmp(value, "<empty>") != 0) {
            strncpy(spPgnPath, value, sizeof(spPgnPath) - 1);
            spPgnPath[sizeof(spPgnPath) - 1] = 0;
        }
    }
    else if (strieq(name, "UseBook"))          optUseBook = strieq(value, "true");
    else if (strieq(name, "BookFile")) {
        if (!value[0] || !strcmp(value, "<empty>")) strcpy(value, "<embedded>");
        strncpy(bookPath, value, sizeof(bookPath) - 1);
        bookPath[sizeof(bookPath) - 1] = 0;
        bookLoad(bookPath);
    }
    else if (strieq(name, "NetFile")) {
        if (value[0] && strcmp(value, "<empty>") != 0) {
            strncpy(nnPath, value, sizeof(nnPath) - 1);
            nnPath[sizeof(nnPath) - 1] = 0;
            if (netLoadFile(&g_net[7], nnPath))
                say("info string NetFile %s loaded as Brain 7 (768 -> %dx2 -> 1)%s\n", g_net[7].src,
                    g_net[7].H, g_brain == 7 ? "" : " -- set Brain to 7 to play with it");
            if (g_brain == 7) { activateBrain(7); ttClear(); evalCacheClear(); }
        }
    }
    else if (strieq(name, "NodeLimit"))   { long long nv = atoll(value); g_optNodes = nv < 0 ? 0 : nv; }
    else if (strieq(name, "MaxDepth")) {
        int d = atoi(value);
        g_maxDepth = d < 0 ? 0 : (d > MAX_PLY - 1 ? MAX_PLY - 1 : d);
    }
    else if (strieq(name, "MoveTimeCap")) { int t = atoi(value); g_optTimeCap = t < 0 ? 0 : t; }
    else if (strieq(name, "Log"))         setLogging(strieq(value, "true") || !strcmp(value, "1"));
    else if (strieq(name, "LogFile")) {
        if (value[0] && strcmp(value, "<empty>") != 0) {
            logClose();                                  /* reopens at the next line */
            strncpy(g_logPath, value, sizeof(g_logPath) - 1);
            g_logPath[sizeof(g_logPath) - 1] = 0;
        }
    }
    else if (strieq(name, "RFP"))       optRFP      = strieq(value, "true");
    else if (strieq(name, "NullMove+")) optNullPlus = strieq(value, "true");
    else if (strieq(name, "Futility"))  optFutility = strieq(value, "true");
    else if (strieq(name, "LMP"))       optLMP      = strieq(value, "true");
    else if (strieq(name, "LMR+"))      optLMRPlus  = strieq(value, "true");
    else if (strieq(name, "SEE"))       optSEE      = strieq(value, "true");
    else if (strieq(name, "Delta"))     optDelta    = strieq(value, "true");
    else if (strieq(name, "IIR"))       optIIR      = strieq(value, "true");
    else if (strieq(name, "Ordering+")) optOrdering = strieq(value, "true");
    else if (strieq(name, "WeightsFile")) {
        if (value[0] && strcmp(value, "<empty>") != 0) {
            strncpy(weightsPath, value, sizeof(weightsPath) - 1);
            weightsPath[sizeof(weightsPath) - 1] = 0;
            weightsLoaded = weightsLoad(weightsPath);
            if (weightsLoaded < 0) say("info string cannot open %s\n", weightsPath);
            else say("info string loaded %d weights from %s\n", weightsLoaded, weightsPath);
        }
    }
    else if (value[0]) {
        if (!weightSet(name, atoi(value)))
            say("info string unknown option %s\n", name);
    }
}

static void printUciOptions(void) {
    int i;
    say("id name %s\nid author K7\n", g_engineName);
    say("option name Hash type spin default %d min 1 max %d\n", TT_DEFAULT_MB, TT_MAX_MB);
    {   /* drop-down lists with the brain names */
        char vars[400];
        int br, len = 0;
        vars[0] = 0;
        for (br = 0; br < N8_BRAINS; br++)
            len += snprintf(vars + len, sizeof(vars) - len, " var %s", brainName(br));
        say("option name Brain type combo default %s%s\n", brainName(g_defaultBrain), vars);
        say("option name SelfPlayOpponent type combo default %s%s\n", brainName(spOpponent), vars);
    }
    say("option name SelfPlay type check default false\n");
    say("option name SelfPlayGames type spin default %d min 1 max 10000\n", spGames);
    say("option name SelfPlayMoveTime type spin default %d min 10 max 600000\n", spMoveTime);
    say("option name SelfPlayPGN type string default %s\n", spPgnPath);
    say("option name UseBook type check default %s\n", optUseBook ? "true" : "false");
    say("option name BookFile type string default <embedded>\n");
    say("option name NetFile type string default <empty>\n");
    say("option name NodeLimit type spin default 0 min 0 max 1000000000\n");
    say("option name MaxDepth type spin default 0 min 0 max %d\n", MAX_PLY - 1);
    say("option name MoveTimeCap type spin default 0 min 0 max 600000\n");
    say("option name Log type check default false\n");
    say("option name LogFile type string default %s\n", g_logPath);
    say("option name RFP type check default %s\n",       optRFP      ? "true" : "false");
    say("option name NullMove+ type check default %s\n", optNullPlus ? "true" : "false");
    say("option name Futility type check default %s\n",  optFutility ? "true" : "false");
    say("option name LMP type check default %s\n",       optLMP      ? "true" : "false");
    say("option name LMR+ type check default %s\n",      optLMRPlus  ? "true" : "false");
    say("option name SEE type check default %s\n",       optSEE      ? "true" : "false");
    say("option name Delta type check default %s\n",     optDelta    ? "true" : "false");
    say("option name IIR type check default %s\n",       optIIR      ? "true" : "false");
    say("option name Ordering+ type check default %s\n", optOrdering ? "true" : "false");
    say("option name WeightsFile type string default %s\n", weightsPath);
    for (i = 0; i < NUM_W; i++)
        say("option name %s type spin default %d min %d max %d\n",
            wdef[i].name, W[i], wdef[i].lo, wdef[i].hi);
    {
        char legend[512];
        int br, len = snprintf(legend, sizeof(legend), "info string Brains:");
        for (br = 0; br < N8_BRAINS; br++)
            len += snprintf(legend + len, sizeof(legend) - len, " %d=%s", br, brainName(br));
        say("%s\n", legend);
    }
    if (weightsLoaded >= 0)
        say("info string loaded %d Homebrew weights from %s\n", weightsLoaded, weightsPath);
    say("uciok\n");
}

/* weights.txt next to the exe first, then the current folder (as HB7). */
static void loadStartupWeights(void) {
    if (g_exeDir[0] && strlen(g_exeDir) < sizeof(weightsPath) - 16)
        snprintf(weightsPath, sizeof(weightsPath), "%sweights.txt", g_exeDir);
    weightsLoaded = weightsLoad(weightsPath);
    if (weightsLoaded < 0) {
        strcpy(weightsPath, "weights.txt");
        weightsLoaded = weightsLoad(weightsPath);
    }
}

/* "go" with the book, NodeLimit, MaxDepth and MoveTimeCap (as HB7). Returns 1 on quit. */
static int cmdGo(BOARD* b, const char* line) {
    SEARCH_INFO info;
    const char* p;
    char best[6];
    MOVE bm;
    int wtime = 0, btime = 0, winc = 0, binc = 0, movestogo = 30, infinite = 0;
    long long wantNodes = 0;

    if (strstr(line, "infinite")) infinite = 1;
    {   /* SelfPlay box: the brain follows the colour this engine plays */
        int want = (optSideBrains && b->stm == BLACK) ? spOpponent : g_optBrain;
        if (want != g_brain) { activateBrain(want); ttClear(); evalCacheClear(); }
    }
    if (g_announced != g_brain) {
        g_announced = g_brain;
        say("info string playing with Brain %d = %s%s\n", g_brain, brainName(g_brain),
            optSideBrains ? (b->stm == WHITE ? " (SelfPlay: White)" : " (SelfPlay: Black)") : "");
    }

    if (optUseBook && !infinite) {
        int w = 0, tot = 0;
        MOVE bmv = bookMove(b, 0, &w, &tot);
        if (bmv) {
            moveToStr(bmv, best);
            say("info string book move %s (weight %d of %d)\n", best, w, tot);
            say("bestmove %s\n", best);
            return 0;
        }
    }

    memset(&info, 0, sizeof(info));
    info.depth = MAX_PLY - 1;
    info.timeLimit = 0;

    if ((p = strstr(line, "depth")))    sscanf(p + 5, "%d", &info.depth);
    if ((p = strstr(line, "movetime"))) sscanf(p + 8, "%d", &info.timeLimit);
    if ((p = strstr(line, "wtime")))    sscanf(p + 5, "%d", &wtime);
    if ((p = strstr(line, "btime")))    sscanf(p + 5, "%d", &btime);
    if ((p = strstr(line, "winc")))     sscanf(p + 4, "%d", &winc);
    if ((p = strstr(line, "binc")))     sscanf(p + 4, "%d", &binc);
    if ((p = strstr(line, "movestogo")))sscanf(p + 9, "%d", &movestogo);
    if ((p = strstr(line, "nodes")))    sscanf(p + 5, "%lld", &wantNodes);

    if (!infinite && info.timeLimit == 0 && (wtime || btime)) {
        int mytime = (b->stm == WHITE) ? wtime : btime;
        int myinc  = (b->stm == WHITE) ? winc  : binc;
        if (movestogo < 1) movestogo = 30;
        info.timeLimit = mytime / movestogo + myinc - 30;
        if (info.timeLimit < 20) info.timeLimit = 20;
        if (info.timeLimit > mytime / 2) info.timeLimit = mytime / 2;
    }
    if (info.depth < 1 || info.depth >= MAX_PLY) info.depth = MAX_PLY - 1;

    /* "go nodes N" overrides NodeLimit; "go infinite" lifts every limit */
    info.nodeLimit = wantNodes > 0 ? wantNodes : g_optNodes;
    if (infinite) info.nodeLimit = 0;
    if (!infinite && g_maxDepth > 0 && info.depth > g_maxDepth) info.depth = g_maxDepth;
    if (!infinite && g_optTimeCap > 0 && (info.timeLimit == 0 || info.timeLimit > g_optTimeCap))
        info.timeLimit = g_optTimeCap;

    bm = searchPosition(b, &info);

    /* UCI: in "go infinite" the move is sent only after "stop", even
       if the search already reached its maximum depth. */
    while (infinite && !info.stopped && !g_quit && !g_pending[0]) {
        if (handleSearchCommand()) break;
    }

    if (bm) {
        moveToStr(bm, best);
        say("bestmove %s\n", best);
    } else {
        say("bestmove 0000\n");
    }
    return g_quit;
}

/* "selfplay [A] [B] [games] [ms]" -- missing values come from the options. */
static int cmdSelfPlay(BOARD* b, const char* args) {
    int a = g_brain, o = spOpponent, games = spGames, ms = spMoveTime, x;
    char t[4][64];
    int n = sscanf(args, "%63s %63s %63s %63s", t[0], t[1], t[2], t[3]);
    if (n >= 1) { if ((x = brainFromString(t[0])) < 0) { say("unknown brain %s\n", t[0]); return 0; } a = x; }
    if (n >= 2) { if ((x = brainFromString(t[1])) < 0) { say("unknown brain %s\n", t[1]); return 0; } o = x; }
    if (n >= 3) games = atoi(t[2]);
    if (n >= 4) ms = atoi(t[3]);
    return selfPlay(b, a, o, games, ms);
}

static void uciLoop(BOARD* b) {
    char line[8192];

    setbuf(stdin, NULL);
    setbuf(stdout, NULL);

    for (;;) {
        char token[64] = {0};
        if (g_pending[0]) {                        /* command queued during a search */
            strcpy(line, g_pending);
            g_pending[0] = 0;
        } else if (!fgets(line, sizeof(line), stdin)) {
            break;
        }
        line[strcspn(line, "\r\n")] = 0;
        if (!line[0]) continue;
        sscanf(line, "%63s", token);
        /* setoption is logged after it is applied, so "LogFile" takes
           effect before its own line is written (as HB7). */
        g_logReady = strcmp(token, "setoption") != 0;
        if (g_logReady) logLine(">>", line);

        if (!strcmp(token, "uci")) {
            printUciOptions();
        }
        else if (!strcmp(token, "isready")) {
            say("readyok\n");
        }
        else if (!strcmp(token, "setoption")) {
            handleSetOption(line);
            logLine(">>", line);
        }
        else if (!strcmp(token, "ucinewgame")) {
            parseFEN(b, START_FEN);
            ttClear();
            evalCacheClear();
        }
        else if (!strcmp(token, "eval"))    printEval(b);
        else if (!strcmp(token, "brains"))  printBrains();
        else if (!strcmp(token, "weights")) weightsPrint();
        else if (!strcmp(token, "help"))    printHelp();
        else if (!strcmp(token, "book"))    printBook(b);
        else if (!strcmp(token, "selfplay")) {
            if (cmdSelfPlay(b, line + 8) == 2) break;
        }
        else if (!strcmp(token, "bench")) {
            /* Fixed-depth search over set positions: same build + same net =
               same node count, so any change in nodes means play changed. */
            static const char* benchFens[] = {
                "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
                "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
                "r1bqkb1r/pppp1ppp/2n2n2/4p3/2B1P3/5N2/PPPP1PPP/RNBQK2R w KQkq - 4 4",
                "r1bq1rk1/pp2ppbp/2np1np1/8/3NP3/2N1BP2/PPPQ2PP/R3KB1R w KQ - 1 9",
                "2rq1rk1/pp1bppbp/3p1np1/4n3/3NP2P/1BN1BP2/PPPQ2P1/2KR3R w - - 1 13",
                "r2q1rk1/1b2bppp/p2ppn2/1p6/3BPP2/2NB4/PPPQ2PP/2KR3R w - - 0 13",
                "rnbq1rk1/ppp1ppbp/3p1np1/8/2PPP3/2N2N2/PP2BPPP/R1BQK2R b KQ - 3 6",
                "r1b2rk1/2q1bppp/p2p1n2/np2p3/3PP3/5N1P/PPBN1PP1/R1BQR1K1 w - - 0 13",
                "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
                "6k1/5ppp/8/8/8/2B5/5PPP/3R2K1 w - - 0 1",
                "8/8/3k4/3p4/3P4/2K5/8/8 w - - 0 1",
                "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10",
                "4rrk1/pp1n3p/3q2pQ/2p1pb2/2PP4/2P3N1/P2B2PP/4RRK1 b - - 7 19",
                "3r1rk1/p5pp/bpp1pp2/8/q1PP1P2/b3P3/P2NQRPP/1R2B1K1 b - - 6 22",
            };
            int nf = (int)(sizeof(benchFens) / sizeof(benchFens[0])), d = 8, i;
            long long total = 0;
            clock_t t0;
            double ms = 0.0;
            sscanf(line + 5, "%d", &d);
            if (d < 1) d = 1;
            if (d > MAX_PLY - 1) d = MAX_PLY - 1;
            g_silent = 1;
            for (i = 0; i < nf; i++) {
                SEARCH_INFO info;
                MOVE bm;
                char mv[6] = "0000";
                parseFEN(b, benchFens[i]);
                ttClear();
                evalCacheClear();
                memset(&info, 0, sizeof(info));
                info.depth = d;
                t0 = clock();                       /* time the search only */
                bm = searchPosition(b, &info);
                ms += (double)(clock() - t0) * 1000.0 / CLOCKS_PER_SEC;
                if (bm) moveToStr(bm, mv);
                say("bench %2d  %-6s nodes %lld\n", i + 1, mv, info.nodes);
                total += info.nodes;
            }
            g_silent = 0;
            say("bench depth %d (Brain %d %s): %lld nodes  %.0f ms  %lld nps\n", d, g_brain,
                brainName(g_brain), total, ms, (long long)(total / (ms / 1000.0 + 0.001)));
            parseFEN(b, START_FEN);
            ttClear();
            evalCacheClear();
        }
        else if (!strcmp(token, "perft")) {
            int d = 1, i;
            sscanf(line + 5, "%d", &d);
            for (i = 1; i <= d; i++) {
                clock_t t0 = clock();
                long nds = perft(b, i);
                double ms = (double)(clock() - t0) * 1000.0 / CLOCKS_PER_SEC;
                say("perft %d : %ld nodes  (%.0f ms)\n", i, nds, ms);
                fflush(stdout);
            }
        }
        else if (!strcmp(token, "position")) {
            char* ptr = strstr(line, "startpos");
            char* fenp = strstr(line, "fen");
            char* mv;

            if (ptr) {
                parseFEN(b, START_FEN);
            } else if (fenp) {
                parseFEN(b, fenp + 4);
            } else {
                parseFEN(b, START_FEN);
            }

            mv = strstr(line, "moves");
            if (mv) {
                char ms[16];
                mv += 5;
                while (sscanf(mv, "%15s", ms) == 1) {
                    MOVE m = strToMove(b, ms);
                    if (!m || b->hisPly >= HIST_SIZE - MAX_PLY - 2) break;
                    makeMove(b, m);
                    mv = strstr(mv, ms) + strlen(ms);
                }
            }
        }
        else if (!strcmp(token, "go")) {
            if (cmdGo(b, line)) break;
        }
        else if (!strcmp(token, "stop")) {
            /* stop with no search running: nothing to do */
        }
        else if (!strcmp(token, "quit")) {
            break;
        }
    }
}

int main(int argc, char** argv) {
    int br;
    g_t0 = nowMs();
    if (argc > 0 && argv[0]) {                     /* folder of the exe */
        const char *s1 = strrchr(argv[0], '/'), *s2 = strrchr(argv[0], '\\');
        const char* slash = s1 > s2 ? s1 : s2;
        if (slash && (size_t)(slash - argv[0]) < sizeof(g_exeDir) - 1) {
            memcpy(g_exeDir, argv[0], (size_t)(slash - argv[0]) + 1);
            g_exeDir[slash - argv[0] + 1] = 0;
        }
        {
            const char* base = slash ? slash + 1 : argv[0];
            size_t n = strlen(base);
            if (n > 4 && (!strcmp(base + n - 4, ".exe") || !strcmp(base + n - 4, ".EXE"))) n -= 4;
            if (n >= sizeof(g_exeBase)) n = sizeof(g_exeBase) - 1;
            memcpy(g_exeBase, base, n);
            g_exeBase[n] = 0;
        }
    }
    g_rng ^= (U64)time(NULL) * 0x9E3779B97F4A7C15ULL ^ (U64)clock();
    if (!g_rng) g_rng = 0x2545F4914F6CDD1DULL;

    initZobrist();
    initLMR();
    weightsReset();

    gboard = (BOARD*)malloc(sizeof(BOARD));
    evalCache = (EVAL_ENTRY*)calloc(EVAL_CACHE_SIZE, sizeof(EVAL_ENTRY));
    ttResize(TT_DEFAULT_MB);
    if (!gboard || !tt || !evalCache) return 1;

    loadStartupWeights();
    loadBuiltinNets();
    for (br = 6; br > 0 && !g_net[br].ok; br--) ;      /* strongest built-in net */
    g_defaultBrain = br;
    {   /* "ChesslabNnet8_Homebrew.exe" -> Brain Homebrew, "..._25k.exe" -> 25k */
        const char* u = strrchr(g_exeBase, '_');
        int fromName = u ? brainFromString(u + 1) : -1;
        if (fromName >= 0 && (fromName == 0 || g_net[fromName].ok)) {
            g_defaultBrain = fromName;
            snprintf(g_engineName, sizeof(g_engineName), "ChesslabNnet8 %s", brainName(fromName));
        }
    }
    activateBrain(g_defaultBrain);
    g_optBrain = g_defaultBrain;
    spOpponent = (g_defaultBrain == 0) ? 6 : 0;
    bookLoad(bookPath);
    parseFEN(gboard, START_FEN);

    if (argc >= 2 && !strcmp(argv[1], "selfplay")) {   /* ChesslabNnet8.exe selfplay A B games ms */
        int a = argc > 2 ? brainFromString(argv[2]) : g_brain;
        int o = argc > 3 ? brainFromString(argv[3]) : spOpponent;
        int games = argc > 4 ? atoi(argv[4]) : spGames;
        int ms = argc > 5 ? atoi(argv[5]) : spMoveTime;
        if (argc > 6) { strncpy(spPgnPath, argv[6], sizeof(spPgnPath) - 1); spPgnPath[sizeof(spPgnPath) - 1] = 0; }
        selfPlay(gboard, a, o, games, ms);
    } else {
        uciLoop(gboard);
    }

    logClose();
    free(tt);
    free(evalCache);
    free(gboard);
    for (br = 0; br < N8_BRAINS; br++) netFree(&g_net[br]);
    free(nnAccMem);
    free(g_bookFileBuf);
    return 0;
}
