/*******************************************************************************************
 *
 *   Go (Weiqi/Baduk) Board Viewer with SGF Support
 *
 *   Features:
 *     - Load and display SGF format game records
 *     - Step through moves with keyboard controls
 *     - Correct Go capture rules (liberty counting, dead stone removal)
 *     - Suicide prevention
 *     - Auto-play mode
 *     - Support for any board size (9x9, 13x13, 19x19, etc.)
 *
 *   Controls:
 *     Right / Space  : Next move
 *     Left           : Previous move
 *     Home           : Jump to start
 *     End            : Jump to end
 *     F              : Toggle auto-play
 *     Up / Down      : Adjust auto-play speed
 *     ESC            : Quit
 *
 *   Usage: ./go_board [path/to/game.sgf]
 *
 ********************************************************************************************/

#include "raylib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

#define MAX_BOARD_SIZE 25
#define MAX_STEPS      1000
#define MAX_STONES     (MAX_BOARD_SIZE * MAX_BOARD_SIZE)
#define DEFAULT_BOARD_SIZE 19

/* ---- Data types ---- */

typedef enum { STONE_EMPTY, STONE_BLACK, STONE_WHITE } StoneColor;

typedef struct {
    int x, y;
} Point;

typedef struct {
    int  played;          /* 1 = move, 2 = add, 0 = pass/none */
    int  color;           /* STONE_BLACK or STONE_WHITE */
    int  x, y;            /* board coords (-1 for pass) */
    int  is_variation;    /* 1 if this step is from a variation branch */
} Step;

typedef struct {
    int size;
    StoneColor board[MAX_BOARD_SIZE][MAX_BOARD_SIZE];
    int step_count;
    Step   steps[MAX_STEPS];
    int    current_step;  /* 0 = initial, 1..step_count = after move N */
    int    move_number;   /* actual move count (excl. setup) */
    int    last_capture_count;
    char   sgf_filename[256];
} GameState;

/* ---- Globals for board layout ---- */
static int board_px, board_py, cell_sz, stone_r;

/* ---- Board helpers ---- */

static void board_clear(GameState *gs)
{
    for (int y = 0; y < gs->size; y++)
        for (int x = 0; x < gs->size; x++)
            gs->board[y][x] = STONE_EMPTY;
    gs->current_step = 0;
    gs->last_capture_count = 0;
}

static int in_bounds(GameState *gs, int x, int y)
{
    return x >= 0 && x < gs->size && y >= 0 && y < gs->size;
}

/* Size-aware bounds check */
static inline int in_bounds_raw(int size, int x, int y)
{
    return x >= 0 && x < size && y >= 0 && y < size;
}

/* ---- Flood-fill: find connected group of same color ---- */

static void find_group(StoneColor board[MAX_BOARD_SIZE][MAX_BOARD_SIZE],
                       int size, int sx, int sy,
                       Point *grp, int *gn,
                       Point *libs, int *ln)
{
    StoneColor color = board[sy][sx];
    int visited[MAX_BOARD_SIZE][MAX_BOARD_SIZE];
    memset(visited, 0, sizeof(visited));

    Point queue[MAX_STONES];
    int head = 0, tail = 0;

    queue[tail++] = (Point){sx, sy};
    visited[sy][sx] = 1;
    *gn = 0;
    *ln = 0;

    static const int dx[4] = {0, 0, 1, -1};
    static const int dy[4] = {1, -1, 0, 0};

    while (head < tail) {
        Point p = queue[head++];
        grp[(*gn)++] = p;

        for (int d = 0; d < 4; d++) {
            int nx = p.x + dx[d];
            int ny = p.y + dy[d];
            if (!in_bounds_raw(size, nx, ny)) continue;
            if (visited[ny][nx]) continue;

            if (board[ny][nx] == STONE_EMPTY) {
                /* mark liberty (deduplicate via visited) */
                visited[ny][nx] = 1;
                if (*ln < MAX_STONES) libs[(*ln)++] = (Point){nx, ny};
            } else if (board[ny][nx] == color) {
                visited[ny][nx] = 1;
                queue[tail++] = (Point){nx, ny};
            }
        }
    }
}

/* ---- Place a stone with full capture logic ---- */

