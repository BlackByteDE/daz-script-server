# TODO — Aufträge im Fork (BlackByteDE/daz-script-server)

Aufträge für das DazScriptServer-Plugin (diesen Fork). Die beads-Issues
(`bd ready`) sind **Upstreams** Roadmap und gehören nicht hierher — siehe
CLAUDE.md. Das Python-Gegenstück liegt in
`../daz-mcp-server/FORK_TODO.md` (gleiches Format).

## Regeln zum Eintragen (auch für andere Projekte)

1. **Eine Zeile pro Auftrag** in der Tabelle *Offen* (oder *Später /
   optional*). Keine Befunde, Logs oder Reproduktionen hier — die gehören in
   den Bug-Katalog bzw. eine Themendatei im eigenen Repo. Die Zeile verlinkt
   nur darauf.
2. **Was:** Was das Plugin danach können oder anders machen soll, aus
   Sicht des Aufrufers (z. B. „Endpoint X liefert Y“, nicht „Bug in Z“).
3. **Quelle:** wer den Auftrag gibt.
   - Projektname (`pinkcharakter`, `daz-mcp-server`, …) = von einem anderen
     Projekt hier abgelegt
   - `User` = im Chat beauftragt
   - `selbst` = bei der Arbeit hier festgestellt
4. **Seit:** Datum des Eintrags (`YYYY-MM-DD`).
5. **Details:** Bug-Nummer aus dem Bug-Katalog
   (`D:\Dev\pinkcharakter\docs\daz-mcp-bridge-bugs.md`), falls vorhanden,
   plus Link auf die Datei mit den Details. Pfade relativ zu diesem Repo
   (`../pinkcharakter/docs/…`), **ohne `#anker`** — Überschriften ändern
   sich, der Anker bricht dann still.
6. **Nicht doppelt führen:** Wird ein Auftrag aus der eigenen Todo-Liste
   hierher verschoben, dort löschen (oder nur noch „→ daz-script-server
   FORK_TODO“ stehen lassen, solange das eigene Projekt darauf wartet).
7. **Erledigt** wird die Zeile hier gelöscht; der Git-Verlauf und
   [FORK_CHANGELOG.md](FORK_CHANGELOG.md) halten fest, was geliefert wurde.
   Das auftraggebende Projekt prüft das Ergebnis und räumt bei sich auf.

## Offen

| Was | Quelle | Seit | Details |
|---|---|---|---|
| Erkennen, wann das Kleidungs-Smoothing (`DzMeshSmoothModifier`) nach einem Frame-Wechsel fertig ist, damit ein Render erst dann startet und die Kleidung im Bild sitzt (heute feste `--settle-seconds` im Aufrufer). Untersuchung zuerst: Ist `DzProgress::isActive()`/`DzBackgroundProgress::isActive()` (siehe `isMainThreadBusy()`) während des Smoothings `true`? Welche Signale senden `DzMeshSmoothModifier`/`DzObject`? Gibt es einen Cache-Status? Per DazScript nicht lesbar: `forceCacheUpdate()` + `getCachedGeom()` enthält das Smoothing nicht. Ergebnis je nach Fund: SSE-Event (z. B. `smoothing.finished`) oder Busy-Flag, das Render-Endpoints/`daz_wait_for_scene_event` abwarten. `time.changed` reicht nicht (feuert vor der Berechnung) | pinkcharakter (via daz-mcp-server) | 2026-10-19 | Bug #46 — [bugs/render-viewport.md](../pinkcharakter/docs/bugs/render-viewport.md); Aufrufer-Sicht: `../daz-mcp-server/FORK_TODO.md` |

## Später / optional

| Was | Quelle | Seit | Details |
|---|---|---|---|
