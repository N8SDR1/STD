# Read before any change

- NEVER add, duplicate, or move Callsign or Locator (grid) fields/settings/labels. They exist ONLY in Options → User (userCall, userGrid). Every feature reads them from there. Before any commit, grep the diff for call/grid/locator widgets or settings keys and remove any new one.
- Exception (Rick, 2026-11-01): the keyboard-mode QSO bar may hold the OTHER station's Call and Loc (qsoBarCall, qsoBarLoc). These are the worked station, never your own call/grid, and are not saved as settings.
- TCI connect sends AUDIO_START:0, never bare START.
- CW stays in Lyra DeepFist; STD does not send CW.
- Do not kill/restart STD while Rick is testing unless he approves a rebuild.
- Work on wspr-hell-ft-multianswer, never start from main; never merge main unasked.
