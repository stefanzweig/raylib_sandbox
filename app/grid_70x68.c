/**********************************************************************************************
*
*   raylib [app] example - 70x68 alternating grid
*
*   A grid of 70 columns x 68 rows of cells, rendered in a checkerboard (alternating)
*   pattern using a light pastel palette. The grid is larger than the window, so the
*   view pans by dragging with the mouse and zooms with the wheel.
*
*   Controls:
*     Left-drag            - pan the view
*     Mouse wheel          - zoom in / out (cell size)
*     R                    - reset pan & zoom
*     H                    - toggle header labels (column/row numbers)
*     F1                   - toggle debug overlay
*     ESC                  - quit
*
**********************************************************************************************/

#include "raylib.h"
#include <stdio.h>
#include "app_dpi.h"

#define COLS 70
#define ROWS 68

static int cellSize = 18;          // current cell size in pixels (zoomable)
static float panX = 0.0f, panY = 0.0f;   // view offset in world pixels
static bool showHeaders = false;

// Light pastel checkerboard colours.
static const Color colorA = { 243, 246, 250, 255 };   // very light blue-white
static const Color colorB = { 226, 232, 240, 255 };   // light slate

int main(void)
{
    app_enable_dpi();
    const int screenWidth = 900;
    const int screenHeight = 700;
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
    InitWindow(screenWidth, screenHeight, "raylib [app] example - 70x68 grid");

    SetTraceLogLevel(LOG_WARNING);
    SetTargetFPS(60);

    bool showDebug = false;
    bool dragging = false;
    Vector2 dragStart = { 0, 0 };
    float panStartX = 0, panStartY = 0;

    // centre the grid roughly on screen initially
    panX = -((COLS * cellSize) - screenWidth) / 2.0f;
    panY = -((ROWS * cellSize) - screenHeight) / 2.0f;

    while (!WindowShouldClose())
    {
        int sw = GetScreenWidth();
        int sh = GetScreenHeight();

        if (IsKeyPressed(KEY_R)) {
            cellSize = 18;
            panX = -((COLS * cellSize) - sw) / 2.0f;
            panY = -((ROWS * cellSize) - sh) / 2.0f;
        }
        if (IsKeyPressed(KEY_H)) showHeaders = !showHeaders;
        if (IsKeyPressed(KEY_F1)) showDebug = !showDebug;

        // zoom with wheel; keep the cell under the cursor stable.
        float wheel = GetMouseWheelMove();
        if (wheel != 0) {
            Vector2 m = GetMousePosition();
            int oldSize = cellSize;
            cellSize += (int)(wheel * 2);
            if (cellSize < 4) cellSize = 4;
            if (cellSize > 60) cellSize = 60;
            float ratio = (float)cellSize / (float)oldSize;
            panX = m.x - (m.x - panX) * ratio;
            panY = m.y - (m.y - panY) * ratio;
        }

        // pan by dragging
        Vector2 m = GetMousePosition();
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            dragging = true;
            dragStart = m;
            panStartX = panX; panStartY = panY;
        }
        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) dragging = false;
        if (dragging) {
            panX = panStartX + (m.x - dragStart.x);
            panY = panStartY + (m.y - dragStart.y);
        }

        // which cells are visible
        int firstCol = (int)(-panX / cellSize) - 1; if (firstCol < 0) firstCol = 0;
        int firstRow = (int)(-panY / cellSize) - 1; if (firstRow < 0) firstRow = 0;
        int lastCol = firstCol + sw / cellSize + 2; if (lastCol >= COLS) lastCol = COLS - 1;
        int lastRow = firstRow + sh / cellSize + 2; if (lastRow >= ROWS) lastRow = ROWS - 1;

        // ---- draw ----
        BeginDrawing();
        ClearBackground((Color){ 210, 216, 224, 255 });   // light frame around the grid

        // grid cells
        for (int r = firstRow; r <= lastRow; r++) {
            for (int c = firstCol; c <= lastCol; c++) {
                int x = (int)panX + c * cellSize;
                int y = (int)panY + r * cellSize;
                Color col = ((c + r) & 1) ? colorB : colorA;
                DrawRectangle(x, y, cellSize, cellSize, col);
            }
        }

        // subtle grid lines on top
        Color line = { 200, 208, 218, 255 };
        for (int c = firstCol; c <= lastCol + 1; c++) {
            int x = (int)panX + c * cellSize;
            DrawLine(x, (int)panY + firstRow * cellSize, x, (int)panY + (lastRow + 1) * cellSize, line);
        }
        for (int r = firstRow; r <= lastRow + 1; r++) {
            int y = (int)panY + r * cellSize;
            DrawLine((int)panX + firstCol * cellSize, y, (int)panX + (lastCol + 1) * cellSize, y, line);
        }

        // border of the whole grid
        DrawRectangleLines((int)panX - 1, (int)panY - 1,
                           COLS * cellSize + 2, ROWS * cellSize + 2,
                           (Color){ 150, 160, 174, 255 });

        // headers (column/row numbers) when enabled
        if (showHeaders) {
            for (int c = firstCol; c <= lastCol; c++) {
                int x = (int)panX + c * cellSize;
                char buf[8]; snprintf(buf, sizeof(buf), "%d", c);
                DrawText(buf, x + 2, (int)panY - 14, 9, (Color){ 90, 96, 108, 255 });
            }
            for (int r = firstRow; r <= lastRow; r++) {
                int y = (int)panY + r * cellSize;
                char buf[8]; snprintf(buf, sizeof(buf), "%d", r);
                DrawText(buf, (int)panX - 22, y + 2, 9, (Color){ 90, 96, 108, 255 });
            }
        }

        // HUD
        DrawText(TextFormat("70 x 68 grid   cell %dpx   view %d,%d",
                            cellSize, (int)panX, (int)panY),
                 10, 10, 13, (Color){ 60, 66, 78, 255 });
        DrawText("L-drag pan   wheel zoom   R reset   H headers   F1 debug   ESC quit",
                 10, sh - 18, 11, (Color){ 90, 96, 108, 255 });

        if (showDebug) {
            DrawFPS(sw - 80, 10);
            DrawText(TextFormat("visible %d..%d x %d..%d   mouse %.0f,%.0f",
                                firstCol, lastCol, firstRow, lastRow, m.x, m.y),
                     10, 28, 11, GREEN);
        }

        EndDrawing();
    }

    CloseWindow();
    return 0;
}