static int place_stone(GameState *gs, int color, int x, int y)
{
    if (!in_bounds(gs, x, y))            return 0;
    if (gs->board[y][x] != STONE_EMPTY)        return 0;

    gs->board[y][x] = (StoneColor)color;

    int opp = (color == STONE_BLACK) ? STONE_WHITE : STONE_BLACK;
    int total_captured = 0;

    Point grp[MAX_STONES], libs[MAX_STONES];
    int gn, ln;

    static const int dx[4] = {0, 0, 1, -1};
    static const int dy[4] = {1, -1, 0, 0};

    /* Check each neighbour for opponent captures */
    for (int d = 0; d < 4; d++) {
        int nx = x + dx[d];
        int ny = y + dy[d];
        if (!in_bounds(gs, nx, ny))                continue;
        if (gs->board[ny][nx] != (StoneColor)opp)  continue;

        /* Already captured this group? (can happen for adjacent groups) */
        if (gs->board[ny][nx] == STONE_EMPTY) continue;

        find_group(gs->board, gs->size, nx, ny, grp, &gn, libs, &ln);
        if (ln == 0) {
            for (int i = 0; i < gn; i++)
                gs->board[grp[i].y][grp[i].x] = STONE_EMPTY;
            total_captured += gn;
        }
    }

    /* Suicide check: own group must have liberties after captures */
    find_group(gs->board, gs->size, x, y, grp, &gn, libs, &ln);
    if (ln == 0) {
        /* Illegal – undo */
        gs->board[y][x] = STONE_EMPTY;
        return 0;
    }

    gs->last_capture_count = total_captured;
    return 1;
}

/* ---- Replay board to a given step index ---- */

static void replay_to(GameState *gs, int to)
{
    board_clear(gs);
    for (int i = 0; i < to && i < gs->step_count; i++) {
        Step *s = &gs->steps[i];
        if (s->played == 1) {
            place_stone(gs, s->color, s->x, s->y);
        } else if (s->played == 2) {
            if (in_bounds(gs, s->x, s->y))
                gs->board[s->y][s->x] = (StoneColor)s->color;
        }
    }
    gs->current_step = to;
}

/* ================================================================================
 *  SGF PARSER
 * ================================================================================ */

/* Skip whitespace in buffer */
static const char *skip_ws(const char *p)
{
    while (*p && isspace((unsigned char)*p)) p++;
    return p;
}

/* Parse one SGF property value inside [...]; returns pointer past ']' */
static const char *parse_sgf_value(const char *p, char *buf, int bufsz)
{
    int n = 0;
    while (*p && *p != ']') {
        if (*p == '\\' && *(p + 1)) { p++; }   /* escaped char */
        if (n < bufsz - 1) buf[n++] = *p;
        p++;
    }
    buf[n] = '\0';
    if (*p == ']') p++;
    return p;
}

