/**********************************************************************************************
*
*   raylib [app] example - skeleton drawing app (menu / toolbar / statusbar)
*
*   A skeleton drawing application with a classic desktop UI:
*     - Menu bar at top (File / Shapes / Help)
*     - Toolbar below the menu (select / circle / rectangle / triangle tools)
*     - Status bar at bottom (tool, mouse position, shape count)
*     - Canvas in the middle
*
*   Tools:
*     V / Left-click on toolbar   - Select tool (click a shape to select it)
*     C                            - Draw circle
*     R                            - Draw rectangle
*     T                            - Draw triangle
*
*   Canvas interaction:
*     Drag with a shape tool       - draw a new shape (live preview while dragging)
*     Left-click a shape (select)  - select it (highlighted with white outline)
*     Right-click a selected shape  - popup context menu with "Delete"
*     Del key                      - delete selected shape
*
*   Menu:
*     File -> Exit                 - quit
*     Shapes -> Clear All          - remove every shape
*     Help -> About                - show a message box
*
**********************************************************************************************/

#include "raylib.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "app_dpi.h"

// ---- layout -------------------------------------------------------------------------------
#define MENU_H       28
#define TOOLBAR_H    44
#define STATUS_H     24
#define CANVAS_Y     (MENU_H + TOOLBAR_H)
#define CANVAS_H     (650 - CANVAS_Y - STATUS_H)
#define WIN_W        900
#define WIN_H        (CANVAS_Y + CANVAS_H + STATUS_H)

// ---- shapes -------------------------------------------------------------------------------
typedef enum { SHAPE_CIRCLE, SHAPE_RECT, SHAPE_TRIANGLE } ShapeType;

typedef struct {
    ShapeType type;
    float x, y;       // bounding-box top-left
    float w, h;       // bounding-box size (w==h for circle = diameter)
    Color color;
    bool selected;
} Shape;

#define MAX_SHAPES 512
static Shape shapes[MAX_SHAPES];
static int shapeCount = 0;
static int selectedIdx = -1;

// ---- tools --------------------------------------------------------------------------------
typedef enum { TOOL_SELECT, TOOL_CIRCLE, TOOL_RECT, TOOL_TRIANGLE } Tool;
static Tool currentTool = TOOL_SELECT;

// ---- menu state ---------------------------------------------------------------------------
#define MAX_MENUS    4
static const char *menuNames[MAX_MENUS] = { "File", "Shapes", "Help", "" };
static int activeMenu = -1;          // which dropdown is open (-1 = closed)
static Rectangle menuRects[MAX_MENUS];
// dropdown item layout (computed each frame from activeMenu)
#define MAX_DROPDOWN_ITEMS 6
static int dropdownItemCount = 0;
static Rectangle dropdownRects[MAX_DROPDOWN_ITEMS];
static const char *dropdownLabels[MAX_DROPDOWN_ITEMS];

// ---- popup (right-click) menu -------------------------------------------------------------
static bool popupOpen = false;
static int popupX = 0, popupY = 0;
#define POPUP_W   120
static Rectangle popupDeleteRect;
static bool aboutVisible = false;
static bool shouldQuit = false;

// ---- toolbar buttons ----------------------------------------------------------------------
#define TOOL_BTN_SIZE  34
#define TOOL_BTN_GAP   6
typedef struct {
    Rectangle rect;
    Tool tool;
    const char *hint;
} ToolButton;
static ToolButton toolBtns[4];

// ---- drawing in progress ------------------------------------------------------------------
static bool drawing = false;
static float drawStartX, drawStartY;
static float drawCurX, drawCurY;

// ---- status text --------------------------------------------------------------------------
static char statusText[128];

//--------------------------------------------------------------------------------------------
// helpers
//--------------------------------------------------------------------------------------------
static void DeselectAll(void) {
    for (int i = 0; i < shapeCount; i++) shapes[i].selected = false;
    selectedIdx = -1;
}

