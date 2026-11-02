// Bug Tracker prototype - welcome page, accounts, Bugzilla-style home + detail pages
// Single file, hand-rolled immediate-mode UI.
// NOTE: accounts are stored in PLAIN TEXT (users.txt). Prototype only, not secure.
#include "raylib.h"
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <time.h>

// ---------- Data model ----------
#define MAX_BUGS 128
#define NAME_LEN 48   // max length of an account name / reporter name (47 characters + terminator)

typedef enum { STATUS_OPEN, STATUS_IN_PROGRESS, STATUS_RESOLVED, STATUS_COUNT } Status;
typedef enum { PRIO_LOW, PRIO_MEDIUM, PRIO_HIGH, PRIO_CRITICAL, PRIO_COUNT } Priority;
// Why a bug was resolved (RES_NONE while it is still open)
typedef enum { RES_NONE, RES_FIXED, RES_WONT_FIX, RES_DUPLICATE, RES_INVALID, RES_WORKS_FOR_ME, RES_COUNT } Resolution;

// One entry in a bug's activity log: comments now, automatic change records later
#define MAX_ACTIVITY  32
#define ACTIVITY_TEXT 256
typedef enum { ACT_CREATED, ACT_COMMENT, ACT_STATUS, ACT_PRIORITY, ACT_ASSIGNED, ACT_RESOLUTION } ActivityType;

typedef struct {
    ActivityType type;
    char         user[NAME_LEN];        // who did it
    time_t       time;                  // when
    char         text[ACTIVITY_TEXT];   // the comment, or a description of the change
} Activity;

// The layout of save files from before the "reporter" field existed (only used to load them)
typedef struct {
    int      id;
    char     title[64];
    char     description[512];
    char     steps[512];
    Status   status;
    Priority priority;
} BugV1;

// The layout of save files from the "reporter" version (only used to load them)
typedef struct {
    int      id;
    char     title[64];
    char     description[512];
    char     steps[512];
    Status   status;
    Priority priority;
    char     reporter[NAME_LEN];
} BugV2;

typedef struct {
    int      id;
    char     title[64];
    char     description[512];
    char     steps[512];
    Status   status;
    Priority priority;
    char     reporter[NAME_LEN];   // name of the account that created the bug ("" = unknown)

    // ---- added in save format version 3 ----
    time_t     created;                // 0 = unknown (bug came from an older save file)
    time_t     updated;
    char       assignee[NAME_LEN];     // account name, "" = unassigned
    Resolution resolution;             // RES_NONE until the bug is resolved
    int        activityCount;          // how many entries of activity[] are in use
    Activity   activity[MAX_ACTIVITY];
} Bug;

static Bug bugs[MAX_BUGS];
static int bugCount = 0;
static int nextId   = 1;
static int selected = -1;   // index into bugs[], -1 = nothing selected

static const char *statusNames[STATUS_COUNT] = { "Open", "In Progress", "Resolved" };
static const char *prioNames[PRIO_COUNT]     = { "Low", "Medium", "High", "Critical" };
static Color statusColors[STATUS_COUNT]      = { { 220, 80, 80, 255 }, { 230, 170, 40, 255 }, { 80, 180, 100, 255 } };
static Color prioColors[PRIO_COUNT]          = { { 130, 140, 150, 255 }, { 70, 130, 200, 255 }, { 230, 140, 40, 255 }, { 200, 50, 50, 255 } };
static const char *resolutionNames[RES_COUNT] = { "None", "Fixed", "Won't Fix", "Duplicate", "Invalid", "Works For Me" };

// ---------- Accounts (plain text, prototype only) ----------
#define MAX_USERS  32
#define PASS_LEN   32            // password (max 31 characters)
#define USERS_FILE "users.txt"   // one account per line: name<TAB>password

// 1 = guests can browse and read, but must sign in to create or edit bugs
// 0 = everyone can edit (the accounts are then just a login screen)
#define REQUIRE_LOGIN_TO_EDIT 1

typedef struct {
    char name[NAME_LEN];
    char password[PASS_LEN];
} Account;

static Account accounts[MAX_USERS];
static int accountCount = 0;
static int currentUser  = -1;    // index into accounts[], -1 = guest

// ---------- UI state ----------
typedef enum { PAGE_WELCOME, PAGE_AUTH, PAGE_HOME, PAGE_DETAIL } Page;   // which screen is showing

typedef enum { FIELD_NONE, FIELD_TITLE, FIELD_DESC, FIELD_STEPS, FIELD_SEARCH,
               FIELD_AUTH_NAME, FIELD_AUTH_PASS, FIELD_AUTH_PASS2, FIELD_COUNT } FieldId;
static FieldId activeField = FIELD_NONE;

// Table sorting: which column the list is sorted by, and in which direction
typedef enum { SORT_ID, SORT_STATUS, SORT_PRIORITY, SORT_REPORTER, SORT_ASSIGNEE, SORT_CREATED, SORT_UPDATED, SORT_TITLE, SORT_COUNT } SortCol;
static SortCol sortCol = SORT_ID;
static bool    sortAsc = true;

// Options for TextField (combine with |)
#define TF_MULTILINE 1   // Enter inserts a newline
#define TF_READONLY  2   // can't be focused or edited
#define TF_MASKED    4   // draw * instead of the characters (passwords)

// Sign in / create account form state
static bool createMode = false;          // false = sign in, true = create account
static char authName[NAME_LEN]  = "";
static char authPass[PASS_LEN]  = "";
static char authPass2[PASS_LEN] = "";    // "confirm password" (create mode only)
static char authMsg[96]         = "";    // error message shown under the form

#define COL_BG        (Color){ 30, 32, 38, 255 }
#define COL_PANEL     (Color){ 40, 43, 51, 255 }
#define COL_ROW       (Color){ 50, 54, 64, 255 }
#define COL_ROW_SEL   (Color){ 65, 90, 140, 255 }
#define COL_TEXT      (Color){ 230, 232, 238, 255 }
#define COL_DIM       (Color){ 140, 146, 160, 255 }
#define COL_ERROR     (Color){ 230, 90, 90, 255 }
#define COL_BTN_BLUE  (Color){ 60, 120, 200, 255 }
#define COL_BTN_GREEN (Color){ 60, 140, 80, 255 }
#define COL_BTN_GREY  (Color){ 70, 74, 86, 255 }
#define LINE_H        28

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

// Status filter chips: right-aligned on the "Showing N of M" line
#define CHIP_Y         120     // top of the chip row (the search box ends at y=116)
#define CHIP_W         120
#define CHIP_H         28      // the row ends at y=148, just above the column header bar at y=152
#define CHIP_GAP       8

// Column x positions, measured from the left edge of a row
#define COL_ID_X       16
#define COL_STATUS_X   80
#define COL_PRIO_X     220
#define COL_REPORTER_X 320
#define COL_ASSIGNEE_X 480
#define COL_CREATED_X  640
#define COL_UPDATED_X  760
#define COL_TITLE_X    880

static const int   colX[SORT_COUNT]     = { COL_ID_X, COL_STATUS_X, COL_PRIO_X, COL_REPORTER_X, COL_ASSIGNEE_X, COL_CREATED_X, COL_UPDATED_X, COL_TITLE_X };
static const char *colNames[SORT_COUNT] = { "ID", "Status", "Priority", "Reporter", "Assignee", "Created", "Updated", "Title" };

