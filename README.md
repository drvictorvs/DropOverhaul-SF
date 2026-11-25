# Drop Overhaul SF

An SFSE plugin for Starfield that lets you pick up a loose object and place it precisely, the way the outpost and ship builders place items: the object sits on whatever you aim at, you turn it with the mouse, and E sets it down. Pressing E and dragging uses the game's physics grab, which swings and drops objects; this holds them still.

Status: 0.5.0, in testing. Picking up and holding work since 0.4.0. 0.5.0 adds builder mode (the default) and mouse controls; the older way, holding the object in the air in front of you, is still there as hold mode.

Requires [SFSE](https://www.nexusmods.com/starfield/mods/106) and [Address Library for SFSE Plugins](https://www.nexusmods.com/starfield/mods/3256). Built for Starfield 1.16.244. No plugin (ESM/ESP) needed. Optional: [OSF Settings](https://github.com/ozooma10/osf-settings-slim) 1.0.0, for an in-game settings page.

## Controls

| Key (default) | Does |
|---|---|
| Middle mouse | Pick up what you're looking at (whatever the "E) Take" prompt is for). Press again to place it |
| E (the game's Activate key) | Place it |
| Delete | Put it back where it was |
| Left mouse button, drag | Turn it |
| Right mouse button, drag | Tilt it (up / down) and roll it (left / right) |
| Mouse wheel | Turn it in steps (15° by default) |
| Numpad 4 / 6 | Turn left / right |
| Numpad 8 / 2 | Tilt forward / back |
| Numpad 7 / 9 | Roll left / right |
| Numpad 5 | Upright (builder mode keeps its facing; hold mode turns it the same way as you) |
| PageUp / PageDown | Higher / lower than where it would sit |
| Numpad + / - | Farther / closer (hold mode) |
| Alt (hold) | Slow, fine adjustments, mouse included |
| Numpad . | Switch between "drops when placed" and "stays where placed" |

**Builder mode** (the default): the object sits on the surface under your crosshair, up to 10 m away: on a floor or table its bottom rests on it, against a wall it stands out from it. It keeps its own facing as you turn. With nothing in reach, it waits in the air at the last distance.

**Hold mode** (`bHoldMode = 1`, or "Hold in front of you" in OSF Settings): the object stays in the air in front of you at the distance and height you picked it up from, and turns with you.

While you hold an object, the game doesn't get E, the mouse buttons, the mouse wheel, or the mouse movement while you drag, so you don't take, shoot or look around by accident. It still gets the other keys, so choose ones it doesn't use.

## Settings

All keys, speeds and options are on the plugin's OSF Settings page (Drop Overhaul), or in `Data/SFSE/Plugins/DropOverhaulSF.ini` without OSF Settings.

With OSF Settings installed:

- its values replace the INI's (except `bDebugLog`, which is always read from the INI), and changes there apply while the game runs;
- if the plugin can't run on your game version, it says so in OSF Settings' Mod Issues tab, as well as in the log.

When placed, the object either drops and settles onto what's below it (the default), or stays exactly where you put it, even in mid-air. An object left in mid-air stays there until the area reloads; then physics takes over again.

Characters can't be picked up. Objects without physics (some furniture and statics) are skipped unless `bAllowStatic = 1`.

## How it works

- **What you're looking at:** the game's own view caster, the same target the HUD's activation prompt uses (`ViewCaster`, Address Library ID 82616 reads it).
- **Holding still:** while held, the object's physics is switched to keyframed, the way Papyrus `SetMotionType` does it (ID 125023 on the object's 3D); when it's placed or put back, it goes back to dynamic and is woken so it settles.
- **Where it goes:** in builder mode, a physics ray cast along the view (`bhkWorld::Pick`, ID 125018, set up the way the game's own activation pick sets it up) finds the surface under the crosshair, skipping the held object and you; the object's bounds keep it from sinking into the surface. In hold mode, a point along the view direction.
- **Moving:** every frame, the object is moved with the engine's `MoveTo` (ID 118176), the function behind Papyrus `SetPosition` and `SetAngle`.
- **Mouse and E:** just before the game reads each frame's input, the plugin takes those events while you hold an object (it clears their action and marks them disabled, and zeroes the mouse movement while you drag).
- **Each frame:** the plugin's work runs right after the game's player-controls input pass (`PerformInputProcessing`, ID 82442), once per frame, inside the game's own frame update. The hook is a jump at the start of that function rather than its vtable slot, so it coexists with plugins that hook the slot (such as Dialogue Movement Enabler).
- **Keys:** a key counts as pressed when either the game's own input events for that frame or Windows report it down; keys are read only while the game window has focus and no menu is open.
- **Messages:** HUD notifications through the event Papyrus `Debug.Notification` sends (ID 86800).

Before doing anything, the plugin checks that every Address Library ID it uses exists for the running game version. If one is missing, it logs which and stays inactive.

The log is at `Documents\My Games\Starfield\SFSE\Logs\DropOverhaulSF.log`. It always records each press of the pick-up key and what came of it; `bDebugLog = 1` in the INI adds every key and button press the game receives (device, code and the action it's bound to) and the camera and object positions at each pickup. For the first 10 minutes of a session, the log also notes once a minute that the plugin is still running.

## Not yet

- Highlighting the held object (the game has a highlight system its builders use; not hooked up yet).
- Snapping, and checking that the spot is clear.
- Lying flat on a sloped surface (the object stays upright; only its height follows the surface).
- Turning the object with the mouse (needs the plugin to take mouse input from the camera).
- Keeping "stays where placed" objects frozen after a reload.

## Build

Requires Visual Studio 2022 Build Tools (C++), XMake 3.0+ and Git. CommonLibSF (OSF's fork) is expected next to this project, and `xmake.lua` includes it as `../commonlibsf`:

```
...\Projects\commonlibsf
...\Projects\DropOverhaul-SF   <- this folder
```

In this folder:

```powershell
xmake f -c -m releasedbg
xmake build -v
```

`DropOverhaulSF.dll` and `.pdb` land in `build\windows\x64\releasedbg\`, and each build also copies them to `dist\Data\SFSE\Plugins\`, next to the INI, so `dist\Data` holds the complete mod (git ignores the copies). With `XSE_SF_MODS_PATH` (or `XSE_SF_GAME_PATH`) set, `xmake install` copies them, the INI and the OSF Settings schema into place. Otherwise, make a mod with:

```
Data/SFSE/Plugins/DropOverhaulSF.dll
Data/SFSE/Plugins/DropOverhaulSF.pdb
Data/SFSE/Plugins/DropOverhaulSF.ini
Data/SFSE/Plugins/OSF/Settings/schemas/dropoverhaul-sf.json
```

`src/vendor/` holds the OSF Settings SDK headers (see its README); the plugin builds and runs without OSF Settings installed.

## Credits

- Skyrim Thiago (Thiago099), [Object Manipulation Overhaul](https://www.nexusmods.com/skyrimspecialedition/mods/123664) for Skyrim ([source](https://github.com/Thiago099/Object-Manipulation-Overhaul), MIT): the design this follows.
- Jaxonz Positioner and Cobb Positioner, for the idea of keyboard-driven precise placement.
- ozooma10, [OSF Settings](https://github.com/ozooma10/osf-settings-slim) (GPL-3.0): the settings page and Mod Issues, through its SDK in `src/vendor/`.

## License

GPL-3.0.
