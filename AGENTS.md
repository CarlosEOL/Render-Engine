# Build retention

- Keep the newest validated OpenGL and DX12 application builds. After a successful replacement Release build has passed the appropriate checks, remove superseded application builds and obsolete build archives.
- If the newest build is temporary, experimental, or test-only, retain the previous known-working application builds. Do not delete the stable builds merely because a newer test executable exists.
- Running render checks against an ordinary Release executable does not make that executable a test-only build. Distinguish the artifact's intended use from the checks used to validate it.
- Current application launchers are `build/current/bin/Render-Engine.exe` and `build/dx12/Release/bin/Render-Engine-DX12.exe`. Preserve both renderer variants when replacing builds.
- Preserve source, assets, dependencies, current incremental object caches, and useful text validation logs. Remove superseded/duplicate test captures after retaining the latest useful evidence; remove raw captures only after validating their replacement PNGs.
- Before recursive cleanup, resolve every target to an absolute path beneath this repository's `build/` directory and reject reparse points. Use native PowerShell filesystem operations and do not delete running application outputs.

These rules record the user's build-cleanup preference. Later explicit user instructions take precedence.
