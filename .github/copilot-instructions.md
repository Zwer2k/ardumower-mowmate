# ArduMower Modem – Anweisungen für Copilot

## Verhaltensregeln

- **Einchecken nur auf ausdrückliche Aufforderung des Users.** Niemals selbstständig `git commit` ausführen.
- Beim Einchecken vorher `git status` / `git diff` prüfen: Änderungen, die nicht aus der aktuellen Session stammen, nach Möglichkeit als **separates Commit** einchecken.
- Commit-Messages auf Englisch, konventioneller Stil (`fix(ui): …`, `feat(map): …`), mit Bullet-Points für Details.
- Antworten an den User auf Deutsch.

## Projekt-Überblick

- **Firmware**: ESP32-S3 (Arduino/PlatformIO, `platformio.ini`, `src/`, `lib/`). C++ mit eigenen Unit-Tests in `test/` und `test_pathplanner/`.
- **Web-UI**: Svelte (SvelteKit) in `ui/`, TypeScript, Carbon Components. Build via `ci/bin/build-ui.sh` bzw. Taskfile.
- **Pfadplanung**: `lib/pathplanner/` (Clipper2-basiert) plus `src/domain/path_planner.cpp`.
- Build/Flash/Test-Workflows: `Taskfile.yml`, `ci/bin/`.

## Wichtige Konventionen im Code

- Map-Punkte: Backend `X/Y` (Y nach oben), Frontend `x/y` mit **invertiertem Y** (`y = -Y`, SVG-Koordinaten).
- Waypoints können `conn`-Flag (Connector-Punkte) tragen – werden nicht persistiert und bei Toggle-Wechsel neu berechnet.
- "Live-Map" = `ui/src/pages/dashboard/main/MiniMap.svelte` (Dashboard). "Editor-Map" = `ui/src/map/Map.svelte` mit Komponenten (`Perimeter.svelte`, `Dockpoints.svelte`, `Waypoints.svelte`, …).
- Darstellungsfarben sollen zwischen Live-Map und Editor-Map konsistent gehalten werden (Perimeter rot, Dock orange, Waypoints blau, gemähte Segmente grün, Mähfläche `rgba(36,161,72,0.15)`).
- Gamepad-Steuerung (`GamepadControl.svelte`): Browser zeigen Gamepads erst nach Tastendruck – kein Bug.

## Debug-Workflow

- UI-Debug-Modus starten mit `task debug-ui ESP_TARGET=esp32-s3`.
- In diesem Modus erfolgt nach Code-Änderungen **automatisch ein Reload** – kein manueller Neustart/Reload nötig, Änderungen an `ui/` sind sofort sichtbar.
- Die UI ist im Debug-Modus unter http://localhost:5000/ erreichbar (WebSocket via `ws://localhost:5000/ws`).
- Browser-Fehler (z. B. WebSocket-Disconnects) lassen sich direkt über die Browser-Tools auf http://localhost:5000/ analysieren.

## Build

- **Firmware + UI zusammen bauen:** `task compile-pio ESP_TARGET=esp32-S3` (PlatformIO; baut vorher automatisch die UI via `package-ui`). Dies ist der Standard-Build-Befehl.
- `task build-firmware` nutzt arduino-cli und baut **nur** die Firmware ohne UI – daher meist `compile-pio` verwenden.
- Achtung: `ESP_TARGET=esp32-S3` (großes S) mappt auf das PlatformIO-Env `esp32-S3-N16-R8`.

## Validierung

- Nach UI-Änderungen: UI-Build/Tests in `ui/` (vitest vorhanden).
- Firmware-Tests: nativer Test-Runner in `test/` bzw. `test_pathplanner/` (Makefile/CMake).
- Keine Commits ohne vorherige Aufforderung – siehe oben.
