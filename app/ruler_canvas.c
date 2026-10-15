/**********************************************************************************************
*
*   raylib [app] example - Ruler canvas
*
*   A canvas with a horizontal ruler (top) and a vertical ruler (left).
*   Left-drag to draw a shape:
*     - RECTANGLE mode: rectangle from drag start to current mouse.
*     - CIRCLE mode:    circle centered at drag start, radius = distance to mouse.
*   Hotkeys: R = rectangle, C = circle, Z = undo last, X = clear all, F1 = toggle debug.
*
*   This example is self-contained and uses only the public raylib API.
*
**********************************************************************************************/

#include "raylib.h"
#include <math.h>
#include "app_dpi.h"

#define MAX_SHAPES     256      // max shapes on canvas
#define RULE_MAJOR      50      // pixels between long ticks
#define RULE_MINOR      10      // pixels between short ticks
#define RULE_THICK      24      // ruler band thickness (px)

typedef enum {
    SHAPE_RECT = 0,
    SHAPE_CIRCLE
} ShapeType;

typedef struct {
    ShapeType type;
    // For SHAPE_RECT: x,y = corner, w,h = size (can be negative while dragging).
    // For SHAPE_CIRCLE: x,y = center, w = radius (in .w, .h unused).
    float x, y, w, h;
    Color color;
} Shape;

static Shape shapes[MAX_SHAPES] = { 0 };
static int shapeCount = 0;

// Canvas area = window minus the two ruler bands.
static inline Rectangle CanvasArea(void)
{
    return (Rectangle){ RULE_THICK, RULE_THICK,
                        GetScreenWidth() - RULE_THICK,
                        GetScreenHeight() - RULE_THICK };
}

// Map a window-space point to canvas (ruler-relative) coordinates.
static inline void ToCanvas(float inX, float inY, float *outX, float *outY)
{
    *outX = inX - RULE_THICK;
    *outY = inY - RULE_THICK;
}

static void DrawRulers(int screenW, int screenH)
{
    // Top ruler band.
    DrawRectangle(0, 0, screenW, RULE_THICK, (Color){ 245, 245, 245, 255 });
    DrawLine(RULE_THICK, RULE_THICK, screenW, RULE_THICK, (Color){ 200, 200, 200, 255 });
    for (int x = 0; x <= screenW - RULE_THICK; x += RULE_MINOR)
    {
        int sx = x + RULE_THICK;
        int h = (x % RULE_MAJOR == 0) ? RULE_THICK - 6 : RULE_THICK / 2;
        DrawLine(sx, RULE_THICK - h, sx, RULE_THICK, (Color){ 160, 160, 160, 255 });
        if (x % RULE_MAJOR == 0 && x > 0)
        {
            char label[16];
            TextFormat(label, "%d", x);
            DrawText(label, sx + 2, RULE_THICK - h - 2, 9, (Color){ 90, 90, 90, 255 });
        }
    }

    // Left ruler band.
    DrawRectangle(0, 0, RULE_THICK, screenH, (Color){ 245, 245, 245, 255 });
    DrawLine(RULE_THICK, RULE_THICK, RULE_THICK, screenH, (Color){ 200, 200, 200, 255 });
    for (int y = 0; y <= screenH - RULE_THICK; y += RULE_MINOR)
    {
        int sy = y + RULE_THICK;
        int w = (y % RULE_MAJOR == 0) ? RULE_THICK - 6 : RULE_THICK / 2;
        DrawLine(RULE_THICK - w, sy, RULE_THICK, sy, (Color){ 160, 160, 160, 255 });
        if (y % RULE_MAJOR == 0 && y > 0)
        {
            char label[16];
            TextFormat(label, "%d", y);
            DrawText(label, 2, sy + 2, 9, (Color){ 90, 90, 90, 255 });
        }
    }

    // Corner square.
    DrawRectangle(0, 0, RULE_THICK, RULE_THICK, (Color){ 230, 230, 230, 255 });
}

