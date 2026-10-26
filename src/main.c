// Bug Tracker prototype - Bugzilla-style home page + detail page
// Single file, hand-rolled immediate-mode UI.
#include "raylib.h"
#include <string.h>
#include <stdio.h>
#include <ctype.h>

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
typedef enum { PAGE_HOME, PAGE_DETAIL } Page;   // which screen is showing

typedef enum { FIELD_NONE, FIELD_TITLE, FIELD_DESC, FIELD_STEPS, FIELD_SEARCH } FieldId;
static FieldId activeField = FIELD_NONE;

#define COL_BG      (Color){ 30, 32, 38, 255 }
#define COL_PANEL   (Color){ 40, 43, 51, 255 }
#define COL_ROW     (Color){ 50, 54, 64, 255 }
#define COL_ROW_SEL (Color){ 65, 90, 140, 255 }
#define COL_TEXT    (Color){ 230, 232, 238, 255 }
#define COL_DIM     (Color){ 140, 146, 160, 255 }
#define LINE_H      28

// Home page layout
#define MARGIN_X       24      // left/right page margin
#define SEARCH_Y       76      // top of the search box
#define SEARCH_H       40
#define TABLE_HEAD_Y   152     // top of the column header bar
#define TABLE_HEAD_H   34
#define TABLE_TOP      192     // y where the first row starts
#define ROW_H          44      // height of one table row
#define ROW_GAP        4       // space between rows
#define ROW_PITCH      (ROW_H + ROW_GAP)

// Column x positions, measured from the left edge of a row
#define COL_ID_X       16
#define COL_STATUS_X   100
#define COL_PRIO_X     300
#define COL_TITLE_X    460

#define SCROLL_SPEED 48.0f   // pixels moved per mouse-wheel notch

#define SCROLLBAR_W        6      // width of the scrollbar track
#define SCROLLBAR_MIN_THUMB 30.0f // thumb never gets smaller than this
#define SCROLLBAR_GRAB      6     // extra pixels either side of the thumb that still count as grabbing it

// ---------- Helpers ----------
static Font uiFont;
#define FONT_SPACING 0.5f

static void UIText(const char *s, int x, int y, int size, Color c)
{
    DrawTextEx(uiFont, s, (Vector2){ (float)x, (float)y }, (float)size, FONT_SPACING, c);
}

static int UIWidth(const char *s, int size)
{
    return (int)MeasureTextEx(uiFont, s, (float)size, FONT_SPACING).x;
}

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

// Case-insensitive substring search: does `needle` appear anywhere inside `hay`?
static bool ContainsIgnoreCase(const char *hay, const char *needle)
{
    if (needle[0] == '\0') return true;          // empty search matches everything
    for (; *hay; hay++) {                        // try starting the match at each position in hay
        const char *h = hay, *n = needle;
        while (*h && *n && tolower((unsigned char)*h) == tolower((unsigned char)*n)) { h++; n++; }
        if (*n == '\0') return true;             // we consumed the whole needle: it's a match
    }
    return false;
}

// Does this bug match the search text? Today: title or description.
// Status/priority filters can add conditions here later.
static bool BugMatches(const Bug *b, const char *query)
{
    return ContainsIgnoreCase(b->title, query) || ContainsIgnoreCase(b->description, query);
}