static int parse_sgf(GameState *gs, const char *filepath)
{
    FILE *f = fopen(filepath, "r");
    if (!f) { fprintf(stderr, "Cannot open: %s\n", filepath); return 0; }

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = (char *)malloc(len + 1);
    if (!buf) { fclose(f); return 0; }
    size_t rd = fread(buf, 1, len, f);
    (void)rd;
    buf[len] = '\0';
    fclose(f);

    /* Save filename for display */
    const char *slash = strrchr(filepath, '/');
    strncpy(gs->sgf_filename, slash ? slash + 1 : filepath, sizeof(gs->sgf_filename) - 1);

    const char *p = buf;

    /* Skip to first '(' */
    while (*p && *p != '(') p++;
    if (!*p) { free(buf); return 0; }
    p++;   /* past '(' */

    /* ---- Parse root node properties ---- */
    while (*p && *p != ';' && *p != '(' && *p != ')') {
        if (isspace((unsigned char)*p)) { p++; continue; }

        char prop[8] = {0};
        int  pi = 0;
        while (*p && isalpha((unsigned char)*p) && pi < 7) prop[pi++] = *p++;
        prop[pi] = '\0';

        char val[64] = {0};
        if (*p == '[') {
            p++;
            p = parse_sgf_value(p, val, sizeof(val));
        }

        if (strcmp(prop, "SZ") == 0) {
            gs->size = atoi(val);
            if (gs->size < 2) gs->size = DEFAULT_BOARD_SIZE;
            if (gs->size > MAX_BOARD_SIZE) gs->size = MAX_BOARD_SIZE;
        }
        /* Ignore other root props (GM, FF, etc.) */
    }

    /* Skip root node ';' */
    if (*p == ';') p++;

    /* ---- Parse game tree ---- */
    int in_main = 1;   /* 1 = main variation, 0 = skip variation */
    int depth = 0;
    int seen_main_child = 0;

    while (*p && gs->step_count < MAX_STEPS) {
        p = skip_ws(p);
        if (!*p) break;

        if (*p == '(') {
            depth++;
            if (depth == 1) {
                in_main = 1;
            } else if (depth == 2) {
                /* Second (or later) child at depth-1 = variation */
                if (seen_main_child) {
                    in_main = 0;
                } else {
                    seen_main_child = 1;
                    in_main = 1;
                }
            }
            p++;
            continue;
        }

        if (*p == ')') {
            depth--;
            if (depth <= 0) break;
            if (depth == 1) in_main = 1;   /* back to main variation level */
            p++;
            continue;
        }

        if (*p == ';') {
            p++;
            if (!in_main && depth >= 2) {
                /* Skip variation node */
                while (*p && *p != ';' && *p != '(' && *p != ')') {
                    while (*p && isalpha((unsigned char)*p)) p++;
                    if (*p == '[') {
                        p++;
                        while (*p && *p != ']') {
                            if (*p == '\\') p++;
                            p++;
                        }
                        if (*p == ']') p++;
                    }
                    p = skip_ws(p);
                }
                continue;
            }
            /* Parse main-line node */
            Step step = {0};
            step.is_variation = 0;

            while (*p && *p != ';' && *p != '(' && *p != ')') {
                if (isspace((unsigned char)*p)) { p++; continue; }

                char prop[8] = {0};
                int  pi = 0;
                while (*p && isalpha((unsigned char)*p) && pi < 7) prop[pi++] = *p++;
                prop[pi] = '\0';

                char val[64] = {0};
                if (*p == '[') {
                    p++;
                    p = parse_sgf_value(p, val, sizeof(val));
                }

                if (strcmp(prop, "B") == 0) {
                    if (strlen(val) >= 2) {
                        step.played = 1; step.color = STONE_BLACK;
                        step.x = val[0] - 'a'; step.y = val[1] - 'a';
                    }
                } else if (strcmp(prop, "W") == 0) {
                    if (strlen(val) >= 2) {
                        step.played = 1; step.color = STONE_WHITE;
                        step.x = val[0] - 'a'; step.y = val[1] - 'a';
                    }
                } else if (strcmp(prop, "AB") == 0) {
                    if (strlen(val) >= 2) {
                        step.played = 2; step.color = STONE_BLACK;
                        step.x = val[0] - 'a'; step.y = val[1] - 'a';
                    }
                } else if (strcmp(prop, "AW") == 0) {
                    if (strlen(val) >= 2) {
                        step.played = 2; step.color = STONE_WHITE;
                        step.x = val[0] - 'a'; step.y = val[1] - 'a';
                    }
                }
                /* Skip unknown properties */
            }

            gs->steps[gs->step_count++] = step;
            continue;
        }

        p++;   /* skip unknown char */
    }

    free(buf);
    return 1;
}

/* ================================================================================
 *  RENDERING
 * ================================================================================ */

/* Convert board coord to screen pixel */
static void board_to_screen(int bx, int by, int *sx, int *sy)
{
    *sx = board_px + bx * cell_sz;
    *sy = board_py + by * cell_sz;
}

