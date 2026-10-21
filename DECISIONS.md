# Design decisions

Status: agreed upon

## Data model
- Statuses: Open, In Progress, Resolved, Closed. Reopen is a transition, not a status.
- Resolutions (required when resolving): Fixed, Won't Fix, Duplicate, Invalid, Works For Me.
- Fields added to Bug: created and updated times, reporter, assignee (-1 = unassigned), resolution, activity log.
- Activity log: fixed array of 32 entries per bug, each with type, user, time and 256 chars of text. When full, new comments are rejected with a message.
- Save format: raw struct dump to bugs.dat. SAVE_MAGIC must be bumped whenever the Bug struct changes, so old files are rejected instead of loaded as garbage. Not portable across machines or compilers; a text format is future work.

## Users and access
- 4 hardcoded demo users with plain-text passwords. This is a demo, not security.
- Login is required for everything except the Home screen.
- No roles or permissions: every user can do everything.
- "Create account" is deferred.

## Screens
- Home, Login, Browse, Report, Detail. One `currentScreen` variable and a switch in the main loop.
- Report edits a draft bug that is not in bugs[] until Submit. Submit opens the new bug's Detail screen.

## Working agreements
- One branch per feature, PR into main, squash merge, never commit directly to main.
- No new text drawn with DrawText or MeasureText. Use UIText and UIWidth so everything uses the UI font.
- Build with warnings on and don't add new warnings.

## Open questions
- What did the professor mean by "full user lifecycle": the bug workflow, user accounts with login, or both?