static bool PointInShape(Shape *s, float px, float py) {
    // quick bounding-box reject first
    if (px < s->x || px > s->x + s->w || py < s->y || py > s->y + s->h) return false;
    if (s->type == SHAPE_CIRCLE) {
        float cx = s->x + s->w / 2, cy = s->y + s->h / 2;
        float dx = px - cx, dy = py - cy;
        return (dx*dx + dy*dy) <= (s->w/2)*(s->w/2);
    }
    if (s->type == SHAPE_RECT) return true;  // already bbox check
    // triangle: vertices (top, bottom-left, bottom-right)
    float tx = s->x + s->w/2, ty = s->y;
    float lx = s->x,          ly = s->y + s->h;
    float rx = s->x + s->w,  ry = s->y + s->h;
    // barycentric sign test
    float d1 = (px - rx) * (ty - ry) - (tx - rx) * (py - ry);
    float d2 = (px - lx) * (ty - ly) - (tx - lx) * (py - ly);
    float d3 = (px - lx) * (ry - ly) - (rx - lx) * (py - ly);
    bool neg = (d1 < 0) || (d2 < 0) || (d3 < 0);
    bool pos = (d1 > 0) || (d2 > 0) || (d3 > 0);
    return !(neg && pos);
}

static int HitTestShapes(float px, float py) {
    // topmost first (iterate reverse)
    for (int i = shapeCount - 1; i >= 0; i--)
        if (PointInShape(&shapes[i], px, py)) return i;
    return -1;
}

static void AddShape(ShapeType type, float x1, float y1, float x2, float y2) {
    if (shapeCount >= MAX_SHAPES) return;
    Shape *s = &shapes[shapeCount];
    s->type = type;
    s->x = fminf(x1, x2);
    s->y = fminf(y1, y2);
    s->w = fabsf(x2 - x1);
    s->h = fabsf(y2 - y1);
    if (s->w < 4) s->w = 4;
    if (s->h < 4) s->h = 4;
    if (type == SHAPE_CIRCLE) {  // keep circle circular
        float d = fmaxf(s->w, s->h);
        s->w = s->h = d;
    }
    s->color = (Color){ (unsigned char)GetRandomValue(80, 220),
                        (unsigned char)GetRandomValue(80, 220),
                        (unsigned char)GetRandomValue(80, 220), 255 };
    s->selected = false;
    shapeCount++;
}

static void DeleteSelected(void) {
    if (selectedIdx < 0) return;
    for (int i = selectedIdx; i < shapeCount - 1; i++) shapes[i] = shapes[i+1];
    shapeCount--;
    selectedIdx = -1;
}

static void ClearAll(void) {
    shapeCount = 0;
    selectedIdx = -1;
}

//--------------------------------------------------------------------------------------------
// menu building: fill dropdownRects / dropdownLabels based on activeMenu
//--------------------------------------------------------------------------------------------
static void BuildDropdown(int menuIdx) {
    dropdownItemCount = 0;
    float dx = menuRects[menuIdx].x;
    float dy = menuRects[menuIdx].y + menuRects[menuIdx].height;

    if (menuIdx == 0) {  // File
        dropdownLabels[0] = "Exit";
        dropdownRects[0] = (Rectangle){ dx, dy, 140, 24 };
        dropdownItemCount = 1;
    } else if (menuIdx == 1) {  // Shapes
        dropdownLabels[0] = "Circle";
        dropdownLabels[1] = "Rectangle";
        dropdownLabels[2] = "Triangle";
        dropdownLabels[3] = "Clear All";
        for (int i = 0; i < 4; i++)
            dropdownRects[i] = (Rectangle){ dx, dy + i*24, 140, 24 };
        dropdownItemCount = 4;
    } else if (menuIdx == 2) {  // Help
        dropdownLabels[0] = "About";
        dropdownRects[0] = (Rectangle){ dx, dy, 140, 24 };
        dropdownItemCount = 1;
    }
}

//--------------------------------------------------------------------------------------------
// drawing
//--------------------------------------------------------------------------------------------
static void DrawShape(Shape *s) {
    switch (s->type) {
        case SHAPE_CIRCLE:
            DrawCircle(s->x + s->w/2, s->y + s->h/2, s->w/2, s->color);
            break;
        case SHAPE_RECT:
            DrawRectangleRec((Rectangle){ s->x, s->y, s->w, s->h }, s->color);
            break;
        case SHAPE_TRIANGLE:
            DrawTriangle((Vector2){ s->x + s->w/2, s->y },
                         (Vector2){ s->x, s->y + s->h },
                         (Vector2){ s->x + s->w, s->y + s->h },
                         s->color);
            break;
    }
    if (s->selected) {
        DrawRectangleLinesEx((Rectangle){ s->x - 2, s->y - 2, s->w + 4, s->h + 4 },
                             2.0f, RAYWHITE);
    }
}

