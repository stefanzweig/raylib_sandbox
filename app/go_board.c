/**********************************************************************************************
*
*   raylib [app] example - Go board with SGF playback
*
*   A 19x19 Go board that can render an SGF game record (Go Text Format) and replay it
*   move by move, honouring basic Go rules: alternating colours, no suicide, capture by
*   removing strings with no liberties, and a simple single-stone ko rule.
*
*   Controls:
*     Left click on a point  - (in EDIT mode) play a stone there
*     Left / Right arrows    - previous / next move (replay mode)
*     Home / End             - jump to start / end of the current line
*     O                      - open an SGF file via a native dialog
*     P                      - toggle Play (replay) / Edit (free) mode
*     D                      - toggle dead-stone markup (cosmetic)
*     C                      - clear the board
*     F1                     - toggle debug overlay
*     ESC                    - quit
*
*   SGF support: parses the main variation (no branching). Recognised properties:
*     SZ[n]      board size (only 19 supported here; others clamped to 19)
*     B[xy]      black play at (x=a..s, y=a..s); empty = pass
*     W[xy]      white play
*     AB[xy...]  add black stones (setup)
*     AW[xy...]  add white stones (setup)
*     RE[...]    result (displayed)
*     GN/PB/PW   game name / players (displayed)
*
**********************************************************************************************/

#include "raylib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "app_dpi.h"

#define BSIZE      19
#define MAX_MOVES  4096

// board state: 0 empty, 1 black, 2 white
static unsigned char board[BSIZE][BSIZE];
static unsigned char setupBoard[BSIZE][BSIZE];   // stones from AB/AW (displayed at move 0)

typedef enum { STONE_NONE = 0, STONE_BLACK = 1, STONE_WHITE = 2 } Stone;

typedef struct {
    Stone colour;
    int   x, y;        // -1,-1 = pass
    bool  isSetup;     // AB/AW (applied at root, not a "move")
} Move;

static Move moves[MAX_MOVES];
static int  moveCount = 0;
static int  curMove = 0;     // how many moves applied (0 = empty board + setups)

// last captured single stone (for ko)
static int koX = -1, koY = -1;

static char gameName[128]  = "";
static char blackName[64]  = "Black";
static char whiteName[64]  = "White";
static char resultStr[32]  = "";

// layout
static int   boardPx = 720;      // board pixel size (edge to edge)
static int   cellPx  = 0;        // spacing between lines
static int   originX = 0, originY = 0;
static int   stonePx = 0;
static Font  font;
static int   fontSize = 14;
static bool  editMode = false;       // free play vs replay
static bool  showDead  = false;
static bool  showDebug = false;

// ---------------------------------------------------------------------------
// board geometry
// ---------------------------------------------------------------------------
static void SetupGeometry(int sw, int sh)
{
    boardPx = (sw < sh ? sw : sh) - 60;
    if (boardPx < 200) boardPx = 200;
    cellPx  = boardPx / (BSIZE - 1);
    stonePx = cellPx - 2; if (stonePx < 4) stonePx = 4;
    originX = (sw - (BSIZE - 1) * cellPx) / 2;
    originY = (sh - (BSIZE - 1) * cellPx) / 2 - 10;
}

static void BoardToScreen(int x, int y, int *sx, int *sy)
{
    *sx = originX + x * cellPx;
    *sy = originY + y * cellPx;
}
static bool ScreenToBoard(int sx, int sy, int *bx, int *by)
{
    int x = (int)((sx - originX + cellPx / 2) / (float)cellPx);
    int y = (int)((sy - originY + cellPx / 2) / (float)cellPx);
    if (x < 0 || y < 0 || x >= BSIZE || y >= BSIZE) return false;
    int lsx, lsy; BoardToScreen(x, y, &lsx, &lsy);
    if (abs(sx - lsx) > cellPx / 2 || abs(sy - lsy) > cellPx / 2) return false;
    *bx = x; *by = y;
    return true;
}

