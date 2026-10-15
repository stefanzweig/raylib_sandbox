/**********************************************************************************************
*
*   raylib [app] example - 2D maze with mouse-driven ball and traced path
*
*   A grid maze generated with recursive-backtracker (DFS). The player is a ball that
*   follows the mouse: each cell the ball settles into is appended to a trail polyline
*   drawn from the entrance to the current position.
*
*   Controls:
*     Move mouse   - the ball walks toward the cell under the cursor (one step per move,
*                    never crossing walls).
*     R            - reset ball to entrance and clear trail.
*     G            - generate a new random maze.
*     F1           - toggle debug overlay (FPS, grid coords).
*     ESC          - quit.
*
**********************************************************************************************/

#include "raylib.h"
#include <stdlib.h>
#include "app_dpi.h"

#define COLS            21        // must be odd
#define ROWS            15        // must be odd
#define CELL            32        // pixels per cell
#define MAZE_W          (COLS * CELL)
#define MAZE_H          (ROWS * CELL)
#define MAX_TRAIL       (COLS * ROWS)

typedef struct {
    int x, y;
} Cell;

typedef struct {
    bool visited;
    bool wallN, wallS, wallE, wallW;   // walls between this cell and neighbour
} MazeCell;

static MazeCell grid[ROWS][COLS] = { 0 };

// Trail of visited cells (player path).
static Cell trail[MAX_TRAIL] = { 0 };
static int trailLen = 0;

static Cell entrance = { 1, 1 };
static Cell exitCell = { COLS - 2, ROWS - 2 };
static Cell ball     = { 1, 1 };        // current ball cell
static Cell cursor  = { 1, 1 };        // keyboard/mouse target cursor the ball chases
static float ballPx = 0, ballPy = 0;    // ball pixel pos (smoothed)
static bool won = false;

static int originX = 0, originY = 0;    // maze top-left in window space

//------------------------------------------------------------------------------------
// Maze generation: recursive backtracker using an explicit stack.
//------------------------------------------------------------------------------------
static void CarveBetween(Cell a, Cell b)
{
    if (a.y > b.y) { grid[a.y][a.x].wallN = false; grid[b.y][b.x].wallS = false; }
    if (a.y < b.y) { grid[a.y][a.x].wallS = false; grid[b.y][b.x].wallN = false; }
    if (a.x > b.x) { grid[a.y][a.x].wallW = false; grid[b.y][b.x].wallE = false; }
    if (a.x < b.x) { grid[a.y][a.x].wallE = false; grid[b.y][b.x].wallW = false; }
}

static void GenerateMaze(void)
{
    // Reset: all cells unvisited, all walls present.
    for (int y = 0; y < ROWS; y++)
        for (int x = 0; x < COLS; x++)
        {
            grid[y][x].visited = false;
            grid[y][x].wallN = grid[y][x].wallS =
            grid[y][x].wallE = grid[y][x].wallW = true;
        }

    Cell stack[ROWS * COLS];
    int sp = 0;
    stack[sp++] = (Cell){ entrance.x, entrance.y };
    grid[entrance.y][entrance.x].visited = true;

    const int dx[4] = { 0, 0, 1, -1 };
    const int dy[4] = { 1, -1, 0, 0 };

    while (sp > 0)
    {
        Cell cur = stack[sp - 1];
        // collect unvisited adjacent rooms
        Cell opts[4];
        int n = 0;
        for (int d = 0; d < 4; d++)
        {
            int nx = cur.x + dx[d], ny = cur.y + dy[d];
            // Only carve between interior rooms (1..COLS-2, 1..ROWS-2);
            // the outer ring stays sealed as the maze boundary wall.
            if (nx >= 1 && nx <= COLS - 2 &&
                ny >= 1 && ny <= ROWS - 2 && !grid[ny][nx].visited)
                opts[n++] = (Cell){ nx, ny };
        }
        if (n == 0) { sp--; continue; }
        Cell nxt = opts[GetRandomValue(0, n - 1)];
        CarveBetween(cur, nxt);
        grid[nxt.y][nxt.x].visited = true;
        stack[sp++] = nxt;
    }
}

static void ResetBall(void)
{
    ball = entrance;
    cursor = entrance;
    ballPx = originX + (entrance.x + 0.5f) * CELL;
    ballPy = originY + (entrance.y + 0.5f) * CELL;
    trailLen = 0;
    trail[trailLen++] = ball;
    won = (ball.x == exitCell.x && ball.y == exitCell.y);
}