static void DrawMenuBar(void) {
    DrawRectangle(0, 0, WIN_W, MENU_H, (Color){ 45, 48, 56, 255 });
    float mx = 10;
    for (int i = 0; i < MAX_MENUS && menuNames[i][0]; i++) {
        float mw = MeasureText(menuNames[i], 14) + 20;
        menuRects[i] = (Rectangle){ mx, 2, mw, MENU_H - 4 };
        Color bg = (i == activeMenu) ? (Color){ 70, 74, 84, 255 }
                                     : (Color){ 45, 48, 56, 255 };
        DrawRectangleRec(menuRects[i], bg);
        DrawText(menuNames[i], mx + 10, 8, 14, RAYWHITE);
        mx += mw;
    }
}

static void DrawDropdown(void) {
    if (activeMenu < 0) return;
    for (int i = 0; i < dropdownItemCount; i++) {
        DrawRectangleRec(dropdownRects[i], (Color){ 60, 63, 70, 255 });
        DrawRectangleLinesEx(dropdownRects[i], 1, (Color){ 90, 93, 100, 255 });
        DrawText(dropdownLabels[i], dropdownRects[i].x + 8, dropdownRects[i].y + 5, 14, RAYWHITE);
    }
}

static void DrawToolbar(void) {
    DrawRectangle(0, MENU_H, WIN_W, TOOLBAR_H, (Color){ 55, 58, 66, 255 });
    float tx = 10;
    for (int i = 0; i < 4; i++) {
        toolBtns[i].rect = (Rectangle){ tx, MENU_H + 5, TOOL_BTN_SIZE, TOOL_BTN_SIZE };
        toolBtns[i].tool = (Tool)i;
        bool active = (currentTool == i);
        Color bg = active ? (Color){ 90, 140, 200, 255 }
                          : (Color){ 70, 73, 80, 255 };
        DrawRectangleRec(toolBtns[i].rect, bg);
        DrawRectangleLinesEx(toolBtns[i].rect, 1, (Color){ 120, 123, 130, 255 });
        // icon glyph
        float cx = tx + TOOL_BTN_SIZE/2;
        float cy = MENU_H + 5 + TOOL_BTN_SIZE/2;
        switch (i) {
            case TOOL_SELECT:
                DrawTriangleLines((Vector2){cx-6,cy-7}, (Vector2){cx-6,cy+6}, (Vector2){cx+5,cy}, RAYWHITE);
                break;
            case TOOL_CIRCLE:
                DrawCircle(cx, cy, 8, SKYBLUE);
                break;
            case TOOL_RECT:
                DrawRectangle(cx-8, cy-6, 16, 12, GOLD);
                break;
            case TOOL_TRIANGLE:
                DrawTriangle((Vector2){cx,cy-8}, (Vector2){cx-8,cy+6}, (Vector2){cx+8,cy+6}, GREEN);
                break;
        }
        tx += TOOL_BTN_SIZE + TOOL_BTN_GAP;
    }
}

static void DrawStatusBar(void) {
    DrawRectangle(0, WIN_H - STATUS_H, WIN_W, STATUS_H, (Color){ 45, 48, 56, 255 });
    DrawText(statusText, 8, WIN_H - STATUS_H + 5, 13, RAYWHITE);
    char count[32];
    snprintf(count, sizeof(count), "Shapes: %d", shapeCount);
    DrawText(count, WIN_W - 100, WIN_H - STATUS_H + 5, 13, RAYWHITE);
}

static void DrawPopup(void) {
    if (!popupOpen) return;
    DrawRectangle(popupX, popupY, POPUP_W, 26, (Color){ 60, 63, 70, 255 });
    DrawRectangleLines(popupX, popupY, POPUP_W, 26, (Color){ 120, 123, 130, 255 });
    popupDeleteRect = (Rectangle){ popupX, popupY, POPUP_W, 26 };
    DrawText("Delete", popupX + 10, popupY + 5, 14, RAYWHITE);
}