// ---------------------------------------------------------------------------
// rules: liberty / capture
// ---------------------------------------------------------------------------
// Flood-fill the connected same-colour string containing (x,y). Sets group[] to 1
// for each stone in the string. Returns total stones; counts liberties (empty
// neighbours) into *libCount.
static int visited[BSIZE][BSIZE];
static int FloodString(int x, int y, Stone colour, int group[BSIZE][BSIZE], int *libCount)
{
    static const int dx[4] = {1,-1,0,0};
    static const int dy[4] = {0,0,1,-1};
    int count = 0;
    *libCount = 0;
    for (int i = 0; i < BSIZE; i++) for (int j = 0; j < BSIZE; j++) { visited[i][j] = 0; group[i][j] = 0; }
    // simple stack-based flood
    int stackX[BSIZE*BSIZE], stackY[BSIZE*BSIZE], sp = 0;
    stackX[sp]=x; stackY[sp]=y; sp++;
    visited[y][x] = 1; group[y][x] = 1; count++;
    while (sp > 0) {
        sp--;
        int cx = stackX[sp], cy = stackY[sp];
        for (int d = 0; d < 4; d++) {
            int nx = cx + dx[d], ny = cy + dy[d];
            if (nx < 0 || ny < 0 || nx >= BSIZE || ny >= BSIZE) continue;
            if (visited[ny][nx]) continue;
            if (board[ny][nx] == 0) { (*libCount)++; visited[ny][nx] = 1; continue; }
            if (board[ny][nx] == colour) {
                visited[ny][nx] = 1; group[ny][nx] = 1; count++;
                stackX[sp]=nx; stackY[sp]=ny; sp++;
            } else {
                visited[ny][nx] = 1;  // opponent, not part of string; mark to skip
            }
        }
    }
    return count;
}

// Try to play a stone at (x,y). Returns true if legal; applies capture.
static bool TryPlay(int x, int y, Stone colour, char *err)
{
    err[0] = '\0';
    if (x < 0 || y < 0 || x >= BSIZE || y >= BSIZE) { strcpy(err, "off board"); return false; }
    if (board[y][x] != 0) { strcpy(err, "point occupied"); return false; }
    // ko check: cannot recapture immediately
    if (x == koX && y == koY) { strcpy(err, "ko violation"); return false; }

    board[y][x] = colour;
    Stone opp = (colour == STONE_BLACK) ? STONE_WHITE : STONE_BLACK;

    // For each opponent neighbour, remove strings with no liberties.
    static const int dx[4] = {1,-1,0,0};
    static const int dy[4] = {0,0,1,-1};
    int totalCaptured = 0;
    int capturedSingleX = -1, capturedSingleY = -1;
    for (int d = 0; d < 4; d++) {
        int nx = x + dx[d], ny = y + dy[d];
        if (nx < 0 || ny < 0 || nx >= BSIZE || ny >= BSIZE) continue;
        if (board[ny][nx] != opp) continue;
        int libs = 0;
        int grp[BSIZE][BSIZE];
        FloodString(nx, ny, opp, grp, &libs);
        if (libs == 0) {
            // remove this group
            for (int i = 0; i < BSIZE; i++)
                for (int j = 0; j < BSIZE; j++)
                    if (grp[i][j]) {
                        board[i][j] = 0;
                        totalCaptured++;
                        capturedSingleX = j; capturedSingleY = i;
                    }
        }
    }
    // suicide check: our own string must have liberties
    {
        int libs = 0; int grp[BSIZE][BSIZE];
        FloodString(x, y, colour, grp, &libs);
        if (libs == 0) {
            // suicide: undo
            board[y][x] = 0;
            strcpy(err, "suicide");
            // NOTE: we already removed opponent strings but if we had no liberties
            // that means we captured nothing, so nothing to restore.
            return false;
        }
    }
    // ko: if we captured exactly one stone and our stone is a single stone with one liberty
    koX = koY = -1;
    if (totalCaptured == 1) {
        int libs = 0; int grp[BSIZE][BSIZE];
        int n = FloodString(x, y, colour, grp, &libs);
        if (n == 1 && libs == 1) { koX = capturedSingleX; koY = capturedSingleY; }
    }
    return true;
}

