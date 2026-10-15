/**********************************************************************************************
*
*   raylib [app] example - Prism Kaleidoscope (3-mirror, 6-fold reflection)
*
*   A real kaleidoscope uses 3 mirrors arranged in a triangle (60° apart), producing
*   6-fold dihedral reflection. Click anywhere: a random pattern spawns CENTRED ON
*   that click point, then is reflected through the 3 mirror lines (and combinations
*   thereof) to tile the whole canvas with the mirrored image.
*
*   The 3 mirror lines pass through the canvas centre at angles 0°, 60°, 120°.
*   Reflecting a point across these lines generates the 6 dihedral copies (D3 group):
*     identity, R1, R2, R3, R1·R2, R2·R3  (where Ri = reflection across line i).
*
*   Controls:
*     Left click / drag  - stamp a random pattern centred on the cursor; it mirrors out.
*     Mouse move         - preview cross at the cursor (where the next stamp lands).
*     SPACE              - clear the canvas.
*     R                  - re-roll the mirror trio angle (rotates the whole field).
*     P                  - pause / resume stroke fade-out.
*     S                  - save a screenshot to ./prism_kaleido_shot.png.
*     F1                 - toggle debug overlay.
*     ESC                - quit.
*
**********************************************************************************************/

#include "raylib.h"
#include <stdlib.h>
#include <math.h>
#include "app_dpi.h"

#define MAX_STROKES     2048
#define STROKE_PTS        6      // vertices per stamp polyline
#define PI3  (PI / 3.0f)         // 60°, the dihedral D3 angle

typedef struct {
    Vector2 pts[STROKE_PTS];     // stamp vertices, RELATIVE to its own centre
    int     ptCount;
    float   width;
    Color   color;
    Vector2 centre;              // where it was stamped (window space)
    float   life;
    float   maxLife;
} Stroke;

static Stroke strokes[MAX_STROKES] = { 0 };
static int strokeCount = 0;

static float mirrorBase = 0.0f;   // rotates the 3 mirror lines together
static bool  paused = false;
static bool  showDebug = false;

static const Color palette[] = {
    {255,  90, 120, 255}, {255, 180,  60, 255}, {255, 240, 120, 255},
    {120, 230, 140, 255}, { 80, 200, 255, 255}, {180, 120, 255, 255},
    {255, 120, 220, 255}, {120, 255, 220, 255}
};
static const int paletteCount = sizeof(palette) / sizeof(palette[0]);

// Reflect point p across a line through the origin at angle a (radians).
static Vector2 ReflectLine(Vector2 p, float a)
{
    float c = cosf(a), s = sinf(a);
    // rotate into line frame, flip Y, rotate back.
    float x = p.x * c + p.y * s;
    float y = -p.x * s + p.y * c;
    Vector2 out;
    out.x = x * c - y * s;
    out.y = x * s + y * c;
    return out;
}

// Add a stamp centred at (mx,my). The stamp is a random polyline whose vertices
// are stored RELATIVE to (mx,my); mirroring later operates on these relative coords.
static void AddStroke(float mx, float my)
{
    if (strokeCount >= MAX_STROKES) {
        for (int i = 1; i < MAX_STROKES; i++) strokes[i - 1] = strokes[i];
        strokeCount = MAX_STROKES - 1;
    }
    Stroke s = { 0 };
    s.ptCount = STROKE_PTS;
    s.centre  = (Vector2){ mx, my };
    // first vertex at the centre, then random walk outward.
    float ang = (float)GetRandomValue(0, 628) / 100.0f;
    float r   = 0.0f;
    for (int i = 0; i < STROKE_PTS; i++) {
        ang += (GetRandomValue(-100, 100) / 100.0f) * 0.9f;
        r   += (GetRandomValue(20, 70) / 10.0f);
        s.pts[i].x = cosf(ang) * r;
        s.pts[i].y = sinf(ang) * r;
    }
    s.width  = (float)GetRandomValue(2, 6);
    s.color  = palette[GetRandomValue(0, paletteCount - 1)];
    s.maxLife = (float)GetRandomValue(60, 160) / 10.0f;
    s.life    = s.maxLife;
    strokes[strokeCount++] = s;
}