#define SCROLL_SPEED 48.0f   // pixels moved per mouse-wheel notch

#define SCROLLBAR_W        6      // width of the scrollbar track
#define SCROLLBAR_MIN_THUMB 30.0f // thumb never gets smaller than this
#define SCROLLBAR_GRAB      6     // extra pixels either side of the thumb that still count as grabbing it

// ---------- Text helpers ----------
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

// Draw text horizontally centered on x = cx
static void UITextCentered(const char *s, int cx, int y, int size, Color c)
{
    UIText(s, cx - UIWidth(s, size) / 2, y, size, c);
}

// Write a time as "2026-10-30 14:05" into `out`. A time of 0 means "unknown".
static void FormatTime(time_t t, char *out, int cap)
{
    struct tm *tmv = (t != 0) ? localtime(&t) : NULL;
    if (!tmv) { snprintf(out, cap, "Unknown"); return; }
    strftime(out, cap, "%Y-%m-%d %H:%M", tmv);
}

// Write a time as "2026-10-30" for the table. A time of 0 (unknown) shows as "-".
// Plain hyphen on purpose: the font only has ASCII characters.
static void FormatDate(time_t t, char *out, int cap)
{
    struct tm *tmv = (t != 0) ? localtime(&t) : NULL;
    if (!tmv) { snprintf(out, cap, "-"); return; }
    strftime(out, cap, "%Y-%m-%d", tmv);
}

// Record something that happened to a bug, and mark the bug as updated.
// If the log is full, the oldest entry is dropped to make room.
static void LogActivity(Bug *b, ActivityType type, const char *user, const char *text)
{
    if (b->activityCount >= MAX_ACTIVITY) {
        memmove(&b->activity[0], &b->activity[1], sizeof(Activity) * (MAX_ACTIVITY - 1));
        b->activityCount = MAX_ACTIVITY - 1;
    }
    Activity *a = &b->activity[b->activityCount++];
    memset(a, 0, sizeof(Activity));
    a->type = type;
    a->time = time(NULL);
    snprintf(a->user, NAME_LEN, "%s", user);
    snprintf(a->text, ACTIVITY_TEXT, "%s", text);
    b->updated = a->time;
}

static void AddBug(const char *title, const char *desc, const char *steps, Status s, Priority p, const char *reporter)
{
    if (bugCount >= MAX_BUGS) return;
    Bug *b = &bugs[bugCount];
    memset(b, 0, sizeof(Bug));
    b->id = nextId++;
    strncpy(b->title, title, sizeof(b->title) - 1);
    strncpy(b->description, desc, sizeof(b->description) - 1);
    strncpy(b->steps, steps, sizeof(b->steps) - 1);
    strncpy(b->reporter, reporter, sizeof(b->reporter) - 1);
    b->status = s;
    b->priority = p;
    b->created = time(NULL);
    LogActivity(b, ACT_CREATED, b->reporter[0] ? b->reporter : "Unknown", "Bug created");
    bugCount++;
}

#define SAVE_FILE  "bugs.dat"
#define SAVE_MAGIC    0x33475542   // marks the file as ours: format version 3 (dates, assignee, resolution, activity log)
#define SAVE_MAGIC_V2 0x32475542   // format version 2 (has "reporter"): still loadable
#define SAVE_MAGIC_V1 0x31475542   // format version 1 (no "reporter"): still loadable

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
    bool ok = fread(&magic, sizeof(int), 1, f) == 1
           && (magic == SAVE_MAGIC || magic == SAVE_MAGIC_V2 || magic == SAVE_MAGIC_V1)
           && fread(&count, sizeof(int), 1, f) == 1 && count >= 0 && count <= MAX_BUGS
           && fread(&id,    sizeof(int), 1, f) == 1;

    if (ok && magic == SAVE_MAGIC) {
        ok = fread(bugs, sizeof(Bug), count, f) == (size_t)count;
    } else if (ok && magic == SAVE_MAGIC_V2) {
        // Version 2 file: read it in the old layout, then copy each bug into the new one.
        // The new fields come AFTER the old ones, so the old bytes fit at the start of a Bug;
        // the rest stays zero (unknown dates, no assignee, no resolution, empty log).
        static BugV2 old[MAX_BUGS];
        ok = fread(old, sizeof(BugV2), count, f) == (size_t)count;
        for (int k = 0; ok && k < count; k++) {
            memset(&bugs[k], 0, sizeof(Bug));
            memcpy(&bugs[k], &old[k], sizeof(BugV2));
        }
    } else if (ok) {
        // Version 1 file (no reporter): same idea, field by field
        static BugV1 old[MAX_BUGS];
        ok = fread(old, sizeof(BugV1), count, f) == (size_t)count;
        for (int k = 0; ok && k < count; k++) {
            memset(&bugs[k], 0, sizeof(Bug));
            bugs[k].id = old[k].id;
            memcpy(bugs[k].title,       old[k].title,       sizeof(old[k].title));
            memcpy(bugs[k].description, old[k].description, sizeof(old[k].description));
            memcpy(bugs[k].steps,       old[k].steps,       sizeof(old[k].steps));
            bugs[k].status   = old[k].status;
            bugs[k].priority = old[k].priority;
        }
    }
    fclose(f);

    if (!ok) { bugCount = 0; nextId = 1; return false; }
    bugCount = count;
    nextId = id;
    return true;
}

// ---------- Account storage and sign-in logic ----------
static bool EqualsIgnoreCase(const char *a, const char *b)
{
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
        a++; b++;
    }
    return *a == *b;   // both strings must end at the same time
}

// Remove leading and trailing spaces
static void TrimInPlace(char *s)
{
    int len = (int)strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1])) s[--len] = '\0';
    int start = 0;
    while (s[start] && isspace((unsigned char)s[start])) start++;
    if (start > 0) memmove(s, s + start, strlen(s + start) + 1);
}

static void SaveAccounts(void)
{
    FILE *f = fopen(USERS_FILE, "w");
    if (!f) return;
    for (int i = 0; i < accountCount; i++)
        fprintf(f, "%s\t%s\n", accounts[i].name, accounts[i].password);   // plain text, on purpose
    fclose(f);
}

static void LoadAccounts(void)
{
    FILE *f = fopen(USERS_FILE, "r");
    if (!f) return;
    char line[256];
    while (accountCount < MAX_USERS && fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';
        char *tab = strchr(line, '\t');
        if (!tab || tab == line) continue;          // malformed line or empty name: skip it
        *tab = '\0';
        snprintf(accounts[accountCount].name,     NAME_LEN, "%s", line);
        snprintf(accounts[accountCount].password, PASS_LEN, "%s", tab + 1);
        accountCount++;
    }
    fclose(f);
}

// Index of the account with this name (case-insensitive), or -1
static int FindAccount(const char *name)
{
    for (int i = 0; i < accountCount; i++)
        if (EqualsIgnoreCase(accounts[i].name, name)) return i;
    return -1;
}