// ---------------------------------------------------------------------------
// applying the move list up to curMove
// ---------------------------------------------------------------------------
static void RebuildBoard(void)
{
    for (int y = 0; y < BSIZE; y++) for (int x = 0; x < BSIZE; x++) board[y][x] = setupBoard[y][x];
    koX = koY = -1;
    int n = (curMove < moveCount) ? curMove : moveCount;
    for (int i = 0; i < n; i++) {
        Move m = moves[i];
        if (m.isSetup) continue;     // setups already on setupBoard
        if (m.x < 0 || m.y < 0) { koX = koY = -1; continue; }  // pass
        char err[32];
        TryPlay(m.x, m.y, m.colour, err);   // recompute captures/ko
    }
}

// ---------------------------------------------------------------------------
// SGF parsing (main variation only)
// ---------------------------------------------------------------------------
static int SgfCoordToXY(const char *s, int *x, int *y)
{
    if (!s || !s[0] || !s[1]) { *x = *y = -1; return 0; }   // pass / empty
    *x = s[0] - 'a';
    *y = s[1] - 'a';
    if (*x < 0 || *x >= BSIZE || *y < 0 || *y >= BSIZE) { *x = *y = -1; return 0; }
    return 1;
}

// parse a single property value list like [pd][dp] into a sequence of (x,y) pairs
typedef struct { int x, y; } Pt;
static int ParseSgfPoints(const char *val, Pt *out, int maxOut)
{
    int n = 0;
    const char *p = val;
    while (*p && n < maxOut) {
        while (*p && *p != '[') p++;
        if (*p != '[') break;
        p++;  // skip [
        char buf[8] = {0}; int bl = 0;
        while (*p && *p != ']' && bl < 7) buf[bl++] = *p++;
        if (*p == ']') p++;
        int x, y; SgfCoordToXY(buf, &x, &y);
        out[n].x = x; out[n].y = y; n++;
    }
    return n;
}

// bounded copy that always NUL-terminates; returns length copied.
static int SafeCopy(char *dst, const char *src, size_t cap)
{
    if (cap == 0) return 0;
    size_t i = 0;
    for (; i + 1 < cap && src[i]; i++) dst[i] = src[i];
    dst[i] = '\0';
    return (int)i;
}