int main(void)
{
    const int screenWidth = 900;
    const int screenHeight = 600;

    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT);
    app_enable_dpi();
    InitWindow(screenWidth, screenHeight, "raylib [app] example - ruler canvas");

    ShapeType mode = SHAPE_RECT;
    bool dragging = false;
    Vector2 dragStart = { 0, 0 };
    Vector2 dragNow = { 0, 0 };
    bool showDebug = false;

    // A small palette to cycle through for drawn shapes.
    const Color palette[] = { DARKBLUE, MAROON, DARKGREEN, DARKPURPLE, ORANGE, BROWN };
    const int paletteCount = sizeof(palette) / sizeof(palette[0]);
    int colorIdx = 0;

    SetTargetFPS(60);

    while (!WindowShouldClose())
    {
        // ---- input ----
        Vector2 mouse = GetMousePosition();
        Rectangle canvas = CanvasArea();

        if (IsKeyPressed(KEY_R)) mode = SHAPE_RECT;
        if (IsKeyPressed(KEY_C)) mode = SHAPE_CIRCLE;
        if (IsKeyPressed(KEY_F1)) showDebug = !showDebug;
        if (IsKeyPressed(KEY_Z) && shapeCount > 0) shapeCount--;
        if (IsKeyPressed(KEY_X)) shapeCount = 0;

        // Only start a drag if the press began inside the canvas.
        bool inCanvas = CheckCollisionPointRec(mouse, canvas);
        if (inCanvas && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
        {
            dragging = true;
            dragStart = dragNow = mouse;
        }
        if (dragging && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) dragNow = mouse;
        if (dragging && IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
        {
            dragging = false;
            if (shapeCount < MAX_SHAPES)
            {
                float sx, sy, nx, ny;
                ToCanvas(dragStart.x, dragStart.y, &sx, &sy);
                ToCanvas(dragNow.x,   dragNow.y,   &nx, &ny);

                Shape s;
                s.type = mode;
                s.color = palette[colorIdx];
                colorIdx = (colorIdx + 1) % paletteCount;

                if (mode == SHAPE_RECT)
                {
                    s.x = (nx < sx) ? nx : sx;
                    s.y = (ny < sy) ? ny : sy;
                    s.w = (nx < sx) ? (sx - nx) : (nx - sx);
                    s.h = (ny < sy) ? (sy - ny) : (ny - sy);
                    // ignore zero-size drags
                    if (s.w < 1.0f || s.h < 1.0f) goto skip_add;
                }
                else // SHAPE_CIRCLE
                {
                    float dx = nx - sx, dy = ny - sy;
                    s.x = sx; s.y = sy;
                    s.w = sqrtf(dx*dx + dy*dy);
                    s.h = 0.0f;
                    if (s.w < 2.0f) goto skip_add;
                }
                shapes[shapeCount++] = s;
            }
        skip_add:;
        }

        // ---- draw ----
        BeginDrawing();
        ClearBackground((Color){ 252, 252, 250, 255 });

        DrawRulers(screenWidth, screenHeight);

        // Canvas background + border.
        DrawRectangleRec(canvas, (Color){ 255, 255, 255, 255 });
        DrawRectangleLinesEx(canvas, 1, (Color){ 200, 200, 200, 255 });

        // Committed shapes.
        for (int i = 0; i < shapeCount; i++)
        {
            Shape s = shapes[i];
            // shapes store canvas-relative coords; offset by ruler.
            float px = s.x + RULE_THICK;
            float py = s.y + RULE_THICK;
            if (s.type == SHAPE_RECT)
                DrawRectangleLinesEx((Rectangle){ px, py, s.w, s.h }, 2, s.color);
            else
                DrawCircleLines((int)px, (int)py, (int)s.w, s.color);
        }

        // Live preview while dragging.
        if (dragging)
        {
            float sx, sy, nx, ny;
            ToCanvas(dragStart.x, dragStart.y, &sx, &sy);
            ToCanvas(dragNow.x,   dragNow.y,   &nx, &ny);
            Color c = palette[colorIdx];
            Color ghost = (Color){ c.r, c.g, c.b, 140 };

            if (mode == SHAPE_RECT)
            {
                float x = (nx < sx) ? nx : sx;
                float y = (ny < sy) ? ny : sy;
                float w = (nx < sx) ? (sx - nx) : (nx - sx);
                float h = (ny < sy) ? (sy - ny) : (ny - sy);
                DrawRectangleRec((Rectangle){ x + RULE_THICK, y + RULE_THICK, w, h }, ghost);
            }
            else
            {
                float dx = nx - sx, dy = ny - sy;
                float r = sqrtf(dx*dx + dy*dy);
                DrawCircle((int)(sx + RULE_THICK), (int)(sy + RULE_THICK), (int)r, ghost);
                // radius helper line + start dot
                DrawLine((int)(sx + RULE_THICK), (int)(sy + RULE_THICK),
                         (int)(nx + RULE_THICK), (int)(ny + RULE_THICK), (Color){ c.r, c.g, c.b, 200 });
                DrawCircle((int)(sx + RULE_THICK), (int)(sy + RULE_THICK), 3, c);
            }
        }

        // Crosshair cursor guides inside the canvas.
        if (CheckCollisionPointRec(mouse, canvas))
        {
            Color g = (Color){ 180, 180, 255, 120 };
            DrawLine((int)mouse.x, RULE_THICK, (int)mouse.x, screenHeight, g);
            DrawLine(0, (int)mouse.y, (int)mouse.x, (int)mouse.y, g);
        }

        // Status bar.
        DrawRectangle(0, screenHeight - 22, screenWidth, 22, (Color){ 240, 240, 240, 255 });
        DrawLine(0, screenHeight - 22, screenWidth, screenHeight - 22, (Color){ 200, 200, 200, 255 });

        float cx, cy;
        ToCanvas(mouse.x, mouse.y, &cx, &cy);
        bool inside = CheckCollisionPointRec(mouse, canvas);
        const char *modeName = (mode == SHAPE_RECT) ? "RECTANGLE" : "CIRCLE";
        char stat[128];
        TextFormat(stat, "mode: %s   shapes: %d/%d   cursor: %s   %s",
                   modeName, shapeCount, MAX_SHAPES,
                   inside ? "" : "(outside canvas)",
                   inside ? TextFormat("%+.0f, %+.0f", cx, cy) : "--");
        DrawText(stat, 8, screenHeight - 17, 12, (Color){ 60, 60, 60, 255 });

        DrawText("R: rect   C: circle   L-drag: draw   Z: undo   X: clear   F1: debug   ESC: quit",
                 screenWidth - 560, screenHeight - 17, 11, (Color){ 110, 110, 110, 255 });

        if (showDebug)
        {
            DrawFPS(screenWidth - 80, 8);
            DrawText(TextFormat("drag: %.0f,%.0f -> %.0f,%.0f",
                                dragging ? dragStart.x : 0, dragging ? dragStart.y : 0,
                                dragging ? dragNow.x : 0,   dragging ? dragNow.y : 0),
                     8, 30, 11, GREEN);
        }

        EndDrawing();
    }

    CloseWindow();
    return 0;
}
