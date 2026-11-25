# Drop Overhaul SF

An SFSE plugin for Starfield that lets you pick up a loose object and place it precisely: hold it in front of you, turn it on any axis, push it farther or pull it closer, raise or lower it, then set it down. Pressing E and dragging uses the game's physics grab, which swings and drops objects; this holds them still.

Status: 0.1.1, not yet working in game: 0.1.0 never reacted to the pick-up key; 0.1.1 logs why.

Requires [SFSE](https://www.nexusmods.com/starfield/mods/106) and [Address Library for SFSE Plugins](https://www.nexusmods.com/starfield/mods/3256). Built for Starfield 1.16.244. No plugin (ESM/ESP) needed.

## Controls

| Key (default) | Does |
|---|---|
| Middle mouse | Pick up what you're looking at (whatever the "E) Take" prompt is for). Press again to place it |
| Delete | Put it back where it was |
| Numpad 4 / 6 | Turn left / right |
| Numpad 8 / 2 | Tilt forward / back |
| Numpad 7 / 9 | Roll left / right |
| Numpad 5 | Upright, facing the same way as you |
| Numpad + / - | Farther / closer |
| PageUp / PageDown | Higher / lower |
| Alt (hold) | Slow, fine adjustments |
| Numpad . | Switch between "drops when placed" and "stays where placed" |

While you hold an object, it follows where you look and turns with you. All keys and speeds are set in `Data/SFSE/Plugins/DropOverhaulSF.ini`. The game still receives these keys, so choose ones it doesn't use.

When placed, the object either drops and settles onto what's below it (the default), or stays exactly where you put it, even in mid-air. An object left in mid-air stays there until the area reloads; then physics takes over again.

Characters can't be picked up. Objects without physics (some furniture and statics) are skipped unless `bAllowStatic = 1`.

## How it works

- **What you're looking at:** the game's own view caster, the same target the HUD's activation prompt uses (`ViewCaster`, Address Library ID 82616 reads it).
- **Holding still:** while held, the object's physics is switched to keyframed, the way Papyrus `SetMotionType` does it (ID 125023 on the object's 3D); when it's placed or put back, it goes back to dynamic and is woken so it settles.
- **Moving:** every frame, the object is moved with the engine's `MoveTo` (ID 118176), the function behind Papyrus `SetPosition` and `SetAngle`, to a point along the camera's view direction.
- **Keys:** read once per frame (an SFSE task), only while the game window has focus and no menu is open.
- **Messages:** HUD notifications through the event Papyrus `Debug.Notification` sends (ID 86800).

Before doing anything, the plugin checks that every Address Library ID it uses exists for the running game version. If one is missing, it logs which and stays inactive.

The log is at `Documents\My Games\Starfield\SFSE\Logs\DropOverhaulSF.log`; `bDebugLog = 1` adds the camera and object positions at each pickup.

## Not yet

- Placing on the surface you're aiming at (needs a physics ray cast) and checking that the spot is clear.
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

`DropOverhaulSF.dll` and `.pdb` land in `build\windows\x64\releasedbg\`. With `XSE_SF_MODS_PATH` (or `XSE_SF_GAME_PATH`) set, `xmake install` copies them and the INI into place. Otherwise, make a mod with:

```
Data/SFSE/Plugins/DropOverhaulSF.dll
Data/SFSE/Plugins/DropOverhaulSF.pdb
Data/SFSE/Plugins/DropOverhaulSF.ini
```

## Credits

- Skyrim Thiago (Thiago099), [Object Manipulation Overhaul](https://www.nexusmods.com/skyrimspecialedition/mods/123664) for Skyrim ([source](https://github.com/Thiago099/Object-Manipulation-Overhaul), MIT): the design this follows.
- Jaxonz Positioner and Cobb Positioner, for the idea of keyboard-driven precise placement.

## License

GPL-3.0.
