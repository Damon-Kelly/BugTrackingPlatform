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

// List panel layout
#define LIST_W      340                  // width of the left panel
#define LIST_TOP    64                   // y where the first row starts (below the header)
#define LIST_MARGIN 8                    // gap between panel edge and rows
#define ROW_H       52                   // height of one row
#define ROW_GAP     6                    // space between rows
#define ROW_PITCH   (ROW_H + ROW_GAP)    // distance from one row's top to the next (58)

#define SCROLL_SPEED 40.0f   // pixels moved per mouse-wheel notch

#define SCROLLBAR_W        6      // width of the scrollbar track
#define SCROLLBAR_MIN_THUMB 30.0f // thumb never gets smaller than this
#define SCROLLBAR_GRAB      6     // extra pixels either side of the thumb that still count as grabbing it

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

#define SAVE_FILE  "bugs.dat"
#define SAVE_MAGIC 0x31475542   // marks the file as ours, and which format version

static void SaveBugs(void)
{
    FILE *f = fopen(SAVE_FILE, "wb");
    if (!f) return;
    int magic = SAVE_MAGIC;
    fwrite(&magic,    sizeof(int), 1, f);
    fwrite(&bugCount, sizeof(int), 1, f);
    fwrite(&nextId,   sizeof(int), 1, f);
    fwrite(bugs, sizeof(Bug), bugCount, f);
    fclose(f);
}

