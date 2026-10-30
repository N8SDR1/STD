# Read before any change

- NEVER add, duplicate, or move Callsign or Locator (grid) fields/settings/labels. They exist ONLY in Options → User (userCall, userGrid). Every feature reads them from there. Before any commit, grep the diff for call/grid/locator widgets or settings keys and remove any new one.
- TCI connect sends AUDIO_START:0, never bare START.
- CW stays in Lyra DeepFist; STD does not send CW.
- Do not kill/restart STD while Rick is testing unless he approves a rebuild.
- Work on wspr-hell-ft-multianswer, never start from main; never merge main unasked.
