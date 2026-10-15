/**********************************************************************************************
*
*   raylib [app] example - Simple Text Editor
*
*   A minimal multi-line text editor using only the public raylib API (+ a tiny Win32
*   ANSI file dialog for open/save on Windows).
*
*   Features:
*     - Dynamic multi-line buffer, line numbers, gutter, status bar.
*     - Cursor: arrows, Home/End, PgUp/PgDn, Ctrl+Left/Right (word jump).
*     - Edit: printable chars, Enter, Backspace, Delete, Tab (spaces).
*     - Selection: Shift+arrows, Ctrl+A select-all; Ctrl+C/X/V copy/cut/paste.
*     - Files: Ctrl+S save, Ctrl+O open, Ctrl+N new. Optional argv[1] to open at start.
*
*   Controls:
*     Arrows/Home/End/PgUp/PgDn    move cursor
*     Shift+<move>                 select
*     Ctrl+Left/Right              jump by word
*     Ctrl+A / Ctrl+C / Ctrl+X / Ctrl+V   select-all / copy / cut / paste
*     Ctrl+S / Ctrl+O / Ctrl+N     save / open / new
*     F1                           toggle debug overlay
*     ESC                          quit
*
**********************************************************************************************/

#include "raylib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "app_dpi.h"

#define MAX_LINES     20000
#define MAX_LINE_LEN   4000
#define TAB_SIZE       4
#define GUTTER_W       48
#define TOP_PAD         8
#define BOT_PAD        24
#define MENU_H         22   // top menu bar height
#define LEFT_PAD       4

typedef struct { char *text; int len; int cap; } Line;

static Line lines[MAX_LINES];
static int  lineCount = 0;
static int  curRow = 0, curCol = 0;
static int  selRow = -1, selCol = -1;
static bool modified = false;
static char fileName[1024] = "untitled.txt";

static int  scrollRow = 0, scrollCol = 0;
static int  fontSize = 16;
static Font font;
static int  charW = 9;     // advance width of one ASCII cell (measured at startup)

// ---- menu ----
#define MENU_COUNT   3
static const char *menuTitles[MENU_COUNT] = { "File", "Edit", "Help" };
#define MENU_FILE   0
#define MENU_EDIT   1
#define MENU_HELP   2
#define MAX_ITEMS   8
static const char *menuItems[MENU_COUNT][MAX_ITEMS] = {
    { "New   Ctrl+N", "Open... Ctrl+O", "Save   Ctrl+S", "---", "Exit   Alt+F4" },
    { "Cut   Ctrl+X", "Copy  Ctrl+C", "Paste Ctrl+V", "Select All Ctrl+A" },
    { "About raylib editor" }
};
static const int menuItemCount[MENU_COUNT] = { 5, 4, 1 };
static int  menuOpen = -1;      // -1 = none, else index of open menu
static int  menuSel = 0;        // highlighted item in open menu
static bool menuKbActive = false; // menu navigation via keyboard

// ---- line helpers ----
static void LineInit(Line *l, const char *s) {
    int n = s ? (int)strlen(s) : 0;
    l->cap = n + 16; if (l->cap < 32) l->cap = 32;
    l->text = (char *)malloc(l->cap);
    if (s && n) memcpy(l->text, s, n);
    l->text[n] = '\0'; l->len = n;
}
static void LineFree(Line *l) { free(l->text); l->text = NULL; l->len = l->cap = 0; }
static void LineEnsureCap(Line *l, int need) {
    if (need + 1 <= l->cap) return;
    while (l->cap < need + 1) l->cap *= 2;
    l->text = (char *)realloc(l->text, l->cap);
}
static void LineInsert(Line *l, int at, char c) {
    if (at < 0) at = 0;
    if (at > l->len) at = l->len;
    LineEnsureCap(l, l->len + 1);
    memmove(l->text + at + 1, l->text + at, l->len - at + 1);
    l->text[at] = c; l->len++;
}
static void LineDelete(Line *l, int at) {
    if (at < 0 || at >= l->len) return;
    memmove(l->text + at, l->text + at + 1, l->len - at);
    l->len--;
}
static void LineAppendStr(Line *l, const char *s, int n) {
    if (n <= 0) return;
    LineEnsureCap(l, l->len + n);
    memcpy(l->text + l->len, s, n); l->len += n; l->text[l->len] = '\0';
}

// ---- UTF-8 helpers (forward decls; defined later) ----
static int  Utf8LenAt(Line *l, int i);
static int  Utf8StartBefore(Line *l, int i);