//--------------------------------------------------------------------------------------------
// menu / toolbar click dispatch
//--------------------------------------------------------------------------------------------
static void HandleDropdownClick(float mx, float my) {
    for (int i = 0; i < dropdownItemCount; i++) {
        if (CheckCollisionPointRec((Vector2){mx, my}, dropdownRects[i])) {
            const char *label = dropdownLabels[i];
            if (strcmp(label, "Exit") == 0) { shouldQuit = true; }
            else if (strcmp(label, "Clear All") == 0) { ClearAll(); }
            else if (strcmp(label, "Circle") == 0)   { currentTool = TOOL_CIRCLE; }
            else if (strcmp(label, "Rectangle") == 0){ currentTool = TOOL_RECT; }
            else if (strcmp(label, "Triangle") == 0) { currentTool = TOOL_TRIANGLE; }
            else if (strcmp(label, "About") == 0) { aboutVisible = !aboutVisible; }
            activeMenu = -1;
            return;
        }
    }
    // clicked outside -> close
    activeMenu = -1;
}

//--------------------------------------------------------------------------------------------
// main
//--------------------------------------------------------------------------------------------
int main(void) {
    app_enable_dpi();
    InitWindow(WIN_W, WIN_H, "raylib skeleton - draw shapes");
    SetTargetFPS(60);
    SetExitKey(KEY_NULL);   // we handle quit via menu/ESC

    int mon = GetCurrentMonitor();
    SetWindowPosition((GetMonitorWidth(mon) - WIN_W)/2,
                      (GetMonitorHeight(mon) - WIN_H)/2);

    while (!WindowShouldClose() && !shouldQuit) {
        float mx = GetMousePosition().x;
        float my = GetMousePosition().y;

        // ---- keyboard shortcuts ----
        if (IsKeyPressed(KEY_V)) currentTool = TOOL_SELECT;
        if (IsKeyPressed(KEY_C)) currentTool = TOOL_CIRCLE;
        if (IsKeyPressed(KEY_R)) currentTool = TOOL_RECT;
        if (IsKeyPressed(KEY_T)) currentTool = TOOL_TRIANGLE;
        if (IsKeyPressed(KEY_DELETE)) DeleteSelected();
        if (IsKeyPressed(KEY_ESCAPE)) {
            if (popupOpen) popupOpen = false;
            else if (activeMenu >= 0) activeMenu = -1;
            else shouldQuit = true;
        }

        // ---- menu bar clicks ----
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            if (aboutVisible) { aboutVisible = false; }
            bool onMenu = false;
            for (int i = 0; i < MAX_MENUS && menuNames[i][0]; i++) {
                if (CheckCollisionPointRec((Vector2){mx, my}, menuRects[i])) {
                    activeMenu = (activeMenu == i) ? -1 : i;
                    if (activeMenu >= 0) BuildDropdown(activeMenu);
                    onMenu = true;
                    break;
                }
            }
            if (!onMenu && activeMenu >= 0) {
                HandleDropdownClick(mx, my);
            }
        }

        // ---- dropdown open: check hover ----
        if (activeMenu >= 0) {
            for (int i = 0; i < dropdownItemCount; i++)
                if (CheckCollisionPointRec((Vector2){mx, my}, dropdownRects[i])) {
                    DrawRectangleRec(dropdownRects[i], (Color){ 90, 120, 170, 255 });
                    DrawText(dropdownLabels[i], dropdownRects[i].x + 8, dropdownRects[i].y + 5, 14, RAYWHITE);
                }
        }

        // ---- toolbar clicks ----
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            for (int i = 0; i < 4; i++) {
                if (CheckCollisionPointRec((Vector2){mx, my}, toolBtns[i].rect)) {
                    currentTool = (Tool)i;
                    DeselectAll();
                }
            }
        }

        // ---- canvas interactions ----
        bool onCanvas = (my >= CANVAS_Y) && (my < WIN_H - STATUS_H);

        // right-click on a selected shape -> popup
        if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT) && onCanvas) {
            int hit = HitTestShapes(mx, my);
            if (hit >= 0) {
                DeselectAll();
                shapes[hit].selected = true;
                selectedIdx = hit;
                popupOpen = true;
                popupX = (int)mx;
                popupY = (int)my;
            } else {
                popupOpen = false;
            }
        }

        // popup click handling
        if (popupOpen && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            if (CheckCollisionPointRec((Vector2){mx, my}, popupDeleteRect)) {
                DeleteSelected();
                popupOpen = false;
            } else if (!CheckCollisionPointRec((Vector2){mx, my}, (Rectangle){popupX, popupY, POPUP_W, 26})) {
                popupOpen = false;  // clicked elsewhere
            }
        }

        // left-button drawing / selecting
        if (onCanvas && !popupOpen) {
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                if (currentTool == TOOL_SELECT) {
                    int hit = HitTestShapes(mx, my);
                    DeselectAll();
                    if (hit >= 0) { shapes[hit].selected = true; selectedIdx = hit; }
                } else {
                    drawing = true;
                    drawStartX = mx;
                    drawStartY = my;
                    drawCurX = mx;
                    drawCurY = my;
                }
            }
            if (drawing && (currentTool != TOOL_SELECT)) {
                drawCurX = mx;
                drawCurY = my;
            }
            if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && drawing) {
                drawing = false;
                ShapeType t = SHAPE_CIRCLE;
                if (currentTool == TOOL_RECT) t = SHAPE_RECT;
                else if (currentTool == TOOL_TRIANGLE) t = SHAPE_TRIANGLE;
                AddShape(t, drawStartX, drawStartY, drawCurX, drawCurY);
            }
        }

        // ---- build status text ----
        const char *toolName = "Select";
        if (currentTool == TOOL_CIRCLE) toolName = "Circle";
        else if (currentTool == TOOL_RECT) toolName = "Rectangle";
        else if (currentTool == TOOL_TRIANGLE) toolName = "Triangle";
        snprintf(statusText, sizeof(statusText), "Tool: %s   |   pos: %d,%d",
                 toolName, (int)mx, (int)my);

        // ============== draw ==============
        BeginDrawing();
            ClearBackground((Color){ 30, 32, 38, 255 });

            // canvas background
            DrawRectangle(0, CANVAS_Y, WIN_W, CANVAS_H, (Color){ 240, 240, 235, 255 });

            // shapes
            for (int i = 0; i < shapeCount; i++) DrawShape(&shapes[i]);

            // live preview while dragging
            if (drawing) {
                Rectangle r = { fminf(drawStartX, drawCurX), fminf(drawStartY, drawCurY),
                                fabsf(drawCurX - drawStartX), fabsf(drawCurY - drawStartY) };
                if (currentTool == TOOL_CIRCLE) {
                    float d = fmaxf(r.width, r.height);
                    DrawCircleLines(r.x + r.width/2, r.y + r.height/2, d/2, RED);
                } else if (currentTool == TOOL_RECT) {
                    DrawRectangleLinesEx(r, 1.5f, RED);
                } else if (currentTool == TOOL_TRIANGLE) {
                    DrawTriangleLines((Vector2){r.x + r.width/2, r.y},
                                      (Vector2){r.x, r.y + r.height},
                                      (Vector2){r.x + r.width, r.y + r.height}, RED);
                }
            }

            DrawMenuBar();
            DrawToolbar();
            DrawStatusBar();
            DrawDropdown();
            DrawPopup();

            if (aboutVisible) {
                DrawRectangle(0, 0, WIN_W, WIN_H, (Color){0,0,0,150});
                DrawRectangle(WIN_W/2-180, WIN_H/2-70, 360, 140, (Color){50,53,60,255});
                DrawRectangleLines(WIN_W/2-180, WIN_H/2-70, 360, 140, RAYWHITE);
                DrawText("raylib skeleton app", WIN_W/2 - MeasureText("raylib skeleton app",18)/2, WIN_H/2-40, 18, RAYWHITE);
                DrawText("Menu / toolbar / statusbar / shape drawing", WIN_W/2 - MeasureText("Menu / toolbar / statusbar / shape drawing",13)/2, WIN_H/2-10, 13, LIGHTGRAY);
                DrawText("Click anywhere to close", WIN_W/2 - MeasureText("Click anywhere to close",12)/2, WIN_H/2+30, 12, GRAY);
            }

            if (shapeCount == 0 && !drawing) {
                const char *hint = "Pick a tool above, then drag on the canvas to draw.";
                DrawText(hint, WIN_W/2 - MeasureText(hint, 14)/2,
                         CANVAS_Y + CANVAS_H/2 - 10, 14, GRAY);
            }
        EndDrawing();
    }

    CloseWindow();
    return 0;
}