// Immediate-mode button: draws AND reports a click in one call.
static bool Button(Rectangle r, const char *label, Color base)
{
    bool hover = CheckCollisionPointRec(GetMousePosition(), r);
    Color c = hover ? ColorBrightness(base, 0.2f) : base;
    DrawRectangleRounded(r, 0.2f, 6, c);
    int w = UIWidth(label, 22);
    UIText(label, (int)(r.x + (r.width - w) / 2), (int)(r.y + (r.height - 22) / 2), 22, WHITE);
    return hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

// Text field. Edits `buf` in place when it is the active field.
static void TextField(Rectangle r, const char *label, char *buf, int cap, FieldId id, bool multiline)
{
    UIText(label, (int)r.x, (int)r.y - 20, 20, COL_DIM);

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
            UIText(line, (int)r.x + 8, y, 22, COL_TEXT);
            if (!nl) {   // last line: draw blinking cursor after it
                if (active && ((int)(GetTime() * 2) % 2 == 0))
                    DrawRectangle((int)r.x + 8 + UIWidth(line, 22) + 2, y, 2, 22, COL_TEXT);
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
    ChangeDirectory(GetApplicationDirectory());   // make relative paths resolve next to the executable
    InitWindow(1600, 900, "Bug Tracker");
    SetExitKey(KEY_NULL);   // so ESC doesn't close the app while you're typing
    SetTargetFPS(60);
    uiFont = LoadFontEx("fonts/Roboto-Regular.ttf", 48, NULL, 0);
    if (uiFont.texture.id == 0) uiFont = GetFontDefault();   // fallback if the file isn't found
    SetTextureFilter(uiFont.texture, TEXTURE_FILTER_BILINEAR);

    if (!LoadBugs()) {
        AddBug("Crash when saving empty file", "App segfaults if the user hits Save with no content.",
               "1. Launch app\n2. Press Ctrl+S immediately", STATUS_OPEN, PRIO_CRITICAL);
        AddBug("Button label clipped on small windows", "The 'Submit' label is cut off below 800px width.",
               "1. Resize window to 700px wide", STATUS_IN_PROGRESS, PRIO_LOW);
        AddBug("Typo on settings page", "'Prefrences' should be 'Preferences'.",
               "1. Open Settings", STATUS_RESOLVED, PRIO_MEDIUM);
    }
    selected = -1;   // the app starts on the home page with nothing opened

    Page  page = PAGE_HOME;
    float listScroll = 0.0f;         // how many pixels the table is scrolled down
    bool  scrollToSelected = false;  // set when we want the selected row brought into view
    bool  draggingThumb = false;     // true while the user is holding the scrollbar thumb
    float dragOffsetY   = 0.0f;      // where on the thumb we grabbed it (distance from thumb top to the mouse)
    char  searchQuery[64] = "";      // what the user has typed in the search box
    char  prevQuery[64]   = "";      // the query from the previous frame, to detect changes

    while (!WindowShouldClose())
    {
        // Page changes are applied at the end of the frame, so the click that
        // switched pages can't also press a button on the new page.
        Page nextPage = page;

        // Clicking empty space clears focus; fields re-set it if clicked (see TextField).
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) activeField = FIELD_NONE;

        int W = GetScreenWidth();
        int H = GetScreenHeight();

        BeginDrawing();
        ClearBackground(COL_BG);

        if (page == PAGE_HOME)
        {
            // =====================================================
            //  HOME PAGE: header, search, results table
            // =====================================================
            Rectangle listView = { 0, TABLE_TOP, (float)W, (float)(H - TABLE_TOP) };

            // Build the list of bugs to show: visible[slot] = index into bugs[].
            int visible[MAX_BUGS];
            int rowCount = 0;
            for (int k = 0; k < bugCount; k++)
                if (BugMatches(&bugs[k], searchQuery)) visible[rowCount++] = k;

            // If the search text changed, jump back to the top of the (new) list
            if (strcmp(searchQuery, prevQuery) != 0) {
                snprintf(prevQuery, sizeof(prevQuery), "%s", searchQuery);
                listScroll = 0.0f;
            }

            float contentH  = rowCount * ROW_PITCH;            // total height of all rows
            float maxScroll = contentH - listView.height;      // how far we can scroll
            if (maxScroll < 0) maxScroll = 0;                  // everything fits: no scrolling

            if (CheckCollisionPointRec(GetMousePosition(), listView))
                listScroll -= GetMouseWheelMove() * SCROLL_SPEED;

            if (scrollToSelected) {
                scrollToSelected = false;

                // Find which on-screen slot shows the selected bug (it may not be visible at all)
                int slot = -1;
                for (int k = 0; k < rowCount; k++)
                    if (visible[k] == selected) { slot = k; break; }

                if (slot >= 0) {
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
            Rectangle track = { (float)(W - SCROLLBAR_W - 6), listView.y + 4, SCROLLBAR_W, listView.height - 8 };
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

            // ----- Header: logo placeholder + New Bug button -----
            UIText("Bug Tracker", MARGIN_X, 14, 38, COL_TEXT);   // the logo goes here later
            if (Button((Rectangle){ (float)(W - MARGIN_X - 140), 16, 140, 40 }, "+ New Bug", (Color){ 60, 120, 200, 255 })) {
                if (bugCount < MAX_BUGS) {
                    AddBug("", "", "", STATUS_OPEN, PRIO_MEDIUM);
                    selected = bugCount - 1;
                    searchQuery[0] = '\0';
                    activeField = FIELD_TITLE;
                    scrollToSelected = true;    // so the new row is in view when we come back
                    nextPage = PAGE_DETAIL;
                }
            }

            // ----- Search box -----
            Rectangle searchBox = { MARGIN_X, SEARCH_Y, (float)(W - 2 * MARGIN_X), SEARCH_H };
            TextField(searchBox, "", searchQuery, sizeof(searchQuery), FIELD_SEARCH, false);
            if (searchQuery[0] == '\0' && activeField != FIELD_SEARCH)
                UIText("Search bugs by title or description...", (int)searchBox.x + 8, (int)searchBox.y + 8, 22, COL_DIM);

            // Clear button: only shown while there is text in the search box
            if (searchQuery[0] != '\0') {
                Rectangle clearBtn = { searchBox.x + searchBox.width - 36, searchBox.y + 7, 26, 26 };
                if (Button(clearBtn, "x", (Color){ 70, 74, 86, 255 })) {
                    searchQuery[0] = '\0';
                    activeField = FIELD_SEARCH;   // keep focus so the user can type a new search
                }
            }

            // ----- Result count -----
            UIText(TextFormat("Showing %d of %d", rowCount, bugCount), MARGIN_X, SEARCH_Y + SEARCH_H + 8, 18, COL_DIM);

            // ----- Column header bar -----
            Rectangle head = { MARGIN_X, TABLE_HEAD_Y, (float)(W - 2 * MARGIN_X), TABLE_HEAD_H };
            DrawRectangleRounded(head, 0.2f, 6, COL_PANEL);
            UIText("ID",       (int)head.x + COL_ID_X,     (int)head.y + 7, 18, COL_DIM);
            UIText("Status",   (int)head.x + COL_STATUS_X, (int)head.y + 7, 18, COL_DIM);
            UIText("Priority", (int)head.x + COL_PRIO_X,   (int)head.y + 7, 18, COL_DIM);
            UIText("Title",    (int)head.x + COL_TITLE_X,  (int)head.y + 7, 18, COL_DIM);

            // ----- Table rows -----
            for (int slot = 0; slot < rowCount; slot++) {
                int i = visible[slot];   // bug index for this row
                Rectangle row = { MARGIN_X, TABLE_TOP + slot * ROW_PITCH - listScroll, (float)(W - 2 * MARGIN_X), ROW_H };

                // Skip rows that are entirely outside the visible table area
                if (row.y + row.height < listView.y || row.y > listView.y + listView.height) continue;

                bool hover = CheckCollisionPointRec(GetMousePosition(), row)
                          && CheckCollisionPointRec(GetMousePosition(), listView)
                          && !draggingThumb;

                // Everything in this row is clipped to the row AND the table area
                Rectangle clip = GetCollisionRec(row, listView);
                BeginScissorMode((int)clip.x, (int)clip.y, (int)clip.width, (int)clip.height);

                DrawRectangleRounded(row, 0.15f, 6, (i == selected) ? COL_ROW_SEL : (hover ? ColorBrightness(COL_ROW, 0.1f) : COL_ROW));

                Color textCol = (bugs[i].status == STATUS_RESOLVED) ? COL_DIM : COL_TEXT;
                int ty = (int)row.y + 12;
                UIText(TextFormat("#%d", bugs[i].id), (int)row.x + COL_ID_X, ty, 20, textCol);
                DrawCircle((int)row.x + COL_STATUS_X + 8, (int)row.y + ROW_H / 2, 7, statusColors[bugs[i].status]);
                UIText(statusNames[bugs[i].status], (int)row.x + COL_STATUS_X + 24, ty, 20, textCol);
                UIText(prioNames[bugs[i].priority], (int)row.x + COL_PRIO_X, ty, 20, prioColors[bugs[i].priority]);
                UIText(bugs[i].title[0] ? bugs[i].title : "(untitled)", (int)row.x + COL_TITLE_X, ty, 20, textCol);

                EndScissorMode();

                // Clicking a row opens that bug's detail page
                if (hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                    selected = i;
                    activeField = FIELD_NONE;
                    nextPage = PAGE_DETAIL;
                }
            }

            // ----- Empty state -----
            if (rowCount == 0)
                UIText(bugCount == 0 ? "No bugs yet. Click + New Bug." : "No bugs match your search.",
                       MARGIN_X + 8, TABLE_TOP + 16, 20, COL_DIM);

            // ----- Scrollbar (only when the table overflows) -----
            if (maxScroll > 0) {
                Color thumbCol = (Color){ 110, 118, 140, 255 };
                if (draggingThumb) thumbCol = ColorBrightness(thumbCol, 0.3f);
                DrawRectangleRounded(track, 1.0f, 4, (Color){ 24, 26, 31, 255 });
                DrawRectangleRounded((Rectangle){ track.x, thumbY, track.width, thumbH }, 1.0f, 4, thumbCol);
            }
        }
        else
        {
            // =====================================================
            //  DETAIL PAGE: edit one bug
            // =====================================================
            if (selected < 0 || selected >= bugCount) {
                nextPage = PAGE_HOME;   // nothing valid to show: go back
            } else {
                int px = 40;
                int pw = W - 2 * px;
                Bug *b = &bugs[selected];

                // Back to the list (button or ESC)
                if (Button((Rectangle){ (float)px, 16, 120, 40 }, "< Back", (Color){ 70, 74, 86, 255 })
                    || IsKeyPressed(KEY_ESCAPE)) {
                    nextPage = PAGE_HOME;
                    scrollToSelected = true;   // bring this bug's row into view on the home page
                    activeField = FIELD_NONE;
                }

                UIText(TextFormat("Bug #%d", b->id), px + 140, 18, 34, COL_TEXT);

                // Status / priority / resolve buttons. Click to cycle.
                if (Button((Rectangle){ (float)(px + pw - 480), 18, 150, 36 }, statusNames[b->status], statusColors[b->status]))
                    b->status = (Status)((b->status + 1) % STATUS_COUNT);
                if (Button((Rectangle){ (float)(px + pw - 320), 18, 150, 36 }, prioNames[b->priority], prioColors[b->priority]))
                    b->priority = (Priority)((b->priority + 1) % PRIO_COUNT);
                if (Button((Rectangle){ (float)(px + pw - 160), 18, 160, 36 }, "Mark Resolved", (Color){ 60, 140, 80, 255 }))
                    b->status = STATUS_RESOLVED;

                // Layout computed from the window height so it fills any size
                int titleY  = 100, titleH = 44;
                int labelGap = 50;                                   // room for the label above each box
                int bottom  = H - 70;                                // leave space for the hint line
                int areaH   = (bottom - (titleY + titleH) - 2 * labelGap) / 2;
                int descY   = titleY + titleH + labelGap;
                int stepsY  = descY + areaH + labelGap;

                TextField((Rectangle){ (float)px, (float)titleY, (float)pw, (float)titleH }, "Title",              b->title,       sizeof(b->title),       FIELD_TITLE, false);
                TextField((Rectangle){ (float)px, (float)descY,  (float)pw, (float)areaH  }, "Description",        b->description, sizeof(b->description), FIELD_DESC,  true);
                TextField((Rectangle){ (float)px, (float)stepsY, (float)pw, (float)areaH  }, "Steps to reproduce", b->steps,       sizeof(b->steps),       FIELD_STEPS, true);

                UIText("Click a field to edit. Enter = newline in multi-line fields. ESC = back to list.", px, H - 40, 20, COL_DIM);
            }
        }

        EndDrawing();
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) SaveBugs();   // covers button clicks and leaving a field

        page = nextPage;
    }
    SaveBugs();
    UnloadFont(uiFont);
    CloseWindow();
    return 0;
}