// ---- document ----
static void DocClear(void) {
    for (int i = 0; i < lineCount; i++) LineFree(&lines[i]);
    lineCount = 0;
    LineInit(&lines[lineCount++], "");
    curRow = curCol = 0; selRow = selCol = -1; modified = false;
    strcpy(fileName, "untitled.txt");
}
static void DocInsertNewline(void) {
    if (lineCount >= MAX_LINES) return;
    Line *cur = &lines[curRow];
    char tail[MAX_LINE_LEN];
    int tailLen = cur->len - curCol;
    if (tailLen > 0) memcpy(tail, cur->text + curCol, tailLen);
    tail[tailLen] = '\0';
    cur->text[curCol] = '\0'; cur->len = curCol;
    for (int i = lineCount; i > curRow + 1; i--) lines[i] = lines[i - 1];
    LineInit(&lines[curRow + 1], tail);
    lineCount++; curRow++; curCol = 0; modified = true;
}
static void DocInsertChar(char c) {
    if (c == '\n') { DocInsertNewline(); return; }
    if (c == '\r') return;
    Line *cur = &lines[curRow];
    if (cur->len >= MAX_LINE_LEN - 1) return;
    LineInsert(cur, curCol, c); curCol++; modified = true;
}
static void DocDeleteBack(void) {
    if (curCol > 0) {
        // delete the whole preceding codepoint (may be >1 byte)
        int start = Utf8StartBefore(&lines[curRow], curCol - 1);
        int n = curCol - start;
        for (int k = 0; k < n; k++) LineDelete(&lines[curRow], start);
        curCol = start;
        modified = true;
    } else if (curRow > 0) {
        Line *prev = &lines[curRow - 1], *cur = &lines[curRow];
        curCol = prev->len;
        LineAppendStr(prev, cur->text, cur->len);
        LineFree(cur);
        for (int i = curRow; i < lineCount - 1; i++) lines[i] = lines[i + 1];
        lineCount--; curRow--; modified = true;
    }
}
static void DocDeleteForward(void) {
    Line *cur = &lines[curRow];
    if (curCol < cur->len) {
        // delete the whole codepoint at the cursor
        int n = Utf8LenAt(cur, curCol);
        for (int k = 0; k < n; k++) LineDelete(cur, curCol);
        modified = true;
    } else if (curRow < lineCount - 1) {
        Line *nxt = &lines[curRow + 1];
        LineAppendStr(cur, nxt->text, nxt->len);
        LineFree(nxt);
        for (int i = curRow + 1; i < lineCount - 1; i++) lines[i] = lines[i + 1];
        lineCount--; modified = true;
    }
}

// ---- selection ----
static bool HasSel(void) { return selRow >= 0; }
static void ClearSel(void) { selRow = selCol = -1; }
static void SelBounds(int *sR, int *sC, int *eR, int *eC) {
    if (selRow < 0) { *sR = *eR = curRow; *sC = *eC = curCol; return; }
    if (selRow < curRow || (selRow == curRow && selCol <= curCol)) {
        *sR = selRow; *sC = selCol; *eR = curRow; *eC = curCol;
    } else { *sR = curRow; *sC = curCol; *eR = selRow; *eC = selCol; }
}
static char *SelCopy(void) {
    int sR, sC, eR, eC; SelBounds(&sR, &sC, &eR, &eC);
    if (sR == eR && sC == eC) return NULL;
    int total = 0;
    for (int r = sR; r <= eR; r++) {
        int from = (r == sR) ? sC : 0;
        int to   = (r == eR) ? eC : lines[r].len;
        total += (to - from) + 1;
    }
    char *buf = (char *)malloc(total + 1);
    int p = 0;
    for (int r = sR; r <= eR; r++) {
        int from = (r == sR) ? sC : 0;
        int to   = (r == eR) ? eC : lines[r].len;
        int n = to - from;
        if (n > 0) memcpy(buf + p, lines[r].text + from, n);
        p += n;
        if (r < eR) buf[p++] = '\n';
    }
    buf[p] = '\0';
    return buf;
}
static void SelDelete(void) {
    int sR, sC, eR, eC; SelBounds(&sR, &sC, &eR, &eC);
    if (sR == eR && sC == eC) { ClearSel(); return; }
    if (sR == eR) {
        Line *l = &lines[sR];
        memmove(l->text + sC, l->text + eC, l->len - eC + 1);
        l->len -= (eC - sC);
    } else {
        Line *first = &lines[sR], *last = &lines[eR];
        int tailLen = last->len - eC;
        char tail[MAX_LINE_LEN];
        if (tailLen > 0) memcpy(tail, last->text + eC, tailLen);
        tail[tailLen] = '\0';
        first->text[sC] = '\0'; first->len = sC;
        LineAppendStr(first, tail, tailLen);
        for (int r = sR + 1; r <= eR; r++) LineFree(&lines[r]);
        int shift = eR - sR;
        for (int r = sR + 1; r + shift < lineCount; r++) lines[r] = lines[r + shift];
        lineCount -= shift;
    }
    curRow = sR; curCol = sC; ClearSel(); modified = true;
}
static void SelPaste(const char *text) {
    SelDelete();
    for (const char *p = text; *p; p++) {
        if (*p == '\n') DocInsertNewline();
        else if (*p == '\r') continue;
        else DocInsertChar(*p);
    }
}
static void SelectAll(void) {
    selRow = 0; selCol = 0;
    curRow = lineCount - 1; curCol = lines[curRow].len;
}

