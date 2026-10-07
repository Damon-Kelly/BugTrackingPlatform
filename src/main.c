// Bug Tracker prototype - slice 1
// Single file, in-memory data, hand-rolled immediate-mode UI.
#include "raylib.h"
#include <string.h>
#include <stdio.h>

// ---------- Data model ----------
#define MAX_BUGS 128

typedef enum { STATUS_OPEN, STATUS_IN_PROGRESS, STATUS_RESOLVED, STATUS_COUNT } Status;
typedef enum { PRIO_LOW, PRIO_MEDIUM, PRIO_HIGH, PRIO_CRITICAL, PRIO_COUNT } Priority;

typedef struct {
    int      id;
    char     title[64];
    char     description[512];
    char     steps[512];
    Status   status;
    Priority priority;
} Bug;

static Bug bugs[MAX_BUGS];
static int bugCount = 0;
static int nextId   = 1;
static int selected = -1;   // index into bugs[], -1 = nothing selected

static const char *statusNames[STATUS_COUNT] = { "Open", "In Progress", "Resolved" };
static const char *prioNames[PRIO_COUNT]     = { "Low", "Medium", "High", "Critical" };
static Color statusColors[STATUS_COUNT]      = { { 220, 80, 80, 255 }, { 230, 170, 40, 255 }, { 80, 180, 100, 255 } };
static Color prioColors[PRIO_COUNT]          = { { 130, 140, 150, 255 }, { 70, 130, 200, 255 }, { 230, 140, 40, 255 }, { 200, 50, 50, 255 } };

// ---------- UI state ----------
typedef enum { FIELD_NONE, FIELD_TITLE, FIELD_DESC, FIELD_STEPS } FieldId;
static FieldId activeField = FIELD_NONE;

#define COL_BG      (Color){ 30, 32, 38, 255 }
#define COL_PANEL   (Color){ 40, 43, 51, 255 }
#define COL_ROW     (Color){ 50, 54, 64, 255 }
#define COL_ROW_SEL (Color){ 65, 90, 140, 255 }
#define COL_TEXT    (Color){ 230, 232, 238, 255 }
#define COL_DIM     (Color){ 140, 146, 160, 255 }
#define LINE_H      22

// ---------- Helpers ----------
static void AddBug(const char *title, const char *desc, const char *steps, Status s, Priority p)
{
    if (bugCount >= MAX_BUGS) return;
    Bug *b = &bugs[bugCount];
    memset(b, 0, sizeof(Bug));
    b->id = nextId++;
    strncpy(b->title, title, sizeof(b->title) - 1);
    strncpy(b->description, desc, sizeof(b->description) - 1);
    strncpy(b->steps, steps, sizeof(b->steps) - 1);
    b->status = s;
    b->priority = p;
    bugCount++;
}

// Immediate-mode button: draws AND reports a click in one call.
static bool Button(Rectangle r, const char *label, Color base)
{
    bool hover = CheckCollisionPointRec(GetMousePosition(), r);
    Color c = hover ? ColorBrightness(base, 0.2f) : base;
    DrawRectangleRounded(r, 0.2f, 6, c);
    int w = MeasureText(label, 18);
    DrawText(label, (int)(r.x + (r.width - w) / 2), (int)(r.y + (r.height - 18) / 2), 18, WHITE);
    return hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

// Text field. Edits `buf` in place when it is the active field.
static void TextField(Rectangle r, const char *label, char *buf, int cap, FieldId id, bool multiline)
{
    DrawText(label, (int)r.x, (int)r.y - 20, 16, COL_DIM);

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(GetMousePosition(), r))
        activeField = id;

    bool active = (activeField == id);

    if (active) {
        int len = (int)strlen(buf);
        int ch;
        while ((ch = GetCharPressed()) > 0) {                 // typed characters this frame
            if (ch >= 32 && ch <= 126 && len < cap - 1) { buf[len++] = (char)ch; buf[len] = '\0'; }
        }
        if ((IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) && len > 0)
            buf[--len] = '\0';
        if (multiline && IsKeyPressed(KEY_ENTER) && len < cap - 1) { buf[len++] = '\n'; buf[len] = '\0'; }
    }

    DrawRectangleRec(r, (Color){ 24, 26, 31, 255 });
    DrawRectangleLinesEx(r, 2, active ? (Color){ 90, 140, 230, 255 } : (Color){ 70, 74, 86, 255 });

    // Clip drawing to the box so long text can't spill out.
    BeginScissorMode((int)r.x + 2, (int)r.y + 2, (int)r.width - 4, (int)r.height - 4);
    {
        int y = (int)r.y + 8;
        const char *p = buf;
        char line[256];
        while (1) {
            const char *nl = strchr(p, '\n');
            int n = nl ? (int)(nl - p) : (int)strlen(p);
            if (n > 255) n = 255;
            memcpy(line, p, n);
            line[n] = '\0';
            DrawText(line, (int)r.x + 8, y, 18, COL_TEXT);
            if (!nl) {   // last line: draw blinking cursor after it
                if (active && ((int)(GetTime() * 2) % 2 == 0))
                    DrawRectangle((int)r.x + 8 + MeasureText(line, 18) + 2, y, 2, 18, COL_TEXT);
                break;
            }
            p = nl + 1;
            y += LINE_H;
        }
    }
    EndScissorMode();
}