//------------------------------------------------------------------------------------
// BFS: next step from `ball` toward `target` along the shortest open path.
// Returns the neighbour cell to step into, or `ball` if no path exists.
//------------------------------------------------------------------------------------
static Cell Neighbours[4];

static Cell NextStepToward(Cell target)
{
    if (target.x == ball.x && target.y == ball.y) return ball;

    static Cell parent[ROWS][COLS];
    static bool seen[ROWS][COLS];
    // (re)init only the cells we touch via a queue
    static Cell queue[ROWS * COLS];
    int head = 0, tail = 0;

    for (int y = 0; y < ROWS; y++)
        for (int x = 0; x < COLS; x++)
            seen[y][x] = false;

    queue[tail++] = ball;
    seen[ball.y][ball.x] = true;
    parent[ball.y][ball.x] = ball;

    bool found = false;
    while (head < tail)
    {
        Cell cur = queue[head++];
        if (cur.x == target.x && cur.y == target.y) { found = true; break; }

        // Build open neighbours of cur.
        int n = 0;
        if (!grid[cur.y][cur.x].wallN && cur.y > 0)      Neighbours[n++] = (Cell){ cur.x,     cur.y - 1 };
        if (!grid[cur.y][cur.x].wallS && cur.y < ROWS-1) Neighbours[n++] = (Cell){ cur.x,     cur.y + 1 };
        if (!grid[cur.y][cur.x].wallW && cur.x > 0)      Neighbours[n++] = (Cell){ cur.x - 1, cur.y     };
        if (!grid[cur.y][cur.x].wallE && cur.x < COLS-1) Neighbours[n++] = (Cell){ cur.x + 1, cur.y     };

        for (int i = 0; i < n; i++)
        {
            Cell nb = Neighbours[i];
            if (!seen[nb.y][nb.x])
            {
                seen[nb.y][nb.x] = true;
                parent[nb.y][nb.x] = cur;
                queue[tail++] = nb;
            }
        }
    }

    if (!found) return ball;  // unreachable

    // Walk parent chain back from target to the cell right after `ball`.
    Cell step = target;
    while (!(parent[step.y][step.x].x == ball.x && parent[step.y][step.x].y == ball.y))
        step = parent[step.y][step.x];
    return step;
}

static void StepToward(Cell target)
{
    // BFS gives the shortest open path from ball to target; step one cell along it.
    Cell next = NextStepToward(target);
    if (next.x == ball.x && next.y == ball.y) return;  // no path / already there
    ball = next;

    // Append to trail if the cell changed.
    if (ball.x != trail[trailLen - 1].x || ball.y != trail[trailLen - 1].y)
    {
        // If we revisited the previous cell, backtrack the trail (no loops in trail).
        if (trailLen >= 2 &&
            trail[trailLen - 2].x == ball.x && trail[trailLen - 2].y == ball.y)
        {
            trailLen--;
        }
        else if (trailLen < MAX_TRAIL)
        {
            trail[trailLen++] = ball;
        }
    }
    if (ball.x == exitCell.x && ball.y == exitCell.y) won = true;
}

//------------------------------------------------------------------------------------
// Drawing helpers
//------------------------------------------------------------------------------------
static void DrawMaze(void)
{
    // Cell floor with subtle checker.
    for (int y = 0; y < ROWS; y++)
        for (int x = 0; x < COLS; x++)
        {
            Color floor = ((x + y) & 1) ? (Color){ 244, 244, 240, 255 }
                                        : (Color){ 250, 250, 248, 255 };
            DrawRectangle(originX + x * CELL, originY + y * CELL, CELL, CELL, floor);
        }

    // Walls.
    // Walls: thick near-black for strong contrast against the light floor.
    const float wallW = 3.0f;
    Color wall = (Color){ 25, 28, 40, 255 };
    for (int y = 0; y < ROWS; y++)
        for (int x = 0; x < COLS; x++)
        {
            int px = originX + x * CELL;
            int py = originY + y * CELL;
            MazeCell c = grid[y][x];
            Vector2 tl = { px,        py };
            Vector2 tr = { px + CELL, py };
            Vector2 bl = { px,        py + CELL };
            Vector2 br = { px + CELL, py + CELL };
            if (c.wallN) DrawLineEx(tl, tr, wallW, wall);
            if (c.wallS) DrawLineEx(bl, br, wallW, wall);
            if (c.wallW) DrawLineEx(tl, bl, wallW, wall);
            if (c.wallE) DrawLineEx(tr, br, wallW, wall);
        }

    // Entrance / exit markers.
    DrawRectangle(originX + entrance.x * CELL + 6, originY + entrance.y * CELL + 6,
                  CELL - 12, CELL - 12, Fade(GREEN, 0.35f));
    DrawText("IN", originX + entrance.x * CELL + 7, originY + entrance.y * CELL + 9,
             CELL - 18, DARKGREEN);

    DrawRectangle(originX + exitCell.x * CELL + 6, originY + exitCell.y * CELL + 6,
                  CELL - 12, CELL - 12, Fade(RED, 0.35f));
    DrawText("OUT", originX + exitCell.x * CELL + 4, originY + exitCell.y * CELL + 9,
             CELL - 22, MAROON);
}