// ---- cursor movement ----
static int ClampCol(int row, int col) {
    if (col < 0) col = 0;
    if (col > lines[row].len) col = lines[row].len;
    return col;
}
static int CurVisualCol(void) {
    int v = 0; Line *l = &lines[curRow];
    int i = 0;
    while (i < curCol) {
        int sz = 0;
        int cp = GetCodepoint(l->text + i, &sz);
        if (cp == 0) cp = '?';
        if (cp == '\t') v += (TAB_SIZE - (v % TAB_SIZE));
        else if (cp >= 0x4E00 && cp <= 0x9FFF) v += 2;
        else if (cp >= 0x3000 && cp <= 0x30FF) v += 2;
        else v += 1;
        i += (sz > 0) ? sz : 1;
    }
    return v;
}
static int VisualColToCharCol(int row, int visCol) {
    Line *l = &lines[row];
    int v = 0, c = 0;
    while (c < l->len) {
        int sz = 0;
        int cp = GetCodepoint(l->text + c, &sz);
        if (cp == 0) cp = '?';
        int step;
        if (cp == '\t') step = (TAB_SIZE - (v % TAB_SIZE));
        else if (cp >= 0x4E00 && cp <= 0x9FFF) step = 2;
        else if (cp >= 0x3000 && cp <= 0x30FF) step = 2;
        else step = 1;
        if (v + step > visCol) break;
        v += step; c += (sz > 0) ? sz : 1;
    }
    return c;
}

// UTF-8 byte is a continuation byte (10xxxxxx)?
static inline bool IsUtf8Cont(unsigned char b) { return (b & 0xC0) == 0x80; }
// bytes consumed by the UTF-8 codepoint STARTING at index i in line l
static int Utf8LenAt(Line *l, int i) {
    if (i < 0 || i >= l->len) return 0;
    int n = 1;
    while (i + n < l->len && IsUtf8Cont((unsigned char)l->text[i + n])) n++;
    return n;
}
// byte offset of the codepoint that starts at or before index i (go to its start)
static int Utf8StartBefore(Line *l, int i) {
    while (i > 0 && IsUtf8Cont((unsigned char)l->text[i])) i--;
    return i;
}

static void MoveLeftSel(bool sel, bool word) {
    if (!sel) ClearSel();
    if (curCol > 0) {
        // step back to the previous codepoint start
        int i = Utf8StartBefore(&lines[curRow], curCol - 1);
        curCol = i;
        if (word) while (curCol > 0 && lines[curRow].text[curCol-1]==' ') {
            curCol = Utf8StartBefore(&lines[curRow], curCol - 1);
        }
    } else if (curRow > 0) { curRow--; curCol = lines[curRow].len; }
}
static void MoveRightSel(bool sel, bool word) {
    if (!sel) ClearSel();
    if (curCol < lines[curRow].len) {
        curCol += Utf8LenAt(&lines[curRow], curCol);
        if (word) while (curCol < lines[curRow].len && lines[curRow].text[curCol]==' ') {
            curCol += Utf8LenAt(&lines[curRow], curCol);
        }
    } else if (curRow < lineCount - 1) { curRow++; curCol = 0; }
}
static void MoveUpDownSel(bool sel, int dir, int visCol) {
    if (!sel) ClearSel();
    curRow += dir;
    if (curRow < 0) curRow = 0;
    if (curRow >= lineCount) curRow = lineCount - 1;
    curCol = VisualColToCharCol(curRow, visCol);
}
static void MovePageSel(bool sel, int dir, int visCol, int pageLines) {
    if (!sel) ClearSel();
    curRow += dir * pageLines;
    if (curRow < 0) curRow = 0;
    if (curRow >= lineCount) curRow = lineCount - 1;
    curCol = VisualColToCharCol(curRow, visCol);
}


// ---- file I/O ----
static void DocLoadFile(const char *path) {
    DocClear();
    FILE *f = fopen(path, "rb");
    if (!f) return;
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    char *buf = (char *)malloc(sz + 1);
    fread(buf, 1, sz, f); buf[sz] = '\0'; fclose(f);
    int start = 0;
    for (long i = 0; i <= sz; i++) {
        if (i == sz || buf[i] == '\n') {
            int len = (int)(i - start);
            char tmp[MAX_LINE_LEN]; int tl = 0;
            for (int k = 0; k < len; k++)
                if (buf[start + k] != '\r' && tl < MAX_LINE_LEN - 1) tmp[tl++] = buf[start + k];
            tmp[tl] = '\0';
            if (lineCount < MAX_LINES) { LineInit(&lines[lineCount], tmp); lineCount++; }
            start = (int)(i + 1);
        }
    }
    free(buf);
    curRow = curCol = 0; modified = false;
    strncpy(fileName, path, sizeof(fileName) - 1); fileName[sizeof(fileName) - 1] = '\0';
}
static void DocSaveFile(const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) return;
    for (int i = 0; i < lineCount; i++) {
        fwrite(lines[i].text, 1, lines[i].len, f);
        if (i < lineCount - 1) fputc('\n', f);
    }
    fclose(f);
    modified = false;
    strncpy(fileName, path, sizeof(fileName) - 1); fileName[sizeof(fileName) - 1] = '\0';
}