// ---------- Main ----------
int main(void)
{
    InitWindow(1100, 700, "Bug Tracker");
    SetExitKey(KEY_NULL);   // so ESC doesn't close the app while you're typing
    SetTargetFPS(60);

    AddBug("Crash when saving empty file", "App segfaults if the user hits Save with no content.",
           "1. Launch app\n2. Press Ctrl+S immediately", STATUS_OPEN, PRIO_CRITICAL);
    AddBug("Button label clipped on small windows", "The 'Submit' label is cut off below 800px width.",
           "1. Resize window to 700px wide", STATUS_IN_PROGRESS, PRIO_LOW);
    AddBug("Typo on settings page", "'Prefrences' should be 'Preferences'.",
           "1. Open Settings", STATUS_RESOLVED, PRIO_MEDIUM);
    selected = 0;

    const int listW = 340;

    while (!WindowShouldClose())
    {
        // Clicking empty space clears focus; fields re-set it if clicked (see TextField).
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) activeField = FIELD_NONE;

        BeginDrawing();
        ClearBackground(COL_BG);

        // ----- Left panel: bug list -----
        DrawRectangle(0, 0, listW, GetScreenHeight(), COL_PANEL);
        DrawText("Bugs", 16, 16, 28, COL_TEXT);
        if (Button((Rectangle){ listW - 120, 14, 104, 34 }, "+ New Bug", (Color){ 60, 120, 200, 255 })) {
            AddBug("", "", "", STATUS_OPEN, PRIO_MEDIUM);
            selected = bugCount - 1;
            activeField = FIELD_TITLE;
        }

        for (int i = 0; i < bugCount; i++) {
            Rectangle row = { 8, 64 + i * 58, listW - 16, 52 };
            bool hover = CheckCollisionPointRec(GetMousePosition(), row);
            DrawRectangleRounded(row, 0.15f, 6, (i == selected) ? COL_ROW_SEL : (hover ? ColorBrightness(COL_ROW, 0.1f) : COL_ROW));
            DrawCircle((int)row.x + 16, (int)row.y + 26, 7, statusColors[bugs[i].status]);

            const char *t = bugs[i].title[0] ? bugs[i].title : "(untitled)";
            BeginScissorMode((int)row.x + 30, (int)row.y, (int)row.width - 38, (int)row.height);
            DrawText(t, (int)row.x + 32, (int)row.y + 8, 18,
                     bugs[i].status == STATUS_RESOLVED ? COL_DIM : COL_TEXT);
            DrawText(TextFormat("#%d  %s", bugs[i].id, prioNames[bugs[i].priority]),
                     (int)row.x + 32, (int)row.y + 30, 14, prioColors[bugs[i].priority]);
            EndScissorMode();

            if (hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) { selected = i; activeField = FIELD_NONE; }
        }

        // ----- Right panel: bug details -----
        int px = listW + 30;
        int pw = GetScreenWidth() - px - 30;

        if (selected < 0 || selected >= bugCount) {
            DrawText("Select a bug, or create a new one.", px, 40, 22, COL_DIM);
        } else {
            Bug *b = &bugs[selected];
            DrawText(TextFormat("Bug #%d", b->id), px, 20, 28, COL_TEXT);

            // Status / priority / resolve buttons. Click to cycle.
            if (Button((Rectangle){ px + pw - 480, 18, 150, 36 }, statusNames[b->status], statusColors[b->status]))
                b->status = (Status)((b->status + 1) % STATUS_COUNT);
            if (Button((Rectangle){ px + pw - 320, 18, 150, 36 }, prioNames[b->priority], prioColors[b->priority]))
                b->priority = (Priority)((b->priority + 1) % PRIO_COUNT);
            if (Button((Rectangle){ px + pw - 160, 18, 160, 36 }, "Mark Resolved", (Color){ 60, 140, 80, 255 }))
                b->status = STATUS_RESOLVED;

            TextField((Rectangle){ px, 100, pw, 38 },  "Title",              b->title,       sizeof(b->title),       FIELD_TITLE, false);
            TextField((Rectangle){ px, 190, pw, 190 }, "Description",        b->description, sizeof(b->description), FIELD_DESC,  true);
            TextField((Rectangle){ px, 430, pw, 190 }, "Steps to reproduce", b->steps,       sizeof(b->steps),       FIELD_STEPS, true);

            DrawText("Click a field to edit. Enter = newline in multi-line fields.", px, 650, 16, COL_DIM);
        }

        EndDrawing();
    }

    CloseWindow();
    return 0;
}