/**********************************************************************************************
*
*   raylib [app] example - Conway's Game of Life
*
*   A grid world where cells live/die by Conway's rules:
*     - A live cell with 2 or 3 live neighbours survives.
*     - A dead cell with exactly 3 live neighbours becomes live.
*     - All others die or stay dead.
*
*   Controls:
*     Left mouse drag       - paint living cells (or erase if started on a live cell).
*     Right mouse drag      - erase cells.
*     SPACE                 - run / pause.
*     Step (.)              - single-step while paused.
*     C                     - clear the world.
*     R                     - fill with random cells.
*     1                     - load Glider preset.
*     2                     - load Pulsar (oscillator) preset.
*     3                     - load Gosper Glider Gun preset.
*     Up / Down             - speed up / slow down (steps per second).
*     F1                    - toggle debug overlay.
*     ESC                   - quit.
*
**********************************************************************************************/

#include "raylib.h"
#include <stdlib.h>
#include "app_dpi.h"

#define COLS            80
#define ROWS            60
#define CELL            12          // pixels per cell
#define WORLD_W         (COLS * CELL)
#define WORLD_H         (ROWS * CELL)
#define MAX_SPEED       60          // steps per second cap
#define MIN_SPEED       1

// Two buffers for double-buffered evolution (no in-place update artifacts).
static unsigned char worldA[ROWS][COLS] = { 0 };
static unsigned char worldB[ROWS][COLS] = { 0 };
static unsigned char *cur = (unsigned char *)worldA;
static unsigned char *nxt = (unsigned char *)worldB;

static int originX = 0, originY = 0;
static int screenWidth = 0, screenHeight = 0;

static inline unsigned char CellAt(unsigned char *g, int x, int y)
{
    if (x < 0) x += COLS;
    if (x >= COLS) x -= COLS;
    if (y < 0) y += ROWS;
    if (y >= ROWS) y -= ROWS;
    return g[y * COLS + x];
}

// Count the 8 wrapped neighbours of (x,y) in grid g.
static int CountNeighbours(unsigned char *g, int x, int y)
{
    int n = 0;
    for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++)
        {
            if (dx == 0 && dy == 0) continue;
            n += CellAt(g, x + dx, y + dy);
        }
    return n;
}

static void Evolve(void)
{
    for (int y = 0; y < ROWS; y++)
        for (int x = 0; x < COLS; x++)
        {
            int n = CountNeighbours(cur, x, y);
            unsigned char alive = CellAt(cur, x, y);
            unsigned char next = 0;
            if (alive && (n == 2 || n == 3)) next = 1;
            else if (!alive && n == 3)       next = 1;
            nxt[y * COLS + x] = next;
        }
    // swap buffers
    unsigned char *tmp = cur; cur = nxt; nxt = tmp;
}

static void ClearWorld(void)
{
    for (int i = 0; i < ROWS * COLS; i++) cur[i] = 0;
}

static void Randomize(void)
{
    for (int i = 0; i < ROWS * COLS; i++)
        cur[i] = (GetRandomValue(0, 7) == 0) ? 1 : 0;  // ~12.5% density
}

// Place a pattern given as a list of relative coordinates, centred on the world.
static void PlacePattern(const int *coords, int count, int cx, int cy)
{
    for (int i = 0; i < count; i++)
    {
        int x = cx + coords[2 * i];
        int y = cy + coords[2 * i + 1];
        if (x >= 0 && x < COLS && y >= 0 && y < ROWS) cur[y * COLS + x] = 1;
    }
}

static void LoadGlider(void)
{
    ClearWorld();
    static const int g[] = { 0,0, 1,0, 2,0, 2,-1, 0,-1 };
    PlacePattern(g, 5, 5, 5);
}

static void LoadPulsar(void)
{
    ClearWorld();
    // Pulsar (period-3 oscillator), 13x13, centred.
    static const int p[] = {
         2,0,  3,0,  4,0,   8,0,  9,0, 10,0,
         0,2,  5,2,  7,2, 12,2,
         0,3,  5,3,  7,3, 12,3,
         0,4,  5,4,  7,4, 12,4,
         2,5,  3,5,  4,5,   8,5,  9,5, 10,5,
         2,7,  3,7,  4,7,   8,7,  9,7, 10,7,
         0,8,  5,8,  7,8, 12,8,
         0,9,  5,9,  7,9, 12,9,
         0,10, 5,10, 7,10,12,10,
         2,12, 3,12, 4,12,  8,12, 9,12,10,12
    };
    PlacePattern(p, sizeof(p) / sizeof(int) / 2, COLS / 2 - 6, ROWS / 2 - 6);
}

static void LoadGun(void)
{
    ClearWorld();
    // Gosper glider gun, top-left anchored at (1,1).
    static const int g[] = {
        1,5,  1,6,  2,5,  2,6,
        11,5, 11,6, 11,7, 12,4, 12,8, 13,3, 13,9, 14,3, 14,9,
        15,6, 16,4, 16,8, 17,5, 17,6, 17,7, 18,6,
        21,3, 21,4, 21,5, 22,3, 22,4, 22,5, 23,2, 23,6, 25,1, 25,2, 25,6, 25,7,
        35,3, 35,4, 36,3, 36,4
    };
    PlacePattern(g, sizeof(g) / sizeof(int) / 2, 2, 2);
}

static int Population(void)
{
    int p = 0;
    for (int i = 0; i < ROWS * COLS; i++) p += cur[i];
    return p;
}

