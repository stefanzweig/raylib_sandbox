/**********************************************************************************************
*
*   raylib [app] example - Kaleidoscope
*
*   Click anywhere: a random symmetric pattern spawns at that point and is replicated
*   across N rotational sectors (plus mirroring), producing a kaleidoscope figure.
*   Move the mouse to preview where the next click will paint. Old strokes slowly fade
*   and the whole canvas gently rotates for that turning-kaleidoscope feel.
*
*   Controls:
*     Left click / drag  - stamp random symmetric patterns along the path.
*     Mouse move         - live preview ring at the cursor (where a stamp would land).
*     SPACE              - clear the canvas.
*     N                  - re-roll the number of symmetry sectors (6..16).
*     P                  - pause / resume the slow rotation.
*     S                  - save a screenshot to ./kaleidoscope_shot.png.
*     F1                 - toggle debug overlay.
*     ESC                - quit.
*
**********************************************************************************************/

#include "raylib.h"
#include <stdlib.h>
#include <math.h>
#include "app_dpi.h"

#define MAX_STROKES     4096
#define SECTORS_MIN      6
#define SECTORS_MAX      16
#define STROKE_PTS        5      // points per stamp polyline

typedef struct {
    float angle;        // polar angle of stamp centre (radians, relative to canvas centre)
    float radius;       // polar radius of stamp centre
    float pts[STROKE_PTS * 2];   // each point stored as (dAngle, dRadius) offsets
    int   ptCount;
    float width;
    Color color;
    float life;         // remaining life in seconds (0 = gone)
    float maxLife;
} Stroke;

static Stroke strokes[MAX_STROKES] = { 0 };
static int strokeCount = 0;

static int sectors = 12;             // rotational symmetry count
static float globalRotation = 0.0f;  // slow canvas rotation
static bool paused = false;
static bool showDebug = false;

// Palette for random stroke colors.
static const Color palette[] = {
    {255,  90, 120, 255}, {255, 180,  60, 255}, {255, 240, 120, 255},
    {120, 230, 140, 255}, { 80, 200, 255, 255}, {180, 120, 255, 255},
    {255, 120, 220, 255}, {120, 255, 220, 255}
};
static const int paletteCount = sizeof(palette) / sizeof(palette[0]);

static void AddStroke(float cx, float cy, float mx, float my)
{
    if (strokeCount >= MAX_STROKES) {
        // drop oldest
        for (int i = 1; i < MAX_STROKES; i++) strokes[i - 1] = strokes[i];
        strokeCount = MAX_STROKES - 1;
    }
    Stroke s = { 0 };
    float dx = mx - cx, dy = my - cy;
    s.angle  = atan2f(dy, dx);
    s.radius = sqrtf(dx * dx + dy * dy);

    s.ptCount = STROKE_PTS;
    // Build a small random polyline around the stamp centre, in polar offsets.
    for (int i = 0; i < STROKE_PTS; i++) {
        float da = (GetRandomValue(-100, 100) / 100.0f) * 0.18f;   // angle offset
        float dr = (GetRandomValue(-100, 100) / 100.0f) * 22.0f;  // radius offset
        s.pts[i * 2]     = da;
        s.pts[i * 2 + 1] = dr;
    }
    s.width  = (float)GetRandomValue(2, 6);
    s.color  = palette[GetRandomValue(0, paletteCount - 1)];
    s.maxLife = (float)GetRandomValue(60, 140) / 10.0f;   // 6..14 s
    s.life    = s.maxLife;

    strokes[strokeCount++] = s;
}

// Draw one stamp (a polyline) given canvas centre and the stroke's polar anchor.
static void DrawStamp(Stroke s, float cx, float cy, float rotOffset)
{
    // Build world-space points from polar offsets, then draw as a line strip.
    Vector2 pts[STROKE_PTS];
    for (int i = 0; i < s.ptCount; i++) {
        float pa = s.angle + s.pts[i * 2]     + rotOffset;
        float pr = s.radius + s.pts[i * 2 + 1];
        pts[i].x = cx + cosf(pa) * pr;
        pts[i].y = cy + sinf(pa) * pr;
    }
    unsigned char alpha = (unsigned char)(255.0f * (s.life / s.maxLife));
    Color c = { s.color.r, s.color.g, s.color.b, alpha };
    for (int i = 1; i < s.ptCount; i++)
        DrawLineEx(pts[i - 1], pts[i], s.width, c);
    // dots at vertices for sparkle
    for (int i = 0; i < s.ptCount; i++)
        DrawCircleV(pts[i], s.width * 0.5f, c);
}