static void DrawTrail(void)
{
    if (trailLen < 2) return;
    // Faded polyline.
    for (int i = 1; i < trailLen; i++)
    {
        Vector2 a = { originX + (trail[i - 1].x + 0.5f) * CELL,
                      originY + (trail[i - 1].y + 0.5f) * CELL };
        Vector2 b = { originX + (trail[i].x + 0.5f) * CELL,
                      originY + (trail[i].y + 0.5f) * CELL };
        DrawLineEx(a, b, 3, (Color){ 70, 130, 220, 180 });
    }
    // Dots on each trail cell.
    for (int i = 0; i < trailLen; i++)
    {
        Vector2 p = { originX + (trail[i].x + 0.5f) * CELL,
                      originY + (trail[i].y + 0.5f) * CELL };
        DrawCircleV(p, 2.5f, (Color){ 40, 90, 170, 220 });
    }
}

//------------------------------------------------------------------------------------
// Program main entry point
//------------------------------------------------------------------------------------
int main(void)
{
    const int screenWidth = 900;
    const int screenHeight = 600;
    app_enable_dpi();
    InitWindow(screenWidth, screenHeight, "raylib [app] example - 2d maze trail");

    // Center the maze in the window.
    originX = (screenWidth  - MAZE_W) / 2;
    originY = (screenHeight - MAZE_H) / 2 + 10;

    SetTraceLogLevel(LOG_WARNING);
    SetRandomSeed((unsigned int)GetTime());
    SetTargetFPS(60);

    GenerateMaze();
    ResetBall();

    bool showDebug = false;
    bool teleportMode = false;        // T: click any reachable cell to warp there instantly
    float stepTimer = 0.0f;       // gates how often the ball steps toward the cursor
    const float stepInterval = 0.04f;

    // Move-cooldown: only step when mouse is inside the maze.
    while (!WindowShouldClose())
    {
        float dt = GetFrameTime();
        stepTimer += dt;

        if (IsKeyPressed(KEY_R)) ResetBall();
        if (IsKeyPressed(KEY_G)) { GenerateMaze(); ResetBall(); }
        if (IsKeyPressed(KEY_F1)) showDebug = !showDebug;
        if (IsKeyPressed(KEY_T)) teleportMode = !teleportMode;

        // Keyboard moves the CURSOR (can roam freely, ignores walls). The ball
        // then walks toward the cursor via BFS, so it auto-navigates around walls.
        if (!won)
        {
            if (IsKeyPressed(KEY_D) || IsKeyPressed(KEY_RIGHT))  cursor.x += 1;
            if (IsKeyPressed(KEY_A) || IsKeyPressed(KEY_LEFT))   cursor.x -= 1;
            if (IsKeyPressed(KEY_S) || IsKeyPressed(KEY_DOWN))  cursor.y += 1;
            if (IsKeyPressed(KEY_W) || IsKeyPressed(KEY_UP))    cursor.y -= 1;
            // clamp cursor inside the playable area
            if (cursor.x < 1) cursor.x = 1;
            if (cursor.y < 1) cursor.y = 1;
            if (cursor.x > COLS - 2) cursor.x = COLS - 2;
            if (cursor.y > ROWS - 2) cursor.y = ROWS - 2;
        }

        // Mouse also positions the cursor when over the maze.
        Vector2 m = GetMousePosition();
        Cell target = ball;
        if (m.x >= originX && m.y >= originY &&
            m.x < originX + MAZE_W && m.y < originY + MAZE_H)
        {
            int mcx = (int)((m.x - originX) / CELL);
            int mcy = (int)((m.y - originY) / CELL);
            if (mcx < 1) mcx = 1;
            if (mcx > COLS - 2) mcx = COLS - 2;
            if (mcy < 1) mcy = 1;
            if (mcy > ROWS - 2) mcy = ROWS - 2;
            cursor.x = mcx;  cursor.y = mcy;
        }
        target = cursor;   // ball always chases the cursor

        // Teleport mode: left-click warps the ball straight to the cursor cell
        // (if a path exists), appending only that cell to the trail.
        if (!won && teleportMode && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
            (target.x != ball.x || target.y != ball.y))
        {
            Cell next = NextStepToward(target);
            if (!(next.x == ball.x && next.y == ball.y))
            {
                ball = target;
                ballPx = originX + (ball.x + 0.5f) * CELL;
                ballPy = originY + (ball.y + 0.5f) * CELL;
                if (trailLen < MAX_TRAIL) trail[trailLen++] = ball;
                if (ball.x == exitCell.x && ball.y == exitCell.y) won = true;
            }
        }

        // Walk mode: ball walks one BFS step toward the cursor at fixed cadence.
        if (!won && !teleportMode && (target.x != ball.x || target.y != ball.y) && stepTimer >= stepInterval)
        {
            StepToward(target);
            stepTimer = 0.0f;
        }

        // Smooth ball pixel position toward current cell centre.
        float gx = originX + (ball.x + 0.5f) * CELL;
        float gy = originY + (ball.y + 0.5f) * CELL;
        ballPx += (gx - ballPx) * 0.30f;
        ballPy += (gy - ballPy) * 0.30f;

        // Draw
        BeginDrawing();
        ClearBackground((Color){ 30, 32, 40, 255 });

        DrawMaze();
        DrawTrail();

        // Draw the target cursor cell (orange box + cross) the ball is chasing.
        {
            int cx = cursor.x, cy = cursor.y;
            DrawRectangleLines(originX + cx * CELL, originY + cy * CELL, CELL, CELL,
                               Fade(ORANGE, 0.85f));
            Vector2 cc = { originX + (cx + 0.5f) * CELL, originY + (cy + 0.5f) * CELL };
            DrawLine((int)cc.x - 4, (int)cc.y,     (int)cc.x + 4, (int)cc.y,     ORANGE);
            DrawLine((int)cc.x,     (int)cc.y - 4, (int)cc.x,     (int)cc.y + 4, ORANGE);
        }

        // The ball.
        DrawCircleV((Vector2){ ballPx, ballPy }, CELL * 0.32f,
                   won ? Fade(GOLD, 0.9f) : Fade(SKYBLUE, 0.95f));
        DrawCircleLines((int)ballPx, (int)ballPy, (int)(CELL * 0.32f),
                        won ? ORANGE : DARKBLUE);

        // HUD
        DrawText(TextFormat("raylib [app] maze trail  %s  - reach red OUT from green IN",
                            teleportMode ? "[TELEPORT MODE]" : "[WALK MODE]"),
                 originX, originY - 22, 13, RAYWHITE);
        DrawText(TextFormat("trail: %d cells", trailLen),
                 originX, originY + MAZE_H + 6, 12, GRAY);
        DrawText(teleportMode
                 ? "[TELEPORT] move cursor (mouse/WASD) then click to warp   T walk   R reset   G new   ESC quit"
                 : "[WALK] move cursor (mouse OR WASD/arrows); ball follows   T teleport   R reset   G new   F1 debug   ESC quit",
                 originX + MAZE_W - 420, originY + MAZE_H + 6, 11, GRAY);

        if (won)
        {
            const char *msg = "  Solved!  Press R to retry, G for a new maze.  ";
            int w = MeasureText(msg, 26);
            DrawRectangle(screenWidth/2 - w/2 - 12, screenHeight/2 - 24, w + 24, 48,
                          Fade(BLACK, 0.7f));
            DrawText(msg, screenWidth/2 - w/2, screenHeight/2 - 13, 26, GOLD);
        }

        if (showDebug)
        {
            DrawFPS(8, 8);
            DrawText(TextFormat("ball %d,%d  target %d,%d  won=%d  mode=%s",
                                ball.x, ball.y, target.x, target.y, won,
                                teleportMode ? "teleport" : "walk"),
                     8, 28, 11, GREEN);
        }

        EndDrawing();
    }

    CloseWindow();
    return 0;
}