// ---- native file dialog (Windows ANSI, minimal prototypes, no <windows.h>) ----
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
#ifdef __cplusplus
extern "C"
#endif
int __stdcall GetSaveFileNameA(APP_OFNA *);
#define APP_OFN_OVERWRITEPROMPT 0x2UL
#define APP_OFN_FILEMUSTEXIST   0x1000UL
#define APP_OFN_PATHMUSTEXIST   0x800UL
#define APP_OFN_NOCHANGEDIR     0x8UL
#define APP_OFN_EXPLORER        0x80000UL

static bool NativeDialog(bool save, char *outPath, int outPathLen) {
    static char pathbuf[1024];
    pathbuf[0] = '\0';
    APP_OFNA ofn = { 0 };
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = NULL;
    ofn.lpstrFilter = "Text files\0*.txt\0All files\0*.*\0\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrFile = pathbuf;
    ofn.nMaxFile = sizeof(pathbuf);
    ofn.lpstrTitle = save ? "Save Text File" : "Open Text File";
    ofn.Flags = (save ? APP_OFN_OVERWRITEPROMPT : APP_OFN_FILEMUSTEXIST)
              | APP_OFN_PATHMUSTEXIST | APP_OFN_NOCHANGEDIR | APP_OFN_EXPLORER;
    ofn.lpstrDefExt = "txt";
    int ok = save ? GetSaveFileNameA(&ofn) : GetOpenFileNameA(&ofn);
    if (!ok) return false;
    strncpy(outPath, pathbuf, outPathLen - 1);
    outPath[outPathLen - 1] = '\0';
    return true;
}
#else
static bool NativeDialog(bool save, char *outPath, int outPathLen) { (void)save; (void)outPath; (void)outPathLen; return false; }
#endif

static bool aboutOpen = false;

// Execute a menu item. actionId encodes menu*100 + item.
static void RunMenuAction(int menu, int item)
{
    if (menu == MENU_FILE) {
        switch (item) {
            case 0: DocClear(); break;                       // New
            case 1: { char p[1024]; if (NativeDialog(false, p, sizeof(p))) DocLoadFile(p); } break; // Open
            case 2: {                                            // Save
                char p[1024];
                if (modified || strstr(fileName, "untitled") == fileName) {
                    if (NativeDialog(true, p, sizeof(p))) DocSaveFile(p);
                } else DocSaveFile(fileName);
            } break;
            case 4: CloseWindow(); break;                     // Exit
            default: break; // separator
        }
    } else if (menu == MENU_EDIT) {
        switch (item) {
            case 0: { char *t = SelCopy(); if (t){SetClipboardText(t);free(t);} SelDelete(); } break; // Cut
            case 1: { char *t = SelCopy(); if (t){SetClipboardText(t);free(t);} } break;             // Copy
            case 2: { const char *cb = GetClipboardText(); if (cb) SelPaste(cb); } break;           // Paste
            case 3: SelectAll(); break;                                                          // Select All
            default: break;
        }
    } else if (menu == MENU_HELP) {
        switch (item) {
            case 0: aboutOpen = true; break;   // About
        }
    }
}