static bool LoadBugs(void)
{
    FILE *f = fopen(SAVE_FILE, "rb");
    if (!f) return false;

    int magic = 0, count = 0, id = 0;
    bool ok = fread(&magic, sizeof(int), 1, f) == 1 && magic == SAVE_MAGIC
           && fread(&count, sizeof(int), 1, f) == 1 && count >= 0 && count <= MAX_BUGS
           && fread(&id,    sizeof(int), 1, f) == 1
           && fread(bugs, sizeof(Bug), count, f) == (size_t)count;
    fclose(f);

    if (!ok) { bugCount = 0; nextId = 1; return false; }
    bugCount = count;
    nextId = id;
    return true;
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

        if (!LoadBugs()) {
        AddBug("Crash when saving empty file", "App segfaults if the user hits Save with no content.",
               "1. Launch app\n2. Press Ctrl+S immediately", STATUS_OPEN, PRIO_CRITICAL);
        AddBug("Button label clipped on small windows", "The 'Submit' label is cut off below 800px width.",
               "1. Resize window to 700px wide", STATUS_IN_PROGRESS, PRIO_LOW);
        AddBug("Typo on settings page", "'Prefrences' should be 'Preferences'.",
               "1. Open Settings", STATUS_RESOLVED, PRIO_MEDIUM);
            }
        selected = (bugCount > 0) ? 0 : -1;
    float listScroll = 0.0f;   // how many pixels the list is scrolled down
    bool scrollToSelected = false;   // set when we want the selected row brought into view
    bool  draggingThumb = false;   // true while the user is holding the scrollbar thumb
    float dragOffsetY   = 0.0f;    // where on the thumb we grabbed it (distance from thumb top to the mouse)

    while (!WindowShouldClose())
    {
        // Clicking empty space clears focus; fields re-set it if clicked (see TextField).
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) activeField = FIELD_NONE;

        // The visible window onto the list. Rows outside this rectangle are scrolled out of view.
        Rectangle listView = { 0, LIST_TOP, LIST_W, GetScreenHeight() - LIST_TOP };

        // How many rows the list is showing. Today that's every bug;
        // search will later set this to the number of matches instead.
        int rowCount = bugCount;

        float contentH  = rowCount * ROW_PITCH;            // total height of all rows
        float maxScroll = contentH - listView.height;      // how far we can scroll
        if (maxScroll < 0) maxScroll = 0;                  // everything fits: no scrolling

        if (CheckCollisionPointRec(GetMousePosition(), listView))
            listScroll -= GetMouseWheelMove() * SCROLL_SPEED;

        if (scrollToSelected) {
            scrollToSelected = false;
            if (selected >= 0 && selected < rowCount) {
                int slot = selected;   // today slot == bug index; search will change this later
                float rowTop    = slot * ROW_PITCH;
                float rowBottom = rowTop + ROW_H;
                if (rowTop < listScroll)
                    listScroll = rowTop;
                else if (rowBottom > listScroll + listView.height)
                    listScroll = rowBottom - listView.height;
            }
        }

        if (listScroll < 0) listScroll = 0;
        if (listScroll > maxScroll) listScroll = maxScroll;

        // ---- Scrollbar geometry and thumb dragging ----
        Rectangle track = { LIST_W - SCROLLBAR_W - 1, listView.y + 4, SCROLLBAR_W, listView.height - 8 };
        float thumbH = 0.0f;
        float thumbY = track.y;

        if (maxScroll > 0) {
            thumbH = track.height * (listView.height / contentH);
            if (thumbH < SCROLLBAR_MIN_THUMB) thumbH = SCROLLBAR_MIN_THUMB;
            float travel = track.height - thumbH;                 // how far the thumb can slide
            thumbY = track.y + travel * (listScroll / maxScroll);

            Vector2 m = GetMousePosition();
            Rectangle grab = { track.x - SCROLLBAR_GRAB, thumbY, track.width + 2 * SCROLLBAR_GRAB, thumbH };

            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(m, grab)) {
                draggingThumb = true;
                dragOffsetY = m.y - thumbY;                       // remember where we grabbed
            }
            if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) draggingThumb = false;

            if (draggingThumb && travel > 0) {
                float newThumbY = m.y - dragOffsetY;              // where the thumb should be now
                listScroll = (newThumbY - track.y) / travel * maxScroll;   // reverse of the thumbY formula
                if (listScroll < 0) listScroll = 0;
                if (listScroll > maxScroll) listScroll = maxScroll;
                thumbY = track.y + travel * (listScroll / maxScroll);      // recompute so we draw the new position
            }
        } else {
            draggingThumb = false;
        }

        BeginDrawing();
        ClearBackground(COL_BG);

        // ----- Left panel: bug list -----
        DrawRectangle(0, 0, LIST_W, GetScreenHeight(), COL_PANEL);
        DrawText("Bugs", 16, 16, 28, COL_TEXT);
        if (Button((Rectangle){ LIST_W - 120, 14, 104, 34 }, "+ New Bug", (Color){ 60, 120, 200, 255 })) {
            AddBug("", "", "", STATUS_OPEN, PRIO_MEDIUM);
            selected = bugCount - 1;
            activeField = FIELD_TITLE;
            scrollToSelected = true;
        }

        for (int slot = 0; slot < rowCount; slot++) {
        int i = slot;   // bug index. Search will later map slot -> matching bug index here
            Rectangle row = { LIST_MARGIN, LIST_TOP + slot * ROW_PITCH - listScroll, LIST_W - 2 * LIST_MARGIN, ROW_H };

            // Skip rows that are entirely outside the visible list area
            if (row.y + row.height < listView.y || row.y > listView.y + listView.height) continue;

        bool hover = CheckCollisionPointRec(GetMousePosition(), row)
                      && CheckCollisionPointRec(GetMousePosition(), listView)
                      && !draggingThumb;

            // Row background and status dot, clipped to the list area
            BeginScissorMode((int)listView.x, (int)listView.y, (int)listView.width, (int)listView.height);
            DrawRectangleRounded(row, 0.15f, 6, (i == selected) ? COL_ROW_SEL : (hover ? ColorBrightness(COL_ROW, 0.1f) : COL_ROW));
            DrawCircle((int)row.x + 16, (int)row.y + 26, 7, statusColors[bugs[i].status]);
            EndScissorMode();

            // Text, clipped to the row's text area AND the list area
            Rectangle textArea = { row.x + 30, row.y, row.width - 38, row.height };
            Rectangle textClip = GetCollisionRec(textArea, listView);
            BeginScissorMode((int)textClip.x, (int)textClip.y, (int)textClip.width, (int)textClip.height);
            const char *t = bugs[i].title[0] ? bugs[i].title : "(untitled)";
            DrawText(t, (int)row.x + 32, (int)row.y + 8, 18,
                     bugs[i].status == STATUS_RESOLVED ? COL_DIM : COL_TEXT);
            DrawText(TextFormat("#%d  %s", bugs[i].id, prioNames[bugs[i].priority]),
                     (int)row.x + 32, (int)row.y + 30, 14, prioColors[bugs[i].priority]);
            EndScissorMode();

            if (hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) { selected = i; activeField = FIELD_NONE; }
        }

        // ----- Scrollbar (only when the list overflows) -----
        if (maxScroll > 0) {
            Color thumbCol = (Color){ 110, 118, 140, 255 };
            if (draggingThumb) thumbCol = ColorBrightness(thumbCol, 0.3f);
            DrawRectangleRounded(track, 1.0f, 4, (Color){ 24, 26, 31, 255 });
            DrawRectangleRounded((Rectangle){ track.x, thumbY, track.width, thumbH }, 1.0f, 4, thumbCol);
        }

        // ----- Right panel: bug details -----
        int px = LIST_W + 30;
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
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) SaveBugs();   // covers button clicks and leaving a field

    }
    SaveBugs();
    CloseWindow();
    return 0;
}