// Prepare the sign-in / create-account form for showing
static void OpenAuth(bool create)
{
    createMode = create;
    authName[0] = '\0';
    authPass[0] = '\0';
    authPass2[0] = '\0';
    authMsg[0] = '\0';
    activeField = FIELD_AUTH_NAME;
}

// Try to sign in (or create the account, in create mode).
// Returns true when the user is now signed in; otherwise authMsg explains why not.
static bool SubmitAuth(void)
{
    TrimInPlace(authName);
    if (authName[0] == '\0') { snprintf(authMsg, sizeof(authMsg), "Enter an email or username."); return false; }
    if (authPass[0] == '\0') { snprintf(authMsg, sizeof(authMsg), "Enter a password.");           return false; }

    int idx = FindAccount(authName);

    if (createMode) {
        if (strcmp(authPass, authPass2) != 0) { snprintf(authMsg, sizeof(authMsg), "The two passwords don't match."); return false; }
        if (idx >= 0)                         { snprintf(authMsg, sizeof(authMsg), "That name is already taken.");    return false; }
        if (accountCount >= MAX_USERS)        { snprintf(authMsg, sizeof(authMsg), "Account limit reached.");         return false; }

        snprintf(accounts[accountCount].name,     NAME_LEN, "%s", authName);
        snprintf(accounts[accountCount].password, PASS_LEN, "%s", authPass);
        currentUser = accountCount++;     // creating an account also signs you in
        SaveAccounts();
        return true;
    }

    if (idx < 0 || strcmp(accounts[idx].password, authPass) != 0) {
        snprintf(authMsg, sizeof(authMsg), "Wrong name or password.");
        return false;
    }
    currentUser = idx;
    return true;
}

// ---------- Changing a bug (each change is logged) ----------
// Who to record in the activity log for the change being made right now
static const char *CurrentUserName(void)
{
    return (currentUser >= 0) ? accounts[currentUser].name : "Guest";
}

static void SetStatus(Bug *b, Status s)
{
    if (b->status == s) return;                       // nothing changed, nothing to log
    char msg[ACTIVITY_TEXT];
    snprintf(msg, sizeof(msg), "Status: %s -> %s", statusNames[b->status], statusNames[s]);
    b->status = s;
    if (s != STATUS_RESOLVED) b->resolution = RES_NONE;   // a bug that is not resolved has no resolution
    LogActivity(b, ACT_STATUS, CurrentUserName(), msg);
}

static void SetPriority(Bug *b, Priority p)
{
    if (b->priority == p) return;
    char msg[ACTIVITY_TEXT];
    snprintf(msg, sizeof(msg), "Priority: %s -> %s", prioNames[b->priority], prioNames[p]);
    b->priority = p;
    LogActivity(b, ACT_PRIORITY, CurrentUserName(), msg);
}

// name = "" means unassigned
static void SetAssignee(Bug *b, const char *name)
{
    if (strcmp(b->assignee, name) == 0) return;
    char msg[ACTIVITY_TEXT];
    snprintf(msg, sizeof(msg), "Assignee: %s -> %s",
             b->assignee[0] ? b->assignee : "nobody", name[0] ? name : "nobody");   // build the message BEFORE overwriting
    snprintf(b->assignee, NAME_LEN, "%s", name);
    LogActivity(b, ACT_ASSIGNED, CurrentUserName(), msg);
}

// Click-to-cycle through: unassigned, each account in turn, back to unassigned
static void CycleAssignee(Bug *b)
{
    int idx  = b->assignee[0] ? FindAccount(b->assignee) : -1;   // -1 = unassigned (or an account that no longer exists)
    int next = idx + 1;
    SetAssignee(b, (next < accountCount) ? accounts[next].name : "");
}

// ---------- Search ----------
// What to show as the reporter of a bug
static const char *ReporterName(const Bug *b)
{
    return b->reporter[0] ? b->reporter : "Unknown";
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

// Should this bug appear in the list? It must pass BOTH tests:
//   1. its status chip is switched on, and
//   2. the search text appears in its title, description or reporter.
static bool BugMatches(const Bug *b, const char *query, const bool *statusOn)
{
    if (!statusOn[b->status]) return false;   // status chip is off: hide it straight away
    return ContainsIgnoreCase(b->title, query) || ContainsIgnoreCase(b->description, query)
        || ContainsIgnoreCase(ReporterName(b), query) || ContainsIgnoreCase(b->assignee, query);
}

// ---------- Sorting ----------
static int CompareIgnoreCase(const char *a, const char *b)
{
    while (*a && *b) {
        int d = tolower((unsigned char)*a) - tolower((unsigned char)*b);
        if (d != 0) return d;
        a++; b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

// qsort comparator for an array of bug INDICES (ints), ordered by sortCol / sortAsc
static int CompareBugs(const void *pa, const void *pb)
{
    const Bug *a = &bugs[*(const int *)pa];
    const Bug *b = &bugs[*(const int *)pb];
    int c = 0;
    switch (sortCol) {
        case SORT_ID:       c = a->id - b->id; break;
        case SORT_STATUS:   c = (int)a->status   - (int)b->status;   break;
        case SORT_PRIORITY: c = (int)a->priority - (int)b->priority; break;
        case SORT_REPORTER: c = CompareIgnoreCase(ReporterName(a), ReporterName(b)); break;
        case SORT_ASSIGNEE: c = CompareIgnoreCase(a->assignee, b->assignee); break;   // unassigned ("") sorts first
        case SORT_CREATED:  c = (a->created > b->created) - (a->created < b->created); break;
        case SORT_UPDATED:  c = (a->updated > b->updated) - (a->updated < b->updated); break;
        case SORT_TITLE:    c = CompareIgnoreCase(a->title, b->title); break;
        default: break;
    }
    if (!sortAsc) c = -c;
    if (c == 0) c = a->id - b->id;   // equal values: fall back to ID so the order is predictable
    return c;
}

// Copy `s` into `out`, shortened with "..." so it is at most maxW pixels wide
static const char *FitText(const char *s, int maxW, int size, char *out, int cap)
{
    snprintf(out, cap, "%s", s);
    if (UIWidth(out, size) <= maxW) return out;
    int len = (int)strlen(out);
    while (len > 0) {
        len--;
        snprintf(out, cap, "%.*s...", len, s);
        if (UIWidth(out, size) <= maxW) break;
    }
    return out;
}

// Small sort arrow with its top at y, centered on cx. Both winding orders are
// drawn so it shows up whichever way the triangle faces.
static void DrawSortArrow(int cx, int y, bool up, Color c)
{
    Vector2 a, b, d;
    if (up) { a = (Vector2){ (float)cx, (float)y };       b = (Vector2){ (float)(cx - 5), (float)(y + 8) }; d = (Vector2){ (float)(cx + 5), (float)(y + 8) }; }
    else    { a = (Vector2){ (float)(cx - 5), (float)y }; b = (Vector2){ (float)cx, (float)(y + 8) };       d = (Vector2){ (float)(cx + 5), (float)y }; }
    DrawTriangle(a, b, d, c);
    DrawTriangle(a, d, b, c);
}

// ---------- Widgets ----------
// A colored label that doesn't react to the mouse.
static void Badge(Rectangle r, const char *label, Color c)
{
    DrawRectangleRounded(r, 0.2f, 6, c);
    int w = UIWidth(label, 22);
    UIText(label, (int)(r.x + (r.width - w) / 2), (int)(r.y + (r.height - 22) / 2), 22, WHITE);
}

// Immediate-mode button: draws AND reports a click in one call.
static bool Button(Rectangle r, const char *label, Color base)
{
    bool hover = CheckCollisionPointRec(GetMousePosition(), r);
    Badge(r, label, hover ? ColorBrightness(base, 0.2f) : base);
    return hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

// Toggle chip: filled with its color when ON, dark and dim when OFF.
// Like Button, it draws AND returns true on the frame it is clicked. The caller flips the bool.
static bool Chip(Rectangle r, const char *label, bool on, Color onColor)
{
    bool hover = CheckCollisionPointRec(GetMousePosition(), r);

    Color fill = on ? onColor : (Color){ 24, 26, 31, 255 };
    if (hover) fill = ColorBrightness(fill, 0.15f);
    DrawRectangleRounded(r, 0.4f, 6, fill);
    if (!on) DrawRectangleRoundedLinesEx(r, 0.4f, 6, 1.0f, (Color){ 70, 74, 86, 255 });

    int w = UIWidth(label, 18);
    UIText(label, (int)(r.x + (r.width - w) / 2), (int)(r.y + (r.height - 18) / 2), 18, on ? WHITE : COL_DIM);

    return hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

// ---------- Text field internals ----------
// A focused text field has a caret (the cursor). Only one field has focus at a time, so the caret
// is shared. Multi-line fields also remember how far they are scrolled (one value per field).
#define TF_FONT      22                  // text size inside fields
#define TF_PAD       8                   // space between a field's border and its text
#define TF_BAR_W     14                  // strip kept free on the right of multi-line fields for the scrollbar
#define TF_MAX_TEXT  512                 // longest text any field holds (description, steps)
#define TF_MAX_LINES (TF_MAX_TEXT + 1)   // worst case: every character is a newline

typedef struct { int start, len; } TextLine;   // one line as drawn: a slice of the text

typedef struct {
    char     shown[TF_MAX_TEXT + 1];    // the text as drawn (stars for passwords)
    float    step[TF_MAX_TEXT + 1];     // how far the pen moves right after each character
    TextLine lines[TF_MAX_LINES];       // the text split into drawn lines
    int      count;                     // number of lines (at least 1)
    int      len;                       // length of the text
} TextLayout;

static int         caret         = 0;             // caret position in the focused field (0 = before the first character)
static FieldId     caretField    = FIELD_NONE;    // which field `caret` belongs to
static double      caretStamp    = 0.0;           // time of the last caret move or edit (the caret blinks from then)
static float       fieldScroll[FIELD_COUNT];      // pixels each multi-line field is scrolled down
static const char *scrollOwner[FIELD_COUNT];      // which text each field's scroll belongs to
static FieldId     barDragField  = FIELD_NONE;    // field whose scrollbar thumb is being dragged
static float       barDragOffset = 0.0f;          // where on the thumb it was grabbed

// True on the frame a key goes down, and again while it is held (auto-repeat)
static bool KeyRepeats(int key)
{
    return IsKeyPressed(key) || IsKeyPressedRepeat(key);
}

// Split `s` (n characters) into lines. A line ends at '\n' or, when wrapMax > 0, where the next word
// would no longer fit in wrapMax pixels (a word longer than a whole line is cut). Returns the line count.
static int LayoutLines(const char *s, int n, const float *step, float wrapMax, TextLine *lines)
{
    int count = 0;
    int start = 0;
    for (;;) {
        int end = start;                                   // this paragraph is s[start..end)
        while (end < n && s[end] != '\n') end++;

        int a = start;
        do {
            int b = end;
            if (wrapMax > 0) {
                float w = 0;
                int lastSpace = -1;
                b = a;
                while (b < end && (b == a || w + step[b] <= wrapMax)) {   // always take at least one character
                    if (s[b] == ' ') lastSpace = b;
                    w += step[b];
                    b++;
                }
                if (b < end) {                                  // the rest did not fit: choose where to break
                    if (s[b] == ' ') b++;                       // a space may hang over the edge
                    else if (lastSpace >= 0) b = lastSpace + 1; // otherwise break after the last space
                }
            }
            if (count < TF_MAX_LINES) { lines[count].start = a; lines[count].len = b - a; count++; }
            a = b;
        } while (a < end);

        if (end >= n) break;
        start = end + 1;                                   // skip the '\n'
    }
    return count;
}

// Measure and split a field's text. wrapMax = 0 means "never wrap".
static void BuildLayout(TextLayout *L, const char *buf, bool masked, float wrapMax)
{
    int n = (int)strlen(buf);
    if (n > TF_MAX_TEXT) n = TF_MAX_TEXT;
    for (int i = 0; i < n; i++) {
        char c = (masked && buf[i] != '\n') ? '*' : buf[i];
        char one[2] = { c, '\0' };
        L->shown[i] = c;
        // DrawTextEx moves the pen by (glyph advance + spacing) per character, so this matches what is drawn
        L->step[i] = (c == '\n') ? 0.0f : MeasureTextEx(uiFont, one, (float)TF_FONT, 0).x + FONT_SPACING;
    }
    L->shown[n] = '\0';
    L->len = n;
    L->count = LayoutLines(L->shown, n, L->step, wrapMax, L->lines);
}

// Which line a caret position is shown on. The position where one wrapped line ends and the next begins
// counts as the start of the next line.
static int LineOfCaret(const TextLayout *L, int pos)
{
    int li = 0;
    for (int i = 0; i < L->count; i++)
        if (L->lines[i].start <= pos) li = i;
    return li;
}

// Horizontal distance from the start of line `li` to caret position `pos`
static float CaretX(const TextLayout *L, int li, int pos)
{
    float x = 0;
    for (int k = L->lines[li].start; k < pos && k < L->len; k++) x += L->step[k];
    return x;
}

// Last caret position that belongs to line `li`. A wrapped line stops one short, because
// the position right after it is already the start of the next line.
static int LineEnd(const TextLayout *L, int li)
{
    int e = L->lines[li].start + L->lines[li].len;
    if (e < L->len && L->shown[e] != '\n') e--;
    return e;
}

// Caret position on line `li` closest to the horizontal offset x (measured from the start of the line)
static int CaretFromX(const TextLayout *L, int li, float x)
{
    int last = LineEnd(L, li);
    float cx = 0;
    for (int k = L->lines[li].start; k < last; k++) {
        if (x < cx + L->step[k] * 0.5f) return k;   // left half of a character: put the caret before it
        cx += L->step[k];
    }
    return last;
}

// How far a single-line field's text is shifted left so that the caret stays inside the box
static float SingleLineOffset(const TextLayout *L, int pos, float boxW)
{
    float cx = CaretX(L, LineOfCaret(L, pos), pos);
    float room = boxW - 2 * TF_PAD - 2;
    return (cx > room) ? cx - room : 0.0f;
}

// Insert one character at *pos and move *pos past it. False if the field is full.
static bool FieldInsertChar(char *buf, int cap, int *pos, char c)
{
    int len = (int)strlen(buf);
    if (len >= cap - 1) return false;
    memmove(buf + *pos + 1, buf + *pos, (size_t)(len - *pos + 1));   // shift the tail (and the '\0') right
    buf[*pos] = c;
    (*pos)++;
    return true;
}

// Delete the character at position pos
static void FieldRemoveChar(char *buf, int pos)
{
    memmove(buf + pos, buf + pos + 1, strlen(buf + pos));            // shift the tail (and the '\0') left
}

// Height and top y of the scrollbar thumb of a multi-line field, for a given scroll position
static void FieldThumb(Rectangle track, float viewH, float contentH, float scroll, float maxScroll,
                       float *thumbH, float *thumbY)
{
    *thumbH = track.height * (viewH / contentH);
    if (*thumbH < SCROLLBAR_MIN_THUMB) *thumbH = SCROLLBAR_MIN_THUMB;
    *thumbY = track.y + (track.height - *thumbH) * (scroll / maxScroll);
}

// Text field. Edits `buf` in place when it has focus. `flags` = TF_* options.
// Click to put the caret anywhere; arrows, Home, End, Backspace and Delete edit at the caret.
// Multi-line fields wrap long lines and scroll (mouse wheel, scrollbar, or by moving the caret).
// Returns true on a frame in which the text changed.
static bool TextField(Rectangle r, const char *label, char *buf, int cap, FieldId id, int flags)
{
    bool multiline = (flags & TF_MULTILINE) != 0;
    bool readonly  = (flags & TF_READONLY)  != 0;
    bool masked    = (flags & TF_MASKED)    != 0;

    UIText(label, (int)r.x, (int)r.y - 20, 20, COL_DIM);

    Vector2 mouse   = GetMousePosition();
    bool    hover   = CheckCollisionPointRec(mouse, r);
    bool    pressed = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);

    float     wrapMax = multiline ? r.width - 2 * TF_PAD - TF_BAR_W : 0.0f;   // 0 = never wrap
    float     viewH   = r.height - 2 * TF_PAD;                                // height available for text
    Rectangle track   = { r.x + r.width - 11, r.y + 6, SCROLLBAR_W, r.height - 12 };

    // A different text than last frame (another bug was opened): start at the top again
    if (scrollOwner[id] != buf) { scrollOwner[id] = buf; fieldScroll[id] = 0.0f; }

    TextLayout L;
    BuildLayout(&L, buf, masked, wrapMax);

    float contentH  = L.count * (float)LINE_H;
    float maxScroll = (multiline && contentH > viewH) ? contentH - viewH : 0.0f;

    // Mouse wheel over the field (works for read-only fields too, so guests can read long text)
    if (maxScroll > 0 && hover) fieldScroll[id] -= GetMouseWheelMove() * LINE_H * 2;

    // Pressing the scrollbar thumb starts a drag
    bool onThumb = false;
    if (maxScroll > 0) {
        float thumbH, thumbY;
        FieldThumb(track, viewH, contentH, fieldScroll[id], maxScroll, &thumbH, &thumbY);
        Rectangle grab = { track.x - SCROLLBAR_GRAB, thumbY, track.width + 2 * SCROLLBAR_GRAB, thumbH };
        if (pressed && CheckCollisionPointRec(mouse, grab)) {
            barDragField  = id;
            barDragOffset = mouse.y - thumbY;
            onThumb = true;
        }
    }

    // Clicking in the field gives it focus and puts the caret where the click landed
    bool clicked = false;
    if (!readonly && pressed && hover) {
        activeField = id;
        if (!onThumb) {
            float offX = 0.0f;   // a single-line field may be shifted left; use the shift it was drawn with
            if (!multiline && caretField == id) offX = SingleLineOffset(&L, caret < L.len ? caret : L.len, r.width);
            int li = (int)((mouse.y - (r.y + TF_PAD) + (multiline ? fieldScroll[id] : 0.0f)) / LINE_H);
            if (li < 0) li = 0;
            if (li > L.count - 1) li = L.count - 1;
            caret = CaretFromX(&L, li, mouse.x - (r.x + TF_PAD) + offX);
            caretField = id;
            clicked = true;
        }
    }

    bool active  = !readonly && (activeField == id);
    bool changed = false;
    bool moved   = clicked;

    if (!active) {
        if (caretField == id) caretField = FIELD_NONE;           // lost focus: forget the caret
    } else {
        if (caretField != id) { caretField = id; caret = L.len; moved = true; }   // focused by code (Tab, New Bug): caret at the end
        if (caret > L.len) caret = L.len;                        // the text got shorter behind our back (e.g. search cleared)

        // ----- Typing and deleting happen at the caret -----
        int ch;
        while ((ch = GetCharPressed()) > 0)                      // typed characters this frame
            if (ch >= 32 && ch <= 126 && FieldInsertChar(buf, cap, &caret, (char)ch)) changed = true;
        if (multiline && IsKeyPressed(KEY_ENTER) && FieldInsertChar(buf, cap, &caret, '\n')) changed = true;
        if (KeyRepeats(KEY_BACKSPACE) && caret > 0) { caret--; FieldRemoveChar(buf, caret); changed = true; }
        if (KeyRepeats(KEY_DELETE) && caret < (int)strlen(buf)) { FieldRemoveChar(buf, caret); changed = true; }
        if (changed) BuildLayout(&L, buf, masked, wrapMax);      // the text moved: measure it again

        // ----- Moving the caret -----
        int li = LineOfCaret(&L, caret);
        if (KeyRepeats(KEY_LEFT)  && caret > 0)     { caret--; moved = true; }
        if (KeyRepeats(KEY_RIGHT) && caret < L.len) { caret++; moved = true; }
        if (IsKeyPressed(KEY_HOME)) { caret = L.lines[li].start; moved = true; }
        if (IsKeyPressed(KEY_END))  { caret = LineEnd(&L, li);   moved = true; }
        if (multiline && KeyRepeats(KEY_UP) && li > 0) {
            caret = CaretFromX(&L, li - 1, CaretX(&L, li, caret));
            moved = true;
        }
        if (multiline && KeyRepeats(KEY_DOWN) && li < L.count - 1) {
            caret = CaretFromX(&L, li + 1, CaretX(&L, li, caret));
            moved = true;
        }
        if (changed || moved) caretStamp = GetTime();
    }

    // ----- Scrolling: keep the caret in view, follow the scrollbar thumb, stay within range -----
    contentH  = L.count * (float)LINE_H;
    maxScroll = (multiline && contentH > viewH) ? contentH - viewH : 0.0f;

    if (multiline && active && (moved || changed)) {
        float top    = LineOfCaret(&L, caret) * (float)LINE_H;
        float bottom = top + LINE_H;
        if (top < fieldScroll[id])                   fieldScroll[id] = top;
        else if (bottom > fieldScroll[id] + viewH)   fieldScroll[id] = bottom - viewH;
    }

    if (barDragField == id) {
        if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT) || maxScroll <= 0) {
            barDragField = FIELD_NONE;
        } else {
            float thumbH, thumbY;
            FieldThumb(track, viewH, contentH, fieldScroll[id], maxScroll, &thumbH, &thumbY);
            float travel = track.height - thumbH;
            if (travel > 0) fieldScroll[id] = (mouse.y - barDragOffset - track.y) / travel * maxScroll;
        }
    }
    if (fieldScroll[id] > maxScroll) fieldScroll[id] = maxScroll;
    if (fieldScroll[id] < 0)         fieldScroll[id] = 0;

    // ----- Drawing -----
    DrawRectangleRec(r, (Color){ 24, 26, 31, 255 });
    DrawRectangleLinesEx(r, 2, active ? (Color){ 90, 140, 230, 255 } : (Color){ 70, 74, 86, 255 });

    float scrollY = multiline ? fieldScroll[id] : 0.0f;
    float offX    = (!multiline && active) ? SingleLineOffset(&L, caret, r.width) : 0.0f;
    int   originX = (int)(r.x + TF_PAD - offX);

    // Clip drawing to the box so long text can't spill out.
    BeginScissorMode((int)r.x + 2, (int)r.y + 2, (int)r.width - 4, (int)r.height - 4);
    {
        char line[TF_MAX_TEXT + 1];
        for (int i = 0; i < L.count; i++) {
            float y = r.y + TF_PAD - scrollY + i * (float)LINE_H;
            if (y + LINE_H < r.y || y > r.y + r.height) continue;       // scrolled out of view: skip it
            memcpy(line, L.shown + L.lines[i].start, (size_t)L.lines[i].len);
            line[L.lines[i].len] = '\0';
            UIText(line, originX, (int)y, TF_FONT, COL_TEXT);
        }

        // Blinking caret (always visible just after it moves, so you can see where a click landed)
        if (active && ((int)((GetTime() - caretStamp) * 2.0) % 2 == 0)) {
            int   cl = LineOfCaret(&L, caret);
            float cy = r.y + TF_PAD - scrollY + cl * (float)LINE_H;
            DrawRectangle(originX + (int)CaretX(&L, cl, caret), (int)cy, 2, TF_FONT, COL_TEXT);
        }
    }
    EndScissorMode();

    // Scrollbar (only when the text is taller than the box)
    if (maxScroll > 0) {
        float thumbH, thumbY;
        FieldThumb(track, viewH, contentH, fieldScroll[id], maxScroll, &thumbH, &thumbY);
        Color thumbCol = (Color){ 110, 118, 140, 255 };
        if (barDragField == id) thumbCol = ColorBrightness(thumbCol, 0.3f);
        DrawRectangleRounded(track, 1.0f, 4, COL_PANEL);
        DrawRectangleRounded((Rectangle){ track.x, thumbY, track.width, thumbH }, 1.0f, 4, thumbCol);
    }

    return changed;
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
               "1. Launch app\n2. Press Ctrl+S immediately", STATUS_OPEN, PRIO_CRITICAL, "");
        AddBug("Button label clipped on small windows", "The 'Submit' label is cut off below 800px width.",
               "1. Resize window to 700px wide", STATUS_IN_PROGRESS, PRIO_LOW, "");
        AddBug("Typo on settings page", "'Prefrences' should be 'Preferences'.",
               "1. Open Settings", STATUS_RESOLVED, PRIO_MEDIUM, "");
    }
    LoadAccounts();
    selected = -1;   // nothing opened at startup

    Page  page = PAGE_WELCOME;       // the app always opens on the welcome page
    float listScroll = 0.0f;         // how many pixels the table is scrolled down
    bool  scrollToSelected = false;  // set when we want the selected row brought into view
    bool  draggingThumb = false;     // true while the user is holding the scrollbar thumb
    float dragOffsetY   = 0.0f;      // where on the thumb we grabbed it (distance from thumb top to the mouse)
    char  searchQuery[64] = "";      // what the user has typed in the search box
    char  prevQuery[64]   = "";      // the query from the previous frame, to detect changes
        bool  statusOn[STATUS_COUNT] = { true, true, true };   // which statuses are shown (all on = no status filtering)

    while (!WindowShouldClose())
    {
        // Page changes are applied at the end of the frame, so the click that
        // switched pages can't also press a button on the new page.
        Page nextPage = page;

        // Clicking empty space clears focus; fields re-set it if clicked (see TextField).
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) activeField = FIELD_NONE;

        int W = GetScreenWidth();
        int H = GetScreenHeight();
        bool canEdit = (currentUser >= 0) || !REQUIRE_LOGIN_TO_EDIT;   // may this visitor create/edit bugs?

        BeginDrawing();
        ClearBackground(COL_BG);

        if (page == PAGE_WELCOME)
        {
            // =====================================================
            //  WELCOME PAGE: shown every time the app opens
            // =====================================================
            int cx = W / 2;
            int bw = 320, bh = 52, bx = cx - bw / 2;

            UITextCentered("Bug Tracker", cx, 200, 56, COL_TEXT);          // the logo goes here later
            UITextCentered("A bug tracking platform.", cx, 275, 22, COL_DIM);

            int by = 390;
            if (currentUser >= 0) {
                UITextCentered(TextFormat("Signed in as %s", accounts[currentUser].name), cx, 330, 20, COL_DIM);
                if (Button((Rectangle){ (float)bx, (float)by, (float)bw, (float)bh }, "Browse bugs", COL_BTN_BLUE))
                    nextPage = PAGE_HOME;
                by += bh + 16;
                if (Button((Rectangle){ (float)bx, (float)by, (float)bw, (float)bh }, "Sign out", COL_BTN_GREY))
                    currentUser = -1;
            } else {
                if (Button((Rectangle){ (float)bx, (float)by, (float)bw, (float)bh }, "Sign in", COL_BTN_BLUE)) {
                    OpenAuth(false);
                    nextPage = PAGE_AUTH;
                }
                by += bh + 16;
                if (Button((Rectangle){ (float)bx, (float)by, (float)bw, (float)bh }, "Create account", COL_BTN_GREEN)) {
                    OpenAuth(true);
                    nextPage = PAGE_AUTH;
                }
                by += bh + 16;
                if (Button((Rectangle){ (float)bx, (float)by, (float)bw, (float)bh }, "Browse bugs", COL_BTN_GREY))
                    nextPage = PAGE_HOME;
            }
        }
        else if (page == PAGE_AUTH)
        {
            // =====================================================
            //  SIGN IN / CREATE ACCOUNT
            // =====================================================
            int cx = W / 2;
            int cardW = 520;
            int fx = cx - cardW / 2;

            if (Button((Rectangle){ 40, 16, 120, 40 }, "< Back", COL_BTN_GREY) || IsKeyPressed(KEY_ESCAPE)) {
                nextPage = PAGE_WELCOME;
                activeField = FIELD_NONE;
            }

            UITextCentered(createMode ? "Create account" : "Sign in", cx, 150, 44, COL_TEXT);

            int y = 260;
            TextField((Rectangle){ (float)fx, (float)y, (float)cardW, 44 }, "Email or username",
                      authName, sizeof(authName), FIELD_AUTH_NAME, 0);
            y += 90;
            TextField((Rectangle){ (float)fx, (float)y, (float)cardW, 44 }, "Password",
                      authPass, sizeof(authPass), FIELD_AUTH_PASS, TF_MASKED);
            if (createMode) {
                y += 90;
                TextField((Rectangle){ (float)fx, (float)y, (float)cardW, 44 }, "Confirm password",
                          authPass2, sizeof(authPass2), FIELD_AUTH_PASS2, TF_MASKED);
            }
            y += 44 + 14;   // just below the last field

            if (authMsg[0]) UIText(authMsg, fx, y, 20, COL_ERROR);
            y += 36;

            bool submit = Button((Rectangle){ (float)fx, (float)y, (float)cardW, 48 },
                                 createMode ? "Create account" : "Sign in", COL_BTN_BLUE);
            y += 48 + 16;

            // Tab moves to the next field, Enter submits
            if (IsKeyPressed(KEY_TAB)) {
                FieldId order[3] = { FIELD_AUTH_NAME, FIELD_AUTH_PASS, FIELD_AUTH_PASS2 };
                int n = createMode ? 3 : 2;
                int cur = -1;
                for (int k = 0; k < n; k++) if (order[k] == activeField) cur = k;
                activeField = order[(cur + 1) % n];
            }
            if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) submit = true;

            if (submit && SubmitAuth()) {
                authName[0] = '\0';
                authPass[0] = '\0';
                authPass2[0] = '\0';
                activeField = FIELD_NONE;
                nextPage = PAGE_HOME;
            }

            // Switch between the two modes
            if (Button((Rectangle){ (float)fx, (float)y, (float)cardW, 40 },
                       createMode ? "Already have an account? Sign in" : "No account yet? Create one", COL_BTN_GREY))
                OpenAuth(!createMode);
        }
        else if (page == PAGE_HOME)
        {
            // =====================================================
            //  HOME PAGE: header, search, results table
            // =====================================================
            Rectangle listView = { 0, TABLE_TOP, (float)W, (float)(H - TABLE_TOP) };

            // Build the list of bugs to show: visible[slot] = index into bugs[].
            int visible[MAX_BUGS];
            int rowCount = 0;
            for (int k = 0; k < bugCount; k++)
                if (BugMatches(&bugs[k], searchQuery, statusOn)) visible[rowCount++] = k;
            qsort(visible, rowCount, sizeof(int), CompareBugs);   // order by the chosen column

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

            // ----- Header: title (click = welcome page), then from the right: account button, + New Bug, who -----
            Rectangle titleR = { MARGIN_X, 10, 240, 52 };
            bool titleHover = CheckCollisionPointRec(GetMousePosition(), titleR);
            UIText("Bug Tracker", MARGIN_X, 14, 38, titleHover ? WHITE : COL_TEXT);   // the logo goes here later
            if (titleHover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) nextPage = PAGE_WELCOME;

            int rightX = W - MARGIN_X;
            Rectangle acctBtn = { (float)(rightX - 130), 16, 130, 40 };
            if (currentUser >= 0) {
                if (Button(acctBtn, "Sign out", COL_BTN_GREY)) currentUser = -1;
            } else {
                if (Button(acctBtn, "Sign in", COL_BTN_BLUE)) { OpenAuth(false); nextPage = PAGE_AUTH; }
            }
            rightX = (int)acctBtn.x - 12;

            if (canEdit) {
                Rectangle newBtn = { (float)(rightX - 140), 16, 140, 40 };
                if (Button(newBtn, "+ New Bug", COL_BTN_BLUE)) {
                    if (bugCount < MAX_BUGS) {
                        AddBug("", "", "", STATUS_OPEN, PRIO_MEDIUM, currentUser >= 0 ? accounts[currentUser].name : "Guest");
                        selected = bugCount - 1;
                        searchQuery[0] = '\0';
                                                for (int s = 0; s < STATUS_COUNT; s++) statusOn[s] = true;   // new bugs start Open, so don't let a chip hide them
                        activeField = FIELD_TITLE;
                        scrollToSelected = true;    // so the new row is in view when we come back
                        nextPage = PAGE_DETAIL;
                    }
                }
                rightX = (int)newBtn.x - 16;
            }

            const char *who = (currentUser >= 0) ? TextFormat("Signed in as %s", accounts[currentUser].name)
                                                 : "Browsing as guest";
            UIText(who, rightX - UIWidth(who, 20), 24, 20, COL_DIM);

            // ----- Search box -----
            Rectangle searchBox = { MARGIN_X, SEARCH_Y, (float)(W - 2 * MARGIN_X), SEARCH_H };
            TextField(searchBox, "", searchQuery, sizeof(searchQuery), FIELD_SEARCH, 0);
            if (searchQuery[0] == '\0' && activeField != FIELD_SEARCH)
                UIText("Search by title, description or reporter...", (int)searchBox.x + 8, (int)searchBox.y + 8, 22, COL_DIM);

            // Clear button: only shown while there is text in the search box
            if (searchQuery[0] != '\0') {
                Rectangle clearBtn = { searchBox.x + searchBox.width - 36, searchBox.y + 7, 26, 26 };
                if (Button(clearBtn, "x", COL_BTN_GREY)) {
                    searchQuery[0] = '\0';
                    activeField = FIELD_SEARCH;   // keep focus so the user can type a new search
                }
            }

            // ----- Result count -----
            UIText(TextFormat("Showing %d of %d", rowCount, bugCount), MARGIN_X, SEARCH_Y + SEARCH_H + 8, 18, COL_DIM);


            // ----- Status filter chips (right-aligned on the same line) -----
            float chipsX = (float)(W - MARGIN_X - STATUS_COUNT * CHIP_W - (STATUS_COUNT - 1) * CHIP_GAP);
            for (int s = 0; s < STATUS_COUNT; s++) {
                Rectangle chip = { chipsX + s * (CHIP_W + CHIP_GAP), CHIP_Y, CHIP_W, CHIP_H };
                if (Chip(chip, statusNames[s], statusOn[s], statusColors[s])) {
                    statusOn[s] = !statusOn[s];
                    listScroll = 0.0f;          // the list changed, so jump back to the top
                }
            }

            // ----- Column header bar (click a column name to sort by it) -----
            Rectangle head = { MARGIN_X, TABLE_HEAD_Y, (float)(W - 2 * MARGIN_X), TABLE_HEAD_H };
            DrawRectangleRounded(head, 0.2f, 6, COL_PANEL);
            for (int k = 0; k < SORT_COUNT; k++) {
                int cellW = ((k + 1 < SORT_COUNT) ? colX[k + 1] : (int)head.width) - colX[k];
                Rectangle cell = { head.x + colX[k] - 8, head.y, (float)cellW, head.height };
                bool cellHover = CheckCollisionPointRec(GetMousePosition(), cell) && !draggingThumb;
                bool isSorted  = ((int)sortCol == k);

                if (cellHover) DrawRectangleRounded(cell, 0.2f, 6, COL_ROW);
                UIText(colNames[k], (int)head.x + colX[k], (int)head.y + 7, 18,
                       (isSorted || cellHover) ? COL_TEXT : COL_DIM);
                if (isSorted)   // arrow up = ascending (A-Z, low-high), down = descending
                    DrawSortArrow((int)head.x + colX[k] + UIWidth(colNames[k], 18) + 14, (int)head.y + 12, sortAsc, COL_TEXT);

                if (cellHover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                    if (isSorted) sortAsc = !sortAsc;                 // same column again: flip direction
                    else { sortCol = (SortCol)k; sortAsc = !(k == SORT_PRIORITY || k == SORT_CREATED || k == SORT_UPDATED); }   // these start with the highest or newest on top
                    listScroll = 0.0f;
                }
            }

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
                char fit[128];   // long names/titles are shortened with "..." so columns never overlap
                UIText(FitText(ReporterName(&bugs[i]), COL_ASSIGNEE_X - COL_REPORTER_X - 20, 20, fit, sizeof(fit)),
                       (int)row.x + COL_REPORTER_X, ty, 20, textCol);
                UIText(FitText(bugs[i].assignee[0] ? bugs[i].assignee : "-", COL_CREATED_X - COL_ASSIGNEE_X - 20, 20, fit, sizeof(fit)),
                       (int)row.x + COL_ASSIGNEE_X, ty, 20, bugs[i].assignee[0] ? textCol : COL_DIM);
                char dateStr[16];
                FormatDate(bugs[i].created, dateStr, sizeof(dateStr));
                UIText(dateStr, (int)row.x + COL_CREATED_X, ty, 20, textCol);
                FormatDate(bugs[i].updated, dateStr, sizeof(dateStr));
                UIText(dateStr, (int)row.x + COL_UPDATED_X, ty, 20, textCol);
                UIText(FitText(bugs[i].title[0] ? bugs[i].title : "(untitled)", (int)row.width - COL_TITLE_X - 24, 20, fit, sizeof(fit)),
                       (int)row.x + COL_TITLE_X, ty, 20, textCol);

                EndScissorMode();

                // Clicking a row opens that bug's detail page
                if (hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                    selected = i;
                    activeField = FIELD_NONE;
                    nextPage = PAGE_DETAIL;
                }
            }

            // ----- Empty state -----
            if (rowCount == 0) {
                const char *msg = (bugCount > 0)  ? "No bugs match your filters."
                                : canEdit         ? "No bugs yet. Click + New Bug."
                                                  : "No bugs yet. Sign in to create one.";
                UIText(msg, MARGIN_X + 8, TABLE_TOP + 16, 20, COL_DIM);
            }

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
            //  DETAIL PAGE: view or edit one bug
            // =====================================================
            if (selected < 0 || selected >= bugCount) {
                nextPage = PAGE_HOME;   // nothing valid to show: go back
            } else {
                int px = 40;
                int pw = W - 2 * px;
                Bug *b = &bugs[selected];
                int fieldFlags = canEdit ? 0 : TF_READONLY;

                // Back to the list (button or ESC)
                if (Button((Rectangle){ (float)px, 16, 120, 40 }, "< Back", COL_BTN_GREY) || IsKeyPressed(KEY_ESCAPE)) {
                    nextPage = PAGE_HOME;
                    scrollToSelected = true;   // bring this bug's row into view on the home page
                    activeField = FIELD_NONE;
                }

                const char *heading = TextFormat("Bug #%d", b->id);
                UIText(heading, px + 140, 18, 34, COL_TEXT);
                UIText(TextFormat("Reported by %s", ReporterName(b)), px + 140 + UIWidth(heading, 34) + 24, 28, 20, COL_DIM);

                if (canEdit) {
                    // Status / priority / resolve buttons. Click to cycle.
                    if (Button((Rectangle){ (float)(px + pw - 480), 18, 150, 36 }, statusNames[b->status], statusColors[b->status]))
                        SetStatus(b, (Status)((b->status + 1) % STATUS_COUNT));
                    if (Button((Rectangle){ (float)(px + pw - 320), 18, 150, 36 }, prioNames[b->priority], prioColors[b->priority]))
                        SetPriority(b, (Priority)((b->priority + 1) % PRIO_COUNT));
                    if (Button((Rectangle){ (float)(px + pw - 160), 18, 160, 36 }, "Mark Resolved", COL_BTN_GREEN))
                        SetStatus(b, STATUS_RESOLVED);
                } else {
                    // Guests see the same info, but it can't be changed
                    Badge((Rectangle){ (float)(px + pw - 320), 18, 150, 36 }, statusNames[b->status], statusColors[b->status]);
                    Badge((Rectangle){ (float)(px + pw - 160), 18, 160, 36 }, prioNames[b->priority], prioColors[b->priority]);
                }
                
                // ----- Assignee row, with the created / updated dates on the right -----
                int rowY = 68;
                UIText("Assignee", px, rowY + 8, 20, COL_DIM);
                const char *assigneeLabel = b->assignee[0] ? b->assignee : "Unassigned";
                if (canEdit) {
                    char fitName[64];   // a long name is shortened so it stays inside the button
                    if (Button((Rectangle){ (float)(px + 100), (float)rowY, 260, 36 },
                               FitText(assigneeLabel, 236, 22, fitName, sizeof(fitName)), COL_BTN_GREY))
                        CycleAssignee(b);
                    if (currentUser >= 0 && strcmp(b->assignee, accounts[currentUser].name) != 0
                        && Button((Rectangle){ (float)(px + 376), (float)rowY, 170, 36 }, "Assign to me", COL_BTN_BLUE))
                        SetAssignee(b, accounts[currentUser].name);
                } else {
                    UIText(assigneeLabel, px + 100, rowY + 8, 20, COL_TEXT);   // guests see it, can't change it
                }

                char createdStr[32], updatedStr[32];
                FormatTime(b->created, createdStr, sizeof(createdStr));
                FormatTime(b->updated, updatedStr, sizeof(updatedStr));
                const char *dates = TextFormat("Created %s    Updated %s", createdStr, updatedStr);
                UIText(dates, px + pw - UIWidth(dates, 20), rowY + 8, 20, COL_DIM);

                // Layout computed from the window height so it fills any size
                int titleY  = 144, titleH = 44;
                int labelGap = 50;                                   // room for the label above each box
                int bottom  = H - 70;                                // leave space for the hint line
                int areaH   = (bottom - (titleY + titleH) - 2 * labelGap) / 2;
                int descY   = titleY + titleH + labelGap;
                int stepsY  = descY + areaH + labelGap;

                                if (TextField((Rectangle){ (float)px, (float)titleY, (float)pw, (float)titleH }, "Title",              b->title,       sizeof(b->title),       FIELD_TITLE, fieldFlags))
                    b->updated = time(NULL);
                if (TextField((Rectangle){ (float)px, (float)descY,  (float)pw, (float)areaH  }, "Description",        b->description, sizeof(b->description), FIELD_DESC,  TF_MULTILINE | fieldFlags))
                    b->updated = time(NULL);
                if (TextField((Rectangle){ (float)px, (float)stepsY, (float)pw, (float)areaH  }, "Steps to reproduce", b->steps,       sizeof(b->steps),       FIELD_STEPS, TF_MULTILINE | fieldFlags))
                    b->updated = time(NULL);

                UIText(canEdit ? "Click to place the cursor. Arrows move it. Enter = newline. Mouse wheel scrolls long text. ESC = back to list."
                               : "Read-only: sign in to edit bugs. ESC = back to list.",
                       px, H - 40, 20, COL_DIM);
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