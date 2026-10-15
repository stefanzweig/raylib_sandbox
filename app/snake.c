/**********************************************************************************************
*
*   raylib [app] example - Classic Snake
*
*   A classic snake game on a grid. The snake grows by one segment every time it eats
*   food; hitting a wall or its own body ends the run. Arrow keys / WASD steer.
*
*   Controls:
*     Arrow keys / WASD - steer the snake (a 180° turn is ignored, so you can't reverse
*                         into yourself in a single frame).
*     SPACE             - pause / resume.
*     R                 - restart after game over.
*     F1                - toggle debug overlay (FPS, head cell coords).
*     ESC               - quit.
*
**********************************************************************************************/

#include "raylib.h"
#include <stdlib.h>
#include "app_dpi.h"

#define COLS            25          // grid columns
#define ROWS            25          // grid rows
#define CELL            24          // pixels per cell
#define BOARD_W         (COLS * CELL)
#define BOARD_H         (ROWS * CELL)
#define HUD_H           40          // top score bar
#define WIN_W           BOARD_W
#define WIN_H           (BOARD_H + HUD_H)
#define MAX_SNAKE       (COLS * ROWS)
#define MOVE_MS         110.0f      // ms per grid step

typedef struct { int x, y; } Cell;

static Cell snake[MAX_SNAKE];
static int  snakeLen = 0;
static Cell dir = { 1, 0 };         // current travel direction
static Cell nextDir = { 1, 0 };     // queued direction, applied on next step
static Cell food = { 0, 0 };
static int  score = 0;
static bool gameOver = false;
static bool paused = false;
static bool debug = false;
static float moveTimer = 0.0f;
static int originX = 0, originY = HUD_H;   // board top-left in window space

//------------------------------------------------------------------------------------
// Place food on a random free cell (never on the snake).
//------------------------------------------------------------------------------------
static void PlaceFood(void)
{
    for (;;)
    {
        int fx = rand() % COLS;
        int fy = rand() % ROWS;
        bool onSnake = false;
        for (int i = 0; i < snakeLen; i++)
            if (snake[i].x == fx && snake[i].y == fy) { onSnake = true; break; }
        if (!onSnake) { food.x = fx; food.y = fy; return; }
    }
}

//------------------------------------------------------------------------------------
// Reset the whole run to the initial 3-cell snake in the middle.
//------------------------------------------------------------------------------------
static void ResetGame(void)
{
    snakeLen = 3;
    snake[0] = (Cell){ COLS/2,     ROWS/2 };
    snake[1] = (Cell){ COLS/2 - 1, ROWS/2 };
    snake[2] = (Cell){ COLS/2 - 2, ROWS/2 };
    dir = (Cell){ 1, 0 };
    nextDir = (Cell){ 1, 0 };
    score = 0;
    gameOver = false;
    paused = false;
    moveTimer = 0.0f;
    PlaceFood();
}

//------------------------------------------------------------------------------------
// Advance the simulation by one grid step.
//------------------------------------------------------------------------------------
static void Step(void)
{
    // Commit queued direction, but reject an immediate 180° reversal relative to the
    // direction we are *currently* travelling (not the one we queued last frame).
    if (!(nextDir.x == -dir.x && nextDir.y == -dir.y)) dir = nextDir;

    // Body follows: each segment takes the position of the one ahead of it.
    for (int i = snakeLen - 1; i > 0; i--) snake[i] = snake[i - 1];
    snake[0].x += dir.x;
    snake[0].y += dir.y;

    // Wall collision.
    if (snake[0].x < 0 || snake[0].x >= COLS ||
        snake[0].y < 0 || snake[0].y >= ROWS)
    { gameOver = true; return; }

    // Self collision (skip the tail, which will move away this step).
    for (int i = 1; i < snakeLen; i++)
        if (snake[0].x == snake[i].x && snake[0].y == snake[i].y)
        { gameOver = true; return; }

    // Eat food: grow by duplicating the tail segment.
    if (snake[0].x == food.x && snake[0].y == food.y)
    {
        snake[snakeLen] = snake[snakeLen - 1];
        snakeLen++;
        score += 10;
        PlaceFood();
    }
}

//------------------------------------------------------------------------------------
// Draw one cell as a rounded rectangle at its grid position.
//------------------------------------------------------------------------------------
static void DrawCell(Cell c, Color color)
{
    int px = originX + c.x * CELL;
    int py = originY + c.y * CELL;
    DrawRectangleRounded((Rectangle){ px + 1, py + 1, CELL - 2, CELL - 2 }, 0.25f, 6, color);
}

//------------------------------------------------------------------------------------
// Program main entry point
//------------------------------------------------------------------------------------
int main(void)
{
    app_enable_dpi();

    // Initialization
    //----------------------------------------------------------------------------------
    InitWindow(WIN_W, WIN_H, "raylib [app] example - classic snake");
    SetTargetFPS(60);

    // Center the window on the primary monitor.
    int mon = GetCurrentMonitor();
    int wx = (GetMonitorWidth(mon)  - WIN_W) / 2;
    int wy = (GetMonitorHeight(mon) - WIN_H) / 2;
    SetWindowPosition(wx, wy);

    srand(1987);    // fixed seed: reproducible-ish but not boring
    ResetGame();
    //----------------------------------------------------------------------------------

    // Main game loop
    while (!WindowShouldClose())
    {
        // Update
        //------------------------------------------------------------------------------
        if (!gameOver && !paused)
        {
            // Steering (queued, so fast multi-key presses within one step still can't
            // produce an illegal reversal).
            if (IsKeyDown(KEY_RIGHT) || IsKeyDown(KEY_D)) nextDir = (Cell){  1,  0 };
            if (IsKeyDown(KEY_LEFT)  || IsKeyDown(KEY_A)) nextDir = (Cell){ -1,  0 };
            if (IsKeyDown(KEY_DOWN)  || IsKeyDown(KEY_S)) nextDir = (Cell){  0,  1 };
            if (IsKeyDown(KEY_UP)    || IsKeyDown(KEY_W)) nextDir = (Cell){  0, -1 };

            moveTimer += GetFrameTime() * 1000.0f;
            while (moveTimer >= MOVE_MS)
            {
                moveTimer -= MOVE_MS;
                Step();
                if (gameOver) break;
            }
        }

        if (IsKeyPressed(KEY_SPACE) && !gameOver) paused = !paused;
        if (IsKeyPressed(KEY_R)) ResetGame();
        if (IsKeyPressed(KEY_F1)) debug = !debug;
        //------------------------------------------------------------------------------

        // Draw
        //------------------------------------------------------------------------------
        BeginDrawing();
            ClearBackground((Color){ 24, 24, 32, 255 });

            // HUD bar
            DrawRectangle(0, 0, WIN_W, HUD_H, (Color){ 16, 16, 22, 255 });
            DrawText("SNAKE", 12, 10, 20, RAYWHITE);
            DrawText(TextFormat("SCORE  %d", score), WIN_W - 140, 12, 20, LIME);

            // Board background + faint grid
            DrawRectangle(originX, originY, BOARD_W, BOARD_H, (Color){ 12, 14, 20, 255 });
            for (int x = 0; x <= COLS; x++)
                DrawLine(originX + x * CELL, originY,
                         originX + x * CELL, originY + BOARD_H,
                         (Color){ 30, 34, 44, 255 });
            for (int y = 0; y <= ROWS; y++)
                DrawLine(originX, originY + y * CELL,
                         originX + BOARD_W, originY + y * CELL,
                         (Color){ 30, 34, 44, 255 });

            // Food: red circle centered in its cell.
            DrawCircle(originX + food.x * CELL + CELL/2,
                        originY + food.y * CELL + CELL/2,
                        CELL * 0.35f, RED);

            // Snake: body first (tail to neck), head last so the head sits on top.
            for (int i = snakeLen - 1; i >= 1; i--)
            {
                // Fade from bright green near the head to dark near the tail.
                float t = (float)i / (float)snakeLen;
                Color bodyCol = ColorAlpha(LIME, 1.0f - t * 0.55f);
                DrawCell(snake[i], bodyCol);
            }
            DrawCell(snake[0], RAYWHITE);   // head stands out

            // Overlays
            if (paused && !gameOver)
            {
                DrawRectangle(0, originY, BOARD_W, BOARD_H, (Color){ 0, 0, 0, 140 });
                DrawText("PAUSED", WIN_W/2 - MeasureText("PAUSED", 30)/2,
                         originY + BOARD_H/2 - 20, 30, RAYWHITE);
                DrawText("press SPACE to resume",
                         WIN_W/2 - MeasureText("press SPACE to resume", 16)/2,
                         originY + BOARD_H/2 + 20, 16, LIGHTGRAY);
            }
            if (gameOver)
            {
                DrawRectangle(0, originY, BOARD_W, BOARD_H, (Color){ 0, 0, 0, 160 });
                DrawText("GAME OVER", WIN_W/2 - MeasureText("GAME OVER", 36)/2,
                         originY + BOARD_H/2 - 40, 36, RED);
                DrawText(TextFormat("Final score: %d", score),
                         WIN_W/2 - MeasureText(TextFormat("Final score: %d", score), 18)/2,
                         originY + BOARD_H/2 + 10, 18, RAYWHITE);
                DrawText("press R to play again",
                         WIN_W/2 - MeasureText("press R to play again", 16)/2,
                         originY + BOARD_H/2 + 40, 16, LIGHTGRAY);
            }

            if (debug)
            {
                DrawRectangle(8, WIN_H - 28, 260, 22, (Color){ 0, 0, 0, 160 });
                DrawText(TextFormat("FPS %d  head (%d,%d)  len %d",
                         GetFPS(), snake[0].x, snake[0].y, snakeLen),
                         14, WIN_H - 25, 14, RAYWHITE);
            }
        EndDrawing();
        //------------------------------------------------------------------------------
    }

    // De-Initialization
    //----------------------------------------------------------------------------------
    CloseWindow();
    //----------------------------------------------------------------------------------

    return 0;
}