/* Draw the wooden board with grid lines and star points */
static void draw_board(GameState *gs)
{
    int sz = gs->size;
    int bx0 = board_px, by0 = board_py;
    int bx1 = board_px + (sz - 1) * cell_sz;
    int by1 = board_py + (sz - 1) * cell_sz;
    int pad = cell_sz / 2 + 4;

    /* Wooden background */
    DrawRectangle(bx0 - pad, by0 - pad,
                  (bx1 - bx0) + 2 * pad, (by1 - by0) + 2 * pad,
                  (Color){220, 179, 92, 255});

    /* Subtle wood grain lines */
    for (int i = 0; i < 8; i++) {
        int ly = by0 - pad + (i + 1) * ((by1 - by0 + 2 * pad) / 9);
        DrawLine(bx0 - pad, ly, bx1 + pad, ly, (Color){210, 168, 80, 80});
    }

    /* Border */
    DrawRectangleLines(bx0 - pad, by0 - pad,
                       (bx1 - bx0) + 2 * pad, (by1 - by0) + 2 * pad,
                       (Color){80, 50, 20, 255});

    /* Grid lines */
    for (int i = 0; i < sz; i++) {
        int x = bx0 + i * cell_sz;
        int y = by0 + i * cell_sz;
        DrawLine(x, by0, x, by1, DARKGRAY);
        DrawLine(bx0, y, bx1, y, DARKGRAY);
    }

    /* Star points (hoshi) */
    int star_offsets_19[] = {3, 9, 15};
    int star_offsets_13[] = {3, 6, 9};
    int star_offsets_9[]  = {2, 4, 6};
    int *offs = NULL;
    int nstar = 0;
    if (sz == 19)      { offs = star_offsets_19; nstar = 3; }
    else if (sz == 13) { offs = star_offsets_13; nstar = 3; }
    else if (sz == 9)  { offs = star_offsets_9;  nstar = 3; }

    if (offs) {
        int r = cell_sz / 8 + 1;
        for (int i = 0; i < nstar; i++)
            for (int j = 0; j < nstar; j++)
                DrawCircle(bx0 + offs[i] * cell_sz,
                           by0 + offs[j] * cell_sz,
                           (float)r, DARKGRAY);
    }

    /* Coordinate labels */
    int font_sz = (cell_sz > 28) ? 12 : 10;
    const char *letters = "ABCDEFGHJKLMNOPQRST";  /* skip I per convention */
    for (int i = 0; i < sz && i < 25; i++) {
        int x = bx0 + i * cell_sz;
        int y = by0 + i * cell_sz;

        /* Top column letters */
        char ls[2] = {letters[i], '\0'};
        int tw = MeasureText(ls, font_sz);
        DrawText(ls, x - tw / 2, by0 - pad + 4, font_sz, GRAY);

        /* Bottom column letters */
        DrawText(ls, x - tw / 2, by1 + pad - font_sz - 4, font_sz, GRAY);

        /* Left row numbers (1 at top) */
        char rs[16];
        snprintf(rs, sizeof(rs), "%d", i + 1);
        tw = MeasureText(rs, font_sz);
        DrawText(rs, bx0 - pad + 4, y - font_sz / 2, font_sz, GRAY);

        /* Right row numbers */
        DrawText(rs, bx1 + pad - tw - 4, y - font_sz / 2, font_sz, GRAY);
    }
}

/* Draw a single stone with 3D shading */
static void draw_stone(int sx, int sy, int color)
{
    int r = stone_r;

    /* Shadow */
    DrawCircle(sx + 2, sy + 2, (float)(r + 1), (Color){0, 0, 0, 50});

    if (color == STONE_BLACK) {
        DrawCircle(sx, sy, (float)r, (Color){25, 25, 25, 255});
        DrawCircle(sx, sy, (float)(r - 1), (Color){45, 45, 45, 255});
        /* Highlight */
        DrawCircle(sx - r / 4, sy - r / 4, (float)(r / 3), (Color){90, 90, 90, 180});
    } else {
        DrawCircle(sx, sy, (float)r, (Color){190, 190, 190, 255});
        DrawCircle(sx, sy, (float)(r - 1), (Color){235, 235, 235, 255});
        /* Highlight */
        DrawCircle(sx - r / 4, sy - r / 4, (float)(r / 3), (Color){255, 255, 255, 200});
    }
}

/* Draw all stones on the board */
static void draw_stones(GameState *gs)
{
    for (int y = 0; y < gs->size; y++) {
        for (int x = 0; x < gs->size; x++) {
            if (gs->board[y][x] == STONE_EMPTY) continue;
            int sx, sy;
            board_to_screen(x, y, &sx, &sy);
            draw_stone(sx, sy, gs->board[y][x]);
        }
    }

    /* Mark last move with a small indicator */
    if (gs->current_step > 0 && gs->current_step <= gs->step_count) {
        Step *s = &gs->steps[gs->current_step - 1];
        if (s->played == 1 && s->x >= 0 && s->y >= 0) {
            int sx, sy;
            board_to_screen(s->x, s->y, &sx, &sy);
            Color mark = (s->color == STONE_BLACK) ? RAYWHITE : DARKGRAY;
            int mr = stone_r / 3;
            DrawCircleLines(sx, sy, (float)mr, mark);
        }
    }
}