// ---- drawing ----
int main(int argc, char **argv)
{
    app_enable_dpi();
    const int screenWidth = 1000, screenHeight = 700;
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
    InitWindow(screenWidth, screenHeight, "raylib [app] example - Simple Text Editor");
    // Load a font that covers ASCII + Latin-1 + CJK basic plane so UTF-8 text
    // (including Chinese) renders correctly. Try YaHei/SimSun first, fall back.
    {
        // Build the codepoint set we want to support.
        int cps[0xA000];
        int n = 0;
        for (int c = 0x20; c <= 0x7E; c++) cps[n++] = c;        // ASCII
        for (int c = 0xA0; c <= 0xFF; c++) cps[n++] = c;        // Latin-1 supplement
        for (int c = 0x4E00; c < 0x9FA6; c++) cps[n++] = c;     // CJK basic
        const char *tryFonts[] = {
            "C:/Windows/Fonts/msyh.ttc",
            "C:/Windows/Fonts/simsun.ttc",
            "C:/Windows/Fonts/nsimsun.ttc",
            "C:/Windows/Fonts/simhei.ttf",
            "C:/Windows/Fonts/consola.ttf"
        };
        bool loaded = false;
        for (int i = 0; i < 5 && !loaded; i++) {
            if (FileExists(tryFonts[i])) {
                font = LoadFontEx(tryFonts[i], fontSize, cps, n);
                if (font.texture.id != 0) loaded = true;
            }
        }
        if (!loaded) font = GetFontDefault();
    }
    SetTextureFilter(font.texture, TEXTURE_FILTER_BILINEAR);
    // Measure the advance width of one ASCII cell for tight, correctly-spaced rendering.
    {
        int gi = GetGlyphIndex(font, 'M');
        charW = (gi >= 0 && font.glyphs[gi].advanceX > 0) ? font.glyphs[gi].advanceX
                                                            : (int)MeasureTextEx(font, "M", fontSize, 0).x;
        if (charW < 4) charW = fontSize / 2;  // sanity fallback
    }

    SetTraceLogLevel(LOG_WARNING);
    SetTargetFPS(60);

    DocClear();
    if (argc >= 2) DocLoadFile(argv[1]);

    bool showDebug = false;

    while (!WindowShouldClose())
    {
        int sw = GetScreenWidth();
        int sh = GetScreenHeight();
        int lineH = fontSize + 4;
        int visLines = (sh - MENU_H - TOP_PAD - BOT_PAD) / lineH;
        int editW = sw - GUTTER_W - LEFT_PAD;
        int visCols = editW / charW;

        // ---- menu input ----
        Vector2 mouse = GetMousePosition();
        int menuWidth = 170;
        // figure menu title x positions (left to right)
        int titleX[MENU_COUNT];
        int tx = 6;
        for (int i = 0; i < MENU_COUNT; i++) {
            titleX[i] = tx;
            tx += (int)MeasureTextEx(font, menuTitles[i], fontSize, 0).x + 22;
        }

        bool alt = IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT);
        // Alt+F/E/H opens a menu; Esc closes; up/down/enter navigate.
        if (IsKeyPressed(KEY_LEFT_ALT) || IsKeyPressed(KEY_RIGHT_ALT)) {
            // just mark alt down; do nothing until letter pressed
        }
        if (alt) {
            if (IsKeyPressed(KEY_F)) { menuOpen = MENU_FILE; menuSel = 0; menuKbActive = true; }
            if (IsKeyPressed(KEY_E)) { menuOpen = MENU_EDIT; menuSel = 0; menuKbActive = true; }
            if (IsKeyPressed(KEY_H)) { menuOpen = MENU_HELP; menuSel = 0; menuKbActive = true; }
        }
        if (IsKeyPressed(KEY_ESCAPE)) { menuOpen = -1; menuKbActive = false; aboutOpen = false; }
        if (menuOpen >= 0 && menuKbActive) {
            if (IsKeyPressed(KEY_DOWN)) { do { menuSel++; } while (menuSel < menuItemCount[menuOpen] && menuItems[menuOpen][menuSel][0]=='-'); if (menuSel >= menuItemCount[menuOpen]) menuSel = 0; }
            if (IsKeyPressed(KEY_UP))   { do { menuSel--; if (menuSel < 0) menuSel = menuItemCount[menuOpen]-1; } while (menuItems[menuOpen][menuSel][0]=='-'); }
            if (IsKeyPressed(KEY_ENTER)) { RunMenuAction(menuOpen, menuSel); menuOpen = -1; menuKbActive = false; }
            if (IsKeyPressed(KEY_LEFT))  { menuOpen = (menuOpen + MENU_COUNT - 1) % MENU_COUNT; menuSel = 0; }
            if (IsKeyPressed(KEY_RIGHT)) { menuOpen = (menuOpen + 1) % MENU_COUNT; menuSel = 0; }
        }

        // mouse: click on a title toggles/opens that menu; click on an item runs it.
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            int clickedMenu = -1, clickedItem = -1;
            if (mouse.y < MENU_H) {
                for (int i = 0; i < MENU_COUNT; i++) {
                    int w = (int)MeasureTextEx(font, menuTitles[i], fontSize, 0).x + 22;
                    if (mouse.x >= titleX[i] && mouse.x < titleX[i] + w) { clickedMenu = i; break; }
                }
            } else if (menuOpen >= 0 &&
                       mouse.x >= titleX[menuOpen] && mouse.x < titleX[menuOpen] + menuWidth &&
                       mouse.y >= MENU_H && mouse.y < MENU_H + menuItemCount[menuOpen] * MENU_H) {
                int it = (int)((mouse.y - MENU_H) / MENU_H);
                if (it >= 0 && it < menuItemCount[menuOpen] && menuItems[menuOpen][it][0] != '-')
                    clickedItem = it;
            }
            if (clickedMenu >= 0) {
                if (menuOpen == clickedMenu) { menuOpen = -1; menuKbActive = false; } // toggle off
                else { menuOpen = clickedMenu; menuSel = 0; menuKbActive = false; }
            } else if (clickedItem >= 0) {
                RunMenuAction(menuOpen, clickedItem); menuOpen = -1; menuKbActive = false;
            } else if (mouse.y > MENU_H && mouse.y < GetScreenHeight() - BOT_PAD) {
                // clicked outside menu -> close it (but still allow editing)
                menuOpen = -1; menuKbActive = false;
            }
        }
        // mouse hover updates menuSel when not keyboard-driven
        if (menuOpen >= 0 && !menuKbActive &&
            mouse.x >= titleX[menuOpen] && mouse.x < titleX[menuOpen] + menuWidth &&
            mouse.y >= MENU_H && mouse.y < MENU_H + menuItemCount[menuOpen] * MENU_H) {
            int it = (int)((mouse.y - MENU_H) / MENU_H);
            if (it >= 0 && it < menuItemCount[menuOpen] && menuItems[menuOpen][it][0] != '-') menuSel = it;
        }

        // ---- character input ----
        // GetCharPressed returns a codepoint; encode it to UTF-8 bytes and insert
        // the whole sequence so multi-byte chars (e.g. Chinese) stay intact.
        int key;
        while ((key = GetCharPressed()) != 0) {
            if (key >= 32 && key != 127) {
                if (HasSel()) SelDelete();
                int utf8Size = 0;
                const char *utf8 = CodepointToUTF8(key, &utf8Size);
                for (int b = 0; b < utf8Size; b++) DocInsertChar(utf8[b]);
            }
        }
        // ---- key input ----
        bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
        bool ctrl  = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);

        if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) {
            if (HasSel()) SelDelete();
            DocInsertNewline();
        }
        if (IsKeyPressed(KEY_BACKSPACE)) {
            if (HasSel()) SelDelete();
            else DocDeleteBack();
        }
        if (IsKeyPressed(KEY_DELETE)) {
            if (HasSel()) SelDelete();
            else DocDeleteForward();
        }
        if (IsKeyPressed(KEY_TAB)) {
            if (HasSel()) SelDelete();
            for (int t = 0; t < TAB_SIZE; t++) DocInsertChar(' ');
        }

        int visCol = CurVisualCol();
        if (IsKeyPressed(KEY_LEFT))       MoveLeftSel(!shift, ctrl);
        if (IsKeyPressed(KEY_RIGHT))      MoveRightSel(!shift, ctrl);
        if (IsKeyPressed(KEY_UP))         MoveUpDownSel(!shift, -1, visCol);
        if (IsKeyPressed(KEY_DOWN))       MoveUpDownSel(!shift,  1, visCol);
        if (IsKeyPressed(KEY_HOME))       { if(!shift) ClearSel(); curCol=0; }
        if (IsKeyPressed(KEY_END))        { if(!shift) ClearSel(); curCol=lines[curRow].len; }
        if (IsKeyPressed(KEY_PAGE_UP))    MovePageSel(!shift, -1, visCol, visLines);
        if (IsKeyPressed(KEY_PAGE_DOWN))  MovePageSel(!shift,  1, visCol, visLines);

        // need forward decls for the move helpers
        if (ctrl && IsKeyPressed(KEY_A)) SelectAll();
        if (ctrl && IsKeyPressed(KEY_C)) { char *t = SelCopy(); if (t) { SetClipboardText(t); free(t); } }
        if (ctrl && IsKeyPressed(KEY_X)) { char *t = SelCopy(); if (t) { SetClipboardText(t); free(t); } SelDelete(); }
        if (ctrl && IsKeyPressed(KEY_V)) { const char *cb = GetClipboardText(); if (cb) SelPaste(cb); }
        if (ctrl && IsKeyPressed(KEY_S)) {
            char path[1024];
            if (modified || strstr(fileName, "untitled") == fileName) {
                if (NativeDialog(true, path, sizeof(path))) DocSaveFile(path);
            } else DocSaveFile(fileName);
        }
        if (ctrl && IsKeyPressed(KEY_O)) {
            char path[1024];
            if (NativeDialog(false, path, sizeof(path))) DocLoadFile(path);
        }
        if (ctrl && IsKeyPressed(KEY_N)) DocClear();
        if (IsKeyPressed(KEY_F1)) showDebug = !showDebug;

        // ---- fix cursor into bounds after edits ----
        if (curRow < 0) curRow = 0;
        if (curRow >= lineCount) curRow = lineCount - 1;
        curCol = ClampCol(curRow, curCol);

        // ---- scroll to keep cursor visible ----
        if (curRow < scrollRow) scrollRow = curRow;
        if (curRow >= scrollRow + visLines) scrollRow = curRow - visLines + 1;
        int cvc = CurVisualCol();
        if (cvc < scrollCol) scrollCol = cvc;
        if (cvc >= scrollCol + visCols) scrollCol = cvc - visCols + 1;
        if (scrollCol < 0) scrollCol = 0;

        // ---- draw ----
        BeginDrawing();
        ClearBackground((Color){ 24, 24, 28, 255 });

        // ---- menu bar ----
        DrawRectangle(0, 0, sw, MENU_H, (Color){ 40, 40, 48, 255 });
        DrawLine(0, MENU_H, sw, MENU_H, (Color){ 60, 60, 66, 255 });
        {
            Vector2 mp = mouse;
            int hoverMenu = -1;
            if (mp.y < MENU_H) {
                for (int i = 0; i < MENU_COUNT; i++) {
                    int w = (int)MeasureTextEx(font, menuTitles[i], fontSize, 0).x + 22;
                    if (mp.x >= titleX[i] && mp.x < titleX[i] + w) { hoverMenu = i; break; }
                }
            }
            for (int i = 0; i < MENU_COUNT; i++) {
                int w = (int)MeasureTextEx(font, menuTitles[i], fontSize, 0).x + 22;
                Color bg = (menuOpen == i || hoverMenu == i) ? (Color){ 70, 90, 160, 255 }
                                                             : (Color){ 40, 40, 48, 0 };
                if (menuOpen == i || hoverMenu == i)
                    DrawRectangle(titleX[i], 0, w, MENU_H, bg);
                Color tc = (menuOpen == i || hoverMenu == i) ? RAYWHITE : (Color){210,215,225,255};
                DrawTextEx(font, menuTitles[i],
                           (Vector2){ titleX[i] + 8, (MENU_H - fontSize) / 2 }, fontSize, 0, tc);
            }
        }
        // ---- dropdown ----
        if (menuOpen >= 0) {
            int n = menuItemCount[menuOpen];
            int mx = titleX[menuOpen];
            DrawRectangle(mx, MENU_H, menuWidth, n * MENU_H + 4, (Color){ 50, 50, 58, 255 });
            DrawRectangleLines(mx, MENU_H, menuWidth, n * MENU_H + 4, (Color){ 80, 80, 90, 255 });
            for (int i = 0; i < n; i++) {
                int iy = MENU_H + i * MENU_H;
                bool sep = (menuItems[menuOpen][i][0] == '-');
                bool sel = (i == menuSel && !sep);
                if (sel) DrawRectangle(mx, iy, menuWidth, MENU_H, (Color){ 70, 90, 160, 255 });
                if (sep) {
                    DrawLine(mx + 6, iy + MENU_H / 2, mx + menuWidth - 6, iy + MENU_H / 2,
                             (Color){ 80, 80, 90, 255 });
                } else {
                    DrawTextEx(font, menuItems[menuOpen][i],
                               (Vector2){ mx + 10, iy + (MENU_H - fontSize) / 2 },
                               fontSize, 0, RAYWHITE);
                }
            }
        }

        // ---- about dialog ----
        if (aboutOpen) {
            int bw = 360, bh = 130;
            int bx = (sw - bw) / 2, by = (sh - bh) / 2;
            DrawRectangle(0, 0, sw, sh, (Color){ 0, 0, 0, 120 });
            DrawRectangle(bx, by, bw, bh, (Color){ 44, 44, 52, 255 });
            DrawRectangleLines(bx, by, bw, bh, (Color){ 120, 130, 150, 255 });
            DrawTextEx(font, "raylib Simple Text Editor",
                       (Vector2){ bx + 16, by + 16 }, fontSize + 2, 0, RAYWHITE);
            DrawTextEx(font, "Built with raylib 5.0 + Win32 dialogs",
                       (Vector2){ bx + 16, by + 16 + fontSize + 10 }, fontSize, 0, GRAY);
            DrawTextEx(font, "Press ESC or click to close",
                       (Vector2){ bx + 16, by + bh - fontSize - 10 }, fontSize - 2, 0, Fade(RAYWHITE,0.6f));
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) aboutOpen = false;
        }

        // gutter background (below the menu bar)
        DrawRectangle(0, MENU_H, GUTTER_W, sh - MENU_H - BOT_PAD, (Color){ 30, 30, 36, 255 });
        DrawLine(GUTTER_W, MENU_H, GUTTER_W, sh - BOT_PAD, (Color){ 60, 60, 66, 255 });

        // selection highlight (drawn per visible line)
        int sR, sC, eR, eC; SelBounds(&sR, &sC, &eR, &eC);
        bool hasSel = !(sR == eR && sC == eC);

        for (int i = 0; i < visLines; i++) {
            int row = scrollRow + i;
            if (row >= lineCount) break;
            int y = MENU_H + TOP_PAD + i * lineH;

            // line number
            char num[12]; snprintf(num, sizeof(num), "%d", row + 1);
            Color numColor = (row == curRow) ? (Color){200,210,255,255} : (Color){100,100,110,255};
            DrawTextEx(font, num, (Vector2){ GUTTER_W - 6 - MeasureTextEx(font, num, fontSize, 0).x, y }, fontSize, 0, numColor);

            Line *l = &lines[row];
            // selection background for this row
            if (hasSel && row >= sR && row <= eR) {
                int from = (row == sR) ? sC : 0;
                int to   = (row == eR) ? eC : l->len;
                if (to > from) {
                    // visual columns (UTF-8 aware; CJK counts as 2 cells)
                    int vTo = 0, k = 0;
                    while (k < to) {
                        int sz=0, cp = GetCodepoint(l->text + k, &sz);
                        if (cp == 0) cp = '?';
                        if (cp=='\t') vTo += (TAB_SIZE-(vTo%TAB_SIZE));
                        else if (cp>=0x4E00 && cp<=0x9FFF) vTo += 2;
                        else if (cp>=0x3000 && cp<=0x30FF) vTo += 2;
                        else vTo += 1;
                        k += (sz>0)?sz:1;
                    }
                    int vF = 0; k = 0;
                    while (k < from) {
                        int sz=0, cp = GetCodepoint(l->text + k, &sz);
                        if (cp == 0) cp = '?';
                        if (cp=='\t') vF += (TAB_SIZE-(vF%TAB_SIZE));
                        else if (cp>=0x4E00 && cp<=0x9FFF) vF += 2;
                        else if (cp>=0x3000 && cp<=0x30FF) vF += 2;
                        else vF += 1;
                        k += (sz>0)?sz:1;
                    }
                    int x1 = GUTTER_W + LEFT_PAD + (vF - scrollCol) * charW;
                    int x2 = GUTTER_W + LEFT_PAD + (vTo - scrollCol) * charW;
                    if (x1 < GUTTER_W) x1 = GUTTER_W;
                    if (x2 > sw) x2 = sw;
                    DrawRectangle(x1, y, x2 - x1, lineH, (Color){ 70, 90, 160, 90 });
                }
            }

            // draw the line text, codepoint by codepoint (UTF-8 aware)
            int x = GUTTER_W + LEFT_PAD;
            int vcol = 0;       // visual column (in char cells; CJK counts as 2)
            int k = 0;
            while (k < l->len) {
                int cpSize = 0;
                int codepoint = GetCodepoint(l->text + k, &cpSize);
                if (codepoint == 0) codepoint = '?';
                // visual width: tab = fills to next TAB_SIZE; CJK (>=0x4E00) = 2 cells
                int visStep;
                if (codepoint == '\t') visStep = (TAB_SIZE - (vcol % TAB_SIZE));
                else if (codepoint >= 0x4E00 && codepoint <= 0x9FFF) visStep = 2;
                else if (codepoint >= 0x3000 && codepoint <= 0x30FF) visStep = 2;  // CJK punct/kana
                else visStep = 1;

                if (vcol >= scrollCol && vcol < scrollCol + visCols + 2) {
                    if (codepoint == '\t') {
                        DrawLine(x, y + lineH - 2, x + visStep * charW, y + lineH - 2, (Color){50,50,56,255});
                    } else {
                        // draw the codepoint; measure its pixel width for proper advance
                        DrawTextCodepoint(font, codepoint, (Vector2){ x, y }, fontSize, (Color){210,215,225,255});
                    }
                }
                // advance x by the visual width (so CJK aligns to a grid)
                x += visStep * charW;
                vcol += visStep;
                k += (cpSize > 0) ? cpSize : 1;
                if (vcol >= scrollCol + visCols + 4) break;
            }

            // cursor bar on current row
            if (row == curRow) {
                int cx = GUTTER_W + LEFT_PAD + (CurVisualCol() - scrollCol) * charW;
                if (cx < GUTTER_W) cx = GUTTER_W;
                DrawRectangle(cx, y, 2, lineH, (Color){230,235,255,255});
            }
        }

        // status bar
        DrawRectangle(0, sh - BOT_PAD, sw, BOT_PAD, (Color){ 36, 36, 42, 255 });
        DrawLine(0, sh - BOT_PAD, sw, sh - BOT_PAD, (Color){ 60, 60, 66, 255 });
        const char *modFlag = modified ? " *" : "";
        DrawTextEx(font, TextFormat("%s%s   row %d col %d   lines %d",
                     fileName, modFlag, curRow + 1, curCol + 1, lineCount),
                   (Vector2){ 8, sh - BOT_PAD + 5 }, fontSize, 0, RAYWHITE);
        DrawTextEx(font, "Ctrl+S save  Ctrl+O open  Ctrl+N new  Ctrl+A/C/X/V  F1 debug  ESC quit",
                   (Vector2){ sw - 460, sh - BOT_PAD + 5 }, fontSize, 0, (Color){140,140,150,255});

        if (showDebug) {
            DrawFPS(sw - 80, 4);
            DrawText(TextFormat("vis=%dx%d scroll=%d,%d", visCols, visLines, scrollCol, scrollRow),
                     8, 4, 11, GREEN);
        }
        EndDrawing();
    }

    // cleanup
    for (int i = 0; i < lineCount; i++) LineFree(&lines[i]);
    UnloadFont(font);
    CloseWindow();
    return 0;
}
