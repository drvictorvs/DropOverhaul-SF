# OSF Settings SDK (vendored)

Unchanged copies of the [OSF Settings](https://github.com/ozooma10/osf-settings-slim) SDK headers from `sdk/` at commit `1a857dbab19815b0a86c47b217cb8ac1146c8ce4` (GPL-3.0 with a modding exception; see that repository's `LICENSE` and `EXCEPTIONS`). They match the copies OSF UI vendors for its OSF Settings 1.0.0 pairing.

| Header | ABI |
| --- | --- |
| `OSFSettings.h` | settings 1.0 |
| `OSFSettings_Providers.h` | providers 1.0 (key presses) |
| `OSFSettings_Diagnostics.h` | diagnostics 1.0 (Mod Issues) |

Copy them unchanged when updating. The plugin only uses them if `OSFSettings.dll` is loaded; without it, the plugin reads its INI.