// Draw one stamp (polyline) translated to (cx,cy).
static void DrawStampAt(const Stroke *s, Vector2 origin, Color c)
{
    Vector2 pts[STROKE_PTS];
    for (int i = 0; i < s->ptCount; i++) {
        pts[i].x = origin.x + s->pts[i].x;
        pts[i].y = origin.y + s->pts[i].y;
    }
    for (int i = 1; i < s->ptCount; i++)
        DrawLineEx(pts[i - 1], pts[i], s->width, c);
    for (int i = 0; i < s->ptCount; i++)
        DrawCircleV(pts[i], s->width * 0.5f, c);
}

int main(void)
{
    app_enable_dpi();
    const int screenWidth = 800;
    const int screenHeight = 800;
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT);
    InitWindow(screenWidth, screenHeight, "raylib [app] example - Prism Kaleidoscope");

    SetTraceLogLevel(LOG_WARNING);
    SetRandomSeed((unsigned int)GetTime());
    SetTargetFPS(60);

    Vector2 center = { screenWidth / 2.0f, screenHeight / 2.0f };

    bool painting = false;
    float paintCooldown = 0.0f;

    // The 3 mirror lines (through canvas centre) at 0°, 60°, 120° + mirrorBase.
    // To get the full D3 set we reflect each stroke across each of the 3 lines
    // and combinations; the unique transforms are:
    //   identity,
    //   M0, M1, M2,              (single reflections)
    //   M0·M1, M1·M2             (double reflections give the two rotations)
    // i.e. 6 copies total.

    while (!WindowShouldClose())
    {
        float dt = GetFrameTime();
        Vector2 m = GetMousePosition();

        if (IsKeyPressed(KEY_SPACE)) strokeCount = 0;
        if (IsKeyPressed(KEY_R))    mirrorBase = (float)GetRandomValue(0, 600) / 100.0f;
        if (IsKeyPressed(KEY_P))    paused = !paused;
        if (IsKeyPressed(KEY_F1))   showDebug = !showDebug;
        if (IsKeyPressed(KEY_S))    TakeScreenshot("prism_kaleido_shot.png");

        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))  { painting = true;  paintCooldown = 0.0f; }
        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) painting = false;
        if (painting) {
            paintCooldown -= dt;
            if (paintCooldown <= 0.0f) {
                AddStroke(m.x, m.y);
                paintCooldown = 0.06f;   // ~16 stamps/sec while dragging
            }
        }

        // Age + compact dead strokes.
        for (int i = 0; i < strokeCount; i++) {
            if (!paused) strokes[i].life -= dt * 0.7f;
            if (strokes[i].life < 0) strokes[i].life = 0;
        }
        int w = 0;
        for (int i = 0; i < strokeCount; i++)
            if (strokes[i].life > 0) strokes[w++] = strokes[i];
        strokeCount = w;

        // ---- draw ----
        BeginDrawing();
        ClearBackground((Color){ 10, 8, 18, 255 });
        DrawCircleGradient((Vector2){ (int)center.x, (int)center.y }, 380,
                           (Color){ 35, 25, 60, 255 },
                           (Color){ 10, 8, 18, 255 });

        // 3 mirror line angles (for both the transforms and a faint visual guide).
        float a0 = mirrorBase;
        float a1 = mirrorBase + PI3;       // 60°
        float a2 = mirrorBase + 2.0f * PI3; // 120°

        // Faint mirror guides.
        Color guide = { 60, 55, 90, 90 };
        for (int i = 0; i < 3; i++) {
            float a = mirrorBase + i * PI3;
            float ex = center.x + cosf(a) * 600;
            float ey = center.y + sinf(a) * 600;
            DrawLineV(center, (Vector2){ ex, ey }, guide);
            DrawLineV(center, (Vector2){ center.x - cosf(a) * 600,
                                         center.y - sinf(a) * 600 }, guide);
        }

        // Render every stroke through the 6 dihedral transforms.
        for (int i = 0; i < strokeCount; i++) {
            Stroke *s = &strokes[i];
            unsigned char alpha = (unsigned char)(255.0f * (s->life / s->maxLife));
            Color c = { s->color.r, s->color.g, s->color.b, alpha };

            // Relative coords of stamp vertices to the canvas centre.
            Vector2 rel0 = { s->centre.x - center.x, s->centre.y - center.y };

            // 6 transforms of the stamp CENTRE (and thus the whole relative polyline).
            // Each transform maps the relative centre; the polyline offsets ride along.
            Vector2 centres[6];
            centres[0] = rel0;                              // identity
            centres[1] = ReflectLine(rel0, a0);             // M0
            centres[2] = ReflectLine(rel0, a1);             // M1
            centres[3] = ReflectLine(rel0, a2);             // M2
            centres[4] = ReflectLine(ReflectLine(rel0, a0), a1);  // M0·M1 (rotation)
            centres[5] = ReflectLine(ReflectLine(rel0, a1), a2);  // M1·M2 (rotation)

            for (int t = 0; t < 6; t++) {
                // For each transform we must also reflect the polyline's vertex
                // offsets so the shape itself mirrors (not just its position).
                Stroke mirrored = *s;
                // pick the reflection to apply to local offsets based on transform t.
                for (int v = 0; v < s->ptCount; v++) {
                    Vector2 rv = s->pts[v];
                    switch (t) {
                        case 0: mirrored.pts[v] = rv; break;
                        case 1: mirrored.pts[v] = ReflectLine(rv, a0); break;
                        case 2: mirrored.pts[v] = ReflectLine(rv, a1); break;
                        case 3: mirrored.pts[v] = ReflectLine(rv, a2); break;
                        case 4: mirrored.pts[v] = ReflectLine(ReflectLine(rv, a0), a1); break;
                        case 5: mirrored.pts[v] = ReflectLine(ReflectLine(rv, a1), a2); break;
                    }
                }
                Vector2 origin = { center.x + centres[t].x, center.y + centres[t].y };
                DrawStampAt(&mirrored, origin, c);
            }
        }

        // Live preview cross at the cursor.
        if (m.x >= 0 && m.y >= 0 && m.x < screenWidth && m.y < screenHeight) {
            DrawLine((int)m.x - 10, (int)m.y, (int)m.x + 10, (int)m.y,
                     (Color){ 255, 255, 255, 180 });
            DrawLine((int)m.x, (int)m.y - 10, (int)m.x, (int)m.y + 10,
                     (Color){ 255, 255, 255, 180 });
        }

        DrawText("Click / drag to stamp a pattern centred on the cursor",
                 10, 10, 14, RAYWHITE);
        DrawText(TextFormat("strokes: %d   mirrors: 3 @ %.0f°/%.0f°/%.0f°   %s",
                            strokeCount,
                            a0 * 180.0f / PI, a1 * 180.0f / PI, a2 * 180.0f / PI,
                            paused ? "PAUSED" : "running"),
                 10, screenHeight - 26, 12, GRAY);
        DrawText("SPACE clear   R re-roll mirror angle   P pause-fade   "
                 "S screenshot   F1 debug   ESC quit",
                 10, screenHeight - 12, 9, (Color){ 120, 120, 130, 255 });

        if (showDebug) {
            DrawFPS(screenWidth - 80, 10);
            DrawText(TextFormat("mouse=%.0f,%.0f  rel=%.0f,%.0f",
                                m.x, m.y, m.x - center.x, m.y - center.y),
                     10, 30, 11, GREEN);
        }

        EndDrawing();
    }

    CloseWindow();
    return 0;
}