int main(void)
{
    app_enable_dpi();
    const int screenWidth = 800;
    const int screenHeight = 800;
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT);
    InitWindow(screenWidth, screenHeight, "raylib [app] example - Kaleidoscope");

    SetTraceLogLevel(LOG_WARNING);
    SetRandomSeed((unsigned int)GetTime());
    SetTargetFPS(60);

    float cx = screenWidth / 2.0f;
    float cy = screenHeight / 2.0f;

    bool painting = false;
    float paintCooldown = 0.0f;

    // Seed an initial flourish so the canvas isn't empty.
    for (int i = 0; i < 6; i++)
        AddStroke(cx, cy,
                  cx + GetRandomValue(-300, 300),
                  cy + GetRandomValue(-300, 300));

    while (!WindowShouldClose())
    {
        float dt = GetFrameTime();
        Vector2 m = GetMousePosition();

        if (IsKeyPressed(KEY_SPACE)) strokeCount = 0;
        if (IsKeyPressed(KEY_N))    sectors = GetRandomValue(SECTORS_MIN, SECTORS_MAX);
        if (IsKeyPressed(KEY_P))    paused = !paused;   // pauses stroke aging only
        if (IsKeyPressed(KEY_F1))   showDebug = !showDebug;
        if (IsKeyPressed(KEY_S))    TakeScreenshot("kaleidoscope_shot.png");

        // Painting: click starts, drag continues to stamp along the path.
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))  { painting = true;  paintCooldown = 0.0f; }
        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) painting = false;

        // Optional manual rotation (hold RIGHT arrow) instead of auto-spin,
        // so clicks always match where patterns appear.
        if (IsKeyDown(KEY_RIGHT)) globalRotation += dt * 0.6f;
        if (IsKeyDown(KEY_LEFT))  globalRotation -= dt * 0.6f;

        if (painting) {
            paintCooldown -= dt;
            if (paintCooldown <= 0.0f) {
                AddStroke(cx, cy, m.x, m.y);
                paintCooldown = 0.05f;   // 20 stamps/sec max while dragging
            }
        }

        // Age strokes.
        for (int i = 0; i < strokeCount; i++) {
            if (!paused) strokes[i].life -= dt * 0.6f;
            if (strokes[i].life < 0) strokes[i].life = 0;
        }
        // Compact: drop dead strokes.
        int w = 0;
        for (int i = 0; i < strokeCount; i++) {
            if (strokes[i].life > 0) strokes[w++] = strokes[i];
        }
        strokeCount = w;

        // ---- draw ----
        BeginDrawing();
        ClearBackground((Color){ 12, 10, 22, 255 });

        // Soft radial background.
        DrawCircleGradient((int)cx, (int)cy, 380,
                           (Color){ 40, 30, 70, 255 },
                           (Color){ 12, 10, 22, 255 });

        // For each rotational sector, draw every stroke rotated + mirrored.
        float sectorStep = 2.0f * PI / sectors;
        for (int s = 0; s < sectors; s++) {
            float rot = sectorStep * s + globalRotation;
            for (int i = 0; i < strokeCount; i++)
                DrawStamp(strokes[i], cx, cy, rot);
            // mirrored sector (flip angle) for true kaleidoscope reflection
            for (int i = 0; i < strokeCount; i++)
                DrawStamp(strokes[i], cx, cy, -rot);
        }

        // Live preview ring at the cursor.
        bool inWin = (m.x >= 0 && m.y >= 0 && m.x < screenWidth && m.y < screenHeight);
        if (inWin) {
            DrawCircleLines((int)m.x, (int)m.y, 8, (Color){ 255, 255, 255, 180 });
            DrawCircleV(m, 2, (Color){ 255, 255, 255, 220 });
        }

        // HUD
        DrawText("Click / drag to paint symmetric patterns",
                 10, 10, 14, RAYWHITE);
        DrawText(TextFormat("sectors: %d   strokes: %d   %s",
                            sectors, strokeCount, paused ? "PAUSED" : "running"),
                 10, screenHeight - 26, 12, GRAY);
        DrawText("SPACE clear   N re-roll sectors   P pause-fade   LEFT/RIGHT rotate   S screenshot   F1 debug   ESC quit",
                 10, screenHeight - 12, 9, (Color){ 120, 120, 130, 255 });

        if (showDebug) {
            DrawFPS(screenWidth - 80, 10);
            DrawText(TextFormat("rot=%.2f  mouse=%.0f,%.0f", globalRotation, m.x, m.y),
                     10, 30, 11, GREEN);
        }

        EndDrawing();
    }

    CloseWindow();
    return 0;
}