/* Draw the right-side info panel */
static void draw_info_panel(GameState *gs, int panel_x, int auto_play, float auto_interval)
{
    int x = panel_x + 20;
    int y = board_py + 10;
    int line_h = 28;

    /* Title */
    DrawText("Go Board Viewer", x, y, 24, DARKGREEN);
    y += 40;

    DrawLine(x, y, x + 340, y, LIGHTGRAY);
    y += 16;

    /* File name */
    DrawText("File:", x, y, 16, GRAY);
    DrawText(gs->sgf_filename, x + 60, y, 16, DARKGRAY);
    y += line_h;

    /* Board size */
    char buf[128];
    snprintf(buf, sizeof(buf), "Board: %dx%d", gs->size, gs->size);
    DrawText(buf, x, y, 16, DARKGRAY);
    y += line_h;

    /* Step / Total */
    snprintf(buf, sizeof(buf), "Step: %d / %d", gs->current_step, gs->step_count);
    DrawText(buf, x, y, 18, DARKBLUE);
    y += line_h;

    /* Current color */
    if (gs->current_step > 0 && gs->current_step <= gs->step_count) {
        Step *s = &gs->steps[gs->current_step - 1];
        const char *cn = (s->color == STONE_BLACK) ? "BLACK \xe2\x97\x8f" : "WHITE \xe2\x97\x8b";
        Color cc = (s->color == STONE_BLACK) ? DARKGRAY : DARKBROWN;
        DrawText("Last:", x, y, 16, GRAY);
        DrawText(cn, x + 60, y, 18, cc);
        y += line_h;

        if (s->played == 1) {
            char coord[8];
            if (s->x >= 0 && s->x < 25 && s->y >= 0 && s->y < 25) {
                const char *letters = "ABCDEFGHJKLMNOPQRST";
                snprintf(coord, sizeof(coord), "%c%d",
                         letters[s->x], s->y + 1);
            } else {
                snprintf(coord, sizeof(coord), "pass");
            }
            snprintf(buf, sizeof(buf), "Move: %s", coord);
            DrawText(buf, x, y, 16, DARKGRAY);
            y += line_h;
        }

        if (gs->last_capture_count > 0) {
            snprintf(buf, sizeof(buf), "Captured: %d stones", gs->last_capture_count);
            DrawText(buf, x, y, 16, RED);
            y += line_h;
        }
    } else {
        DrawText("Last: (start)", x, y, 16, GRAY);
        y += line_h * 2;
    }

    y += 20;
    DrawLine(x, y, x + 340, y, LIGHTGRAY);
    y += 20;

    /* Controls */
    DrawText("Controls:", x, y, 18, DARKBROWN);
    y += line_h + 4;
    DrawText("->/Space : Next move",  x, y, 14, DARKGRAY); y += line_h - 6;
    DrawText("<-       : Previous move", x, y, 14, DARKGRAY); y += line_h - 6;
    DrawText("Home/End : First / Last",  x, y, 14, DARKGRAY); y += line_h - 6;
    DrawText("F        : Auto-play",      x, y, 14, DARKGRAY); y += line_h - 6;
    DrawText("Up/Down  : Speed",          x, y, 14, DARKGRAY); y += line_h - 6;
    DrawText("ESC      : Quit",           x, y, 14, DARKGRAY); y += line_h + 10;

    if (auto_play) {
        snprintf(buf, sizeof(buf), "Auto-play: ON (%.1fs)", auto_interval);
        DrawText(buf, x, y, 16, GREEN);
    }
}

/* ================================================================================
 *  MAIN
 * ================================================================================ */