static void ParseSGF(const char *src)
{
    moveCount = 0; curMove = 0;
    for (int y = 0; y < BSIZE; y++) for (int x = 0; x < BSIZE; x++) setupBoard[y][x] = 0;
    gameName[0] = blackName[0] = whiteName[0] = resultStr[0] = '\0';
    strcpy(blackName, "Black"); strcpy(whiteName, "White");

    const char *p = src;
    // find '('
    while (*p && *p != '(') p++;
    if (!*p) return;
    p++; // past '('
    // expect ';'
    while (*p && *p != ';') p++;

    // tokens: we look for ; then a property id then [..] values, repeat.
    while (*p) {
        // skip whitespace, but '(' starts a sub-variation we skip to its matching ')'
        while (*p && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')) p++;
        if (*p == ')') break;
        if (*p == '(') {
            // skip nested variation
            int depth = 1; p++;
            while (*p && depth > 0) {
                if (*p == '(') depth++;
                else if (*p == ')') depth--;
                p++;
            }
            continue;
        }
        if (*p == ';') { p++; continue; }

        // read property id (uppercase letters)
        char id[8] = {0}; int il = 0;
        while (*p && *p >= 'A' && *p <= 'Z' && il < 7) id[il++] = *p++;
        if (il == 0) { p++; continue; }   // unknown char, skip

        // read all [..] values for this property
        char values[4096]; int vl = 0;
        while (*p == '[') {
            p++;
            while (*p && *p != ']' && vl < (int)sizeof(values) - 2) {
                if (*p == '\\') { p++; if (*p) values[vl++] = *p++; }
                else values[vl++] = *p++;
            }
            if (*p == ']') p++;
            if (*p == '[') values[vl++] = ',';   // separate multiple values
        }
        values[vl] = '\0';

        // interpret
        if (strcmp(id, "SZ") == 0) {
            // board size; we only support 19
        } else if (strcmp(id, "GN") == 0) {
            SafeCopy(gameName, values, sizeof(gameName));
        } else if (strcmp(id, "PB") == 0) {
            SafeCopy(blackName, values, sizeof(blackName));
        } else if (strcmp(id, "PW") == 0) {
            SafeCopy(whiteName, values, sizeof(whiteName));
        } else if (strcmp(id, "RE") == 0) {
            SafeCopy(resultStr, values, sizeof(resultStr));
        } else if (strcmp(id, "AB") == 0 || strcmp(id, "AW") == 0 || strcmp(id, "AE") == 0) {
            Stone c = (id[1]=='B') ? STONE_BLACK : (id[1]=='W' ? STONE_WHITE : 0);
            Pt pts[256]; int n = ParseSgfPoints(values, pts, 256);
            for (int i = 0; i < n; i++) {
                if (pts[i].x >= 0) setupBoard[pts[i].y][pts[i].x] = (unsigned char)c;
            }
        } else if (strcmp(id, "B") == 0 || strcmp(id, "W") == 0) {
            Stone c = (id[0]=='B') ? STONE_BLACK : STONE_WHITE;
            int x, y; SgfCoordToXY(values, &x, &y);
            if (moveCount < MAX_MOVES) {
                moves[moveCount].colour = c;
                moves[moveCount].x = x;
                moves[moveCount].y = y;
                moves[moveCount].isSetup = false;
                moveCount++;
            }
        }
        // else: ignore other properties
    }
    RebuildBoard();
}

// ---------------------------------------------------------------------------
// file dialog (reuse text_editor's approach, ANSI Win32)
// ---------------------------------------------------------------------------
#if defined(_WIN32) || defined(__CYGWIN__)
typedef struct {
    unsigned long lStructSize; void *hwndOwner; void *hInstance;
    const char *lpstrFilter; char *lpstrCustomFilter; unsigned long nMaxCustFilter;
    unsigned long nFilterIndex; char *lpstrFile; unsigned long nMaxFile;
    char *lpstrFileTitle; unsigned long nMaxFileTitle;
    const char *lpstrInitialDir; const char *lpstrTitle; unsigned long Flags;
    unsigned short nFileOffset; unsigned short nFileExtension;
    const char *lpstrDefExt; void *lCustData; void *lpfnHook;
    const char *lpTemplateName; void *pvReserved; unsigned long dwReserved;
    unsigned long FlagsEx;
} APP_OFNA;
#ifdef __cplusplus
extern "C"
#endif
int __stdcall GetOpenFileNameA(APP_OFNA *);
#define APP_OFN_FILEMUSTEXIST 0x1000UL
#define APP_OFN_PATHMUSTEXIST 0x800UL
#define APP_OFN_NOCHANGEDIR   0x8UL
#define APP_OFN_EXPLORER      0x80000UL
static bool NativeOpen(char *out, int outLen) {
    static char path[1024]; path[0] = '\0';
    APP_OFNA ofn = {0};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = "SGF files\0*.sgf\0All files\0*.*\0\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrFile = path;
    ofn.nMaxFile = sizeof(path);
    ofn.lpstrTitle = "Open SGF Game";
    ofn.Flags = APP_OFN_FILEMUSTEXIST | APP_OFN_PATHMUSTEXIST | APP_OFN_NOCHANGEDIR | APP_OFN_EXPLORER;
    if (!GetOpenFileNameA(&ofn)) return false;
    SafeCopy(out, path, outLen);
    return true;
}
#else
static bool NativeOpen(char *out, int outLen) { (void)out; (void)outLen; return false; }
#endif

static char loadedFile[1024] = "";

static void OpenSGF(void)
{
    char path[1024];
    if (!NativeOpen(path, sizeof(path))) return;
    FILE *f = fopen(path, "rb");
    if (!f) return;
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    char *buf = (char *)malloc(sz + 1);
    fread(buf, 1, sz, f); buf[sz] = '\0'; fclose(f);
    ParseSGF(buf);
    free(buf);
    SafeCopy(loadedFile, path, sizeof(loadedFile));
    editMode = false;
}

// ---------------------------------------------------------------------------
// drawing
// ---------------------------------------------------------------------------
static void DrawBoard(void)
{
    // wood background
    DrawRectangle(originX - cellPx, originY - cellPx,
                  (BSIZE + 1) * cellPx, (BSIZE + 1) * cellPx,
                  (Color){ 219, 178, 120, 255 });

    Color lineCol = { 60, 40, 20, 255 };
    // grid lines
    for (int i = 0; i < BSIZE; i++) {
        int p = originX + i * cellPx;
        DrawLine(p, originY, p, originY + (BSIZE - 1) * cellPx, lineCol);
        int q = originY + i * cellPx;
        DrawLine(originX, q, originX + (BSIZE - 1) * cellPx, q, lineCol);
    }
    // star points (hoshi): 4-4 etc.
    static const int stars[9][2] = {
        {3,3},{9,3},{15,3},{3,9},{9,9},{15,9},{3,15},{9,15},{15,15}
    };
    int r = cellPx / 6; if (r < 3) r = 3;
    for (int i = 0; i < 9; i++) {
        int sx, sy; BoardToScreen(stars[i][0], stars[i][1], &sx, &sy);
        DrawCircle(sx, sy, r, lineCol);
    }

    // coordinates
    char lab[3];
    Color labCol = { 90, 60, 30, 255 };
    for (int i = 0; i < BSIZE; i++) {
        snprintf(lab, sizeof(lab), "%c", 'A' + (i < 8 ? i : i + 1)); // skip I
        DrawText(lab, originX + i * cellPx - 4, originY - cellPx + 4, fontSize - 2, labCol);
        DrawText(lab, originX + i * cellPx - 4, originY + (BSIZE - 1) * cellPx + 6, fontSize - 2, labCol);
        snprintf(lab, sizeof(lab), "%d", BSIZE - i);
        DrawText(lab, originX - cellPx + 4, originY + i * cellPx - 6, fontSize - 2, labCol);
        DrawText(lab, originX + (BSIZE - 1) * cellPx + 6, originY + i * cellPx - 6, fontSize - 2, labCol);
    }

    // stones
    for (int y = 0; y < BSIZE; y++) {
        for (int x = 0; x < BSIZE; x++) {
            if (board[y][x] == 0) continue;
            int sx, sy; BoardToScreen(x, y, &sx, &sy);
            if (board[y][x] == STONE_BLACK) {
                DrawCircle(sx + 1, sy + 1, stonePx / 2, (Color){ 30, 25, 20, 90 });   // shadow
                DrawCircle(sx, sy, stonePx / 2, (Color){ 20, 18, 18, 255 });
            } else {
                DrawCircle(sx + 1, sy + 1, stonePx / 2, (Color){ 30, 25, 20, 90 });
                DrawCircle(sx, sy, stonePx / 2, (Color){ 235, 232, 226, 255 });
                DrawCircleLines(sx, sy, stonePx / 2, (Color){ 120, 110, 95, 255 });
            }
        }
    }

    // last move marker
    if (curMove > 0 && curMove <= moveCount) {
        Move m = moves[curMove - 1];
        if (m.x >= 0 && m.y >= 0) {
            int sx, sy; BoardToScreen(m.x, m.y, &sx, &sy);
            Color mc = (board[m.y][m.x] == STONE_BLACK) ? (Color){235,232,226,255}
                                                        : (Color){20,18,18,255};
            DrawCircleLines(sx, sy, stonePx / 4, mc);
            DrawCircle(sx, sy, 2, mc);
        }
    }
}

int main(int argc, char **argv)
{
    app_enable_dpi();
    const int screenWidth = 800, screenHeight = 820;
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
    InitWindow(screenWidth, screenHeight, "raylib [app] example - Go board + SGF");
    font = LoadFontEx("C:/Windows/Fonts/consola.ttf", fontSize, NULL, 0);
    if (font.texture.id == 0) font = GetFontDefault();

    SetTraceLogLevel(LOG_WARNING);
    SetTargetFPS(60);

    SetupGeometry(GetScreenWidth(), GetScreenHeight());
    RebuildBoard();

    // Load an SGF file given as a command-line argument (no dialog).
    if (argc >= 2) {
        FILE *f = fopen(argv[1], "rb");
        if (f) {
            fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
            char *buf = (char *)malloc(sz + 1);
            fread(buf, 1, sz, f); buf[sz] = '\0'; fclose(f);
            ParseSGF(buf); free(buf);
            SafeCopy(loadedFile, argv[1], sizeof(loadedFile));
            editMode = false;
        }
    }

    while (!WindowShouldClose())
    {
        int sw = GetScreenWidth(), sh = GetScreenHeight();
        SetupGeometry(sw, sh);

        if (IsKeyPressed(KEY_O)) OpenSGF();
        if (IsKeyPressed(KEY_P)) editMode = !editMode;
        if (IsKeyPressed(KEY_C)) {
            moveCount = 0; curMove = 0;
            for (int y = 0; y < BSIZE; y++) for (int x = 0; x < BSIZE; x++) setupBoard[y][x] = 0;
            RebuildBoard();
        }
        if (IsKeyPressed(KEY_D)) showDead = !showDead;
        if (IsKeyPressed(KEY_F1)) showDebug = !showDebug;

        // replay navigation
        if (IsKeyPressed(KEY_RIGHT)) { if (curMove < moveCount) { curMove++; RebuildBoard(); } }
        if (IsKeyPressed(KEY_LEFT))  { if (curMove > 0)         { curMove--; RebuildBoard(); } }
        if (IsKeyPressed(KEY_HOME))  { curMove = 0; RebuildBoard(); }
        if (IsKeyPressed(KEY_END))   { curMove = moveCount; RebuildBoard(); }

        // edit mode: click to play
        if (editMode) {
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                int bx, by;
                if (ScreenToBoard(GetMouseX(), GetMouseY(), &bx, &by)) {
                    Stone c = (curMove % 2 == 0) ? STONE_BLACK : STONE_WHITE;
                    char err[32];
                    if (TryPlay(bx, by, c, err)) {
                        if (moveCount < MAX_MOVES) {
                            moves[moveCount].colour = c;
                            moves[moveCount].x = bx;
                            moves[moveCount].y = by;
                            moves[moveCount].isSetup = false;
                            moveCount++;
                            curMove = moveCount;
                        }
                    }
                }
            }
        }

        BeginDrawing();
        ClearBackground((Color){ 60, 45, 30, 255 });
        DrawBoard();

        // HUD
        const char *mode = editMode ? "EDIT (free play)" : "REPLAY";
        DrawText(TextFormat("%s   move %d/%d   %s vs %s   %s",
                            mode, curMove, moveCount, blackName, whiteName,
                            resultStr[0] ? resultStr : ""),
                 10, 8, 13, RAYWHITE);
        const char *hint = editMode
            ? "click to play   P replay mode   O open SGF   C clear   F1 debug   ESC quit"
            : "<- / ->  prev/next   Home/End  start/end   P edit mode   O open   C clear   F1 debug   ESC quit";
        DrawText(hint, 10, sh - 20, 11, (Color){210,210,210,255});
        if (loadedFile[0]) {
            const char *fn = loadedFile;
            // show basename
            for (const char *q = loadedFile; *q; q++) if (*q=='/'||*q=='\\') fn = q+1;
            DrawText(TextFormat("file: %s", fn), 10, 26, 11, (Color){200,180,140,255});
        }

        if (showDebug) {
            DrawFPS(sw - 80, 8);
            DrawText(TextFormat("ko=%d,%d  cellPx=%d  stone=%d  dead=%d", koX, koY, cellPx, stonePx, showDead),
                     10, 42, 11, GREEN);
        }

        EndDrawing();
    }

    CloseWindow();
    return 0;
}