int main(void)
{
    screenWidth = COLS * CELL + 16;
    screenHeight = ROWS * CELL + 40;
    app_enable_dpi();
    InitWindow(screenWidth, screenHeight, "raylib [app] example - Game of Life");

    originX = (screenWidth - WORLD_W) / 2;
    originY = 4;

    SetTraceLogLevel(LOG_WARNING);
    SetRandomSeed((unsigned int)GetTime());
    SetTargetFPS(60);

    LoadGlider();                 // start with something fun
    bool running = false;
    int  speed = 10;             // steps per second
    double stepAccum = 0.0;
    long  generation = 0;
    bool showDebug = false;

    bool painting = false;
    bool paintValue = 1;         // value to paint when dragging
    bool erasing = false;

    while (!WindowShouldClose())
    {
        double dt = GetFrameTime();

        // ---- input ----
        if (IsKeyPressed(KEY_SPACE)) running = !running;
        if (IsKeyPressed(KEY_PERIOD) && !running) { Evolve(); generation++; }
        if (IsKeyPressed(KEY_C)) { ClearWorld(); generation = 0; }
        if (IsKeyPressed(KEY_R)) { Randomize(); generation = 0; }
        if (IsKeyPressed(KEY_ONE)) { LoadGlider(); generation = 0; }
        if (IsKeyPressed(KEY_TWO)) { LoadPulsar(); generation = 0; }
        if (IsKeyPressed(KEY_THREE)) { LoadGun(); generation = 0; }
        if (IsKeyPressed(KEY_F1)) showDebug = !showDebug;
        if (IsKeyPressed(KEY_UP))   speed = speed < MAX_SPEED ? speed + 1 : MAX_SPEED;
        if (IsKeyPressed(KEY_DOWN)) speed = speed > MIN_SPEED ? speed - 1 : MIN_SPEED;

        Vector2 m = GetMousePosition();
        int gx = (int)((m.x - originX) / CELL);
        int gy = (int)((m.y - originY) / CELL);
        bool inWorld = (m.x >= originX && m.y >= originY &&
                        m.x < originX + WORLD_W && m.y < originY + WORLD_H);

        // Left-drag: paint (or erase if begun on a live cell).
        if (inWorld && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
        {
            painting = true;
            paintValue = CellAt(cur, gx, gy) ? 0 : 1;   // toggle based on start cell
        }
        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) painting = false;

        // Right-drag: erase.
        if (inWorld && IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) erasing = true;
        if (IsMouseButtonReleased(MOUSE_BUTTON_RIGHT)) erasing = false;

        if ((painting || erasing) && inWorld)
        {
            unsigned char v = erasing ? 0 : paintValue;
            if (gx >= 0 && gx < COLS && gy >= 0 && gy < ROWS)
                cur[gy * COLS + gx] = v;
        }

        // ---- step ----
        if (running)
        {
            stepAccum += dt;
            double interval = 1.0 / (double)speed;
            while (stepAccum >= interval)
            {
                Evolve();
                generation++;
                stepAccum -= interval;
            }
        }

        // ---- draw ----
        BeginDrawing();
        ClearBackground((Color){ 28, 30, 36, 255 });

        // world background
        DrawRectangle(originX, originY, WORLD_W, WORLD_H, (Color){ 16, 18, 22, 255 });

        // cells
        Color liveColor = (Color){ 120, 220, 140, 255 };
        for (int y = 0; y < ROWS; y++)
            for (int x = 0; x < COLS; x++)
                if (cur[y * COLS + x])
                    DrawRectangle(originX + x * CELL + 1, originY + y * CELL + 1,
                                  CELL - 1, CELL - 1, liveColor);

        // subtle grid lines
        Color grid = (Color){ 40, 44, 52, 255 };
        for (int x = 0; x <= COLS; x++)
            DrawLine(originX + x * CELL, originY,
                     originX + x * CELL, originY + WORLD_H, grid);
        for (int y = 0; y <= ROWS; y++)
            DrawLine(originX, originY + y * CELL,
                     originX + WORLD_W, originY + y * CELL, grid);

        // cursor highlight
        if (inWorld && gx >= 0 && gx < COLS && gy >= 0 && gy < ROWS)
        {
            Color hc = erasing ? (Color){ 230, 100, 100, 200 }
                               : (Color){ 100, 180, 255, 200 };
            DrawRectangleLines(originX + gx * CELL, originY + gy * CELL, CELL, CELL, hc);
        }

        // border
        DrawRectangleLines(originX - 1, originY - 1, WORLD_W + 2, WORLD_H + 2,
                           (Color){ 90, 100, 110, 255 });

        // status bar
        DrawRectangle(0, screenHeight - 24, screenWidth, 24, (Color){ 40, 44, 52, 255 });
        DrawLine(0, screenHeight - 24, screenWidth, screenHeight - 24,
                 (Color){ 70, 76, 84, 255 });

        const char *state = running ? "RUNNING" : "PAUSED";
        DrawText(TextFormat("%s   gen: %ld   pop: %d   speed: %d/s",
                            state, generation, Population(), speed),
                 8, screenHeight - 18, 12, RAYWHITE);
        DrawText("SPACE run/step  . step  L-paint  R-erase  C clear  R rand  "
                 "1 glider 2 pulsar 3 gun  ^/v speed  ESC quit",
                 8, screenHeight - 4, 9, (Color){ 150, 156, 164, 255 });

        if (showDebug)
        {
            DrawFPS(screenWidth - 80, 8);
            DrawText(TextFormat("grid %dx%d  cell %d  mouse %d,%d",
                                COLS, ROWS, CELL, gx, gy),
                     8, 8, 10, GREEN);
        }

        EndDrawing();
    }

    CloseWindow();
    return 0;
}