int main(int argc, char *argv[])
{
    const char *sgf_path = (argc > 1) ? argv[1] : "app/game.sgf";

    /* ---- Init game state ---- */
    GameState gs = {0};
    gs.size = DEFAULT_BOARD_SIZE;

    if (!parse_sgf(&gs, sgf_path)) {
        fprintf(stderr, "Failed to load SGF: %s\n", sgf_path);
        fprintf(stderr, "Usage: %s [path/to/game.sgf]\n", argv[0]);
        return 1;
    }

    board_clear(&gs);

    /* ---- Init window ---- */
    int win_w = 1280, win_h = 860;
    char title[512];
    snprintf(title, sizeof(title), "Go Board Viewer - %s", gs.sgf_filename);

    InitWindow(win_w, win_h, title);
    SetTargetFPS(60);

    /* Calculate board layout */
    int board_area = win_h - 80;
    cell_sz = board_area / (gs.size + 1);
    if (cell_sz < 16) cell_sz = 16;
    if (cell_sz > 44) cell_sz = 44;
    stone_r = cell_sz / 2 - 2;

    int grid_px = (gs.size - 1) * cell_sz;
    board_px = 40 + cell_sz / 2 + 4;
    board_py = 40 + cell_sz / 2 + 4;

    int panel_x = board_px + grid_px + cell_sz / 2 + 50;

    /* Auto-play state */
    int   auto_play     = 0;
    float auto_interval = 0.5f;   /* seconds between moves */
    float auto_timer    = 0.0f;

    /* ---- Main loop ---- */
    while (!WindowShouldClose()) {

        /* ---- Input ---- */
        if (IsKeyPressed(KEY_RIGHT) || IsKeyPressed(KEY_SPACE)) {
            if (gs.current_step < gs.step_count) {
                replay_to(&gs, gs.current_step + 1);
                auto_timer = 0.0f;
            }
        }
        if (IsKeyPressed(KEY_LEFT)) {
            if (gs.current_step > 0) {
                replay_to(&gs, gs.current_step - 1);
                auto_timer = 0.0f;
            }
        }
        if (IsKeyPressed(KEY_HOME)) {
            replay_to(&gs, 0);
            auto_timer = 0.0f;
        }
        if (IsKeyPressed(KEY_END)) {
            replay_to(&gs, gs.step_count);
            auto_timer = 0.0f;
        }
        if (IsKeyPressed(KEY_F)) {
            auto_play = !auto_play;
            auto_timer = 0.0f;
        }
        if (IsKeyPressed(KEY_UP)) {
            auto_interval += 0.1f;
            if (auto_interval > 5.0f) auto_interval = 5.0f;
        }
        if (IsKeyPressed(KEY_DOWN)) {
            auto_interval -= 0.1f;
            if (auto_interval < 0.1f) auto_interval = 0.1f;
        }

        /* Auto-play advance */
        if (auto_play) {
            auto_timer += GetFrameTime();
            if (auto_timer >= auto_interval) {
                auto_timer = 0.0f;
                if (gs.current_step < gs.step_count) {
                    replay_to(&gs, gs.current_step + 1);
                } else {
                    auto_play = 0;   /* reached end */
                }
            }
        }

        /* ---- Draw ---- */
        BeginDrawing();
        ClearBackground(RAYWHITE);

        draw_board(&gs);
        draw_stones(&gs);

        /* Hover ghost stone */
        Vector2 mp = GetMousePosition();
        int hx = (int)roundf((mp.x - board_px) / (float)cell_sz);
        int hy = (int)roundf((mp.y - board_py) / (float)cell_sz);
        if (in_bounds(&gs, hx, hy) && mp.x > board_px - cell_sz && mp.x < board_px + grid_px + cell_sz
            && mp.y > board_py - cell_sz && mp.y < board_py + grid_px + cell_sz) {
            int hsx, hsy;
            board_to_screen(hx, hy, &hsx, &hsy);
            Color ghost = (gs.current_step < gs.step_count &&
                           gs.steps[gs.current_step].color == STONE_WHITE)
                          ? (Color){200, 200, 200, 100}
                          : (Color){50, 50, 50, 100};
            DrawCircle(hsx, hsy, (float)stone_r, ghost);
        }

        draw_info_panel(&gs, panel_x, auto_play, auto_interval);

        /* Progress bar at bottom */
        if (gs.step_count > 0) {
            int bar_x = board_px;
            int bar_y = board_py + grid_px + cell_sz / 2 + 30;
            int bar_w = grid_px;
            int bar_h = 8;
            DrawRectangle(bar_x, bar_y, bar_w, bar_h, LIGHTGRAY);
            float frac = (float)gs.current_step / (float)gs.step_count;
            DrawRectangle(bar_x, bar_y, (int)(bar_w * frac), bar_h, DARKGREEN);
            DrawRectangleLines(bar_x, bar_y, bar_w, bar_h, GRAY);
        }

        EndDrawing();
    }

    CloseWindow();
    return 0;
}
