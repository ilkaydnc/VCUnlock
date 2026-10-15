<div align="center">

# VCUnlock

**Play GTA Vice City at any frame rate.**<br>
The game still runs at 30 Hz underneath, so nothing breaks.

[![Latest release](https://img.shields.io/github/v/release/ilkaydnc/VCUnlock?label=latest%20release)](https://github.com/ilkaydnc/VCUnlock/releases/latest)
[![Build](https://github.com/ilkaydnc/VCUnlock/actions/workflows/build.yml/badge.svg)](https://github.com/ilkaydnc/VCUnlock/actions/workflows/build.yml)

### [Download VCUnlock.zip](https://github.com/ilkaydnc/VCUnlock/releases/latest/download/VCUnlock.zip)

<sub>Steam, retail 1.0 and Japanese · Windows 10 and 11 · one DLL, no game files changed</sub>

</div>

> [!NOTE]
> VCUnlock is an unofficial fan project. It needs your own copy of GTA Vice City, from Steam, the retail 1.0 release or the Japanese release, and contains no game code or files. It is not affiliated with Rockstar Games or Take-Two Interactive.

<!-- video -->

## Why

Vice City was made for 30 fps. Its physics move one step per frame, so when you turn the frame limiter off, the game runs faster than it should: cars stop dead, helicopters climb too fast and boats crawl.

VCUnlock lets you play at your monitor's refresh rate while the game keeps running at 30 steps a second, exactly like the original.

It changes nothing else. Wide screen, window modes and draw distance are left to the game and to the mods you choose for them.

## Features

| | Original | VCUnlock |
|---|---|---|
| **Frame rate** | 30 fps cap | VSync, any cap or uncapped |
| **Physics and timers** | Break above 30 fps | Same at any frame rate |
| **Building fade-in** | 0.06 s at 240 fps | 0.5 s at any frame rate |
| **Settings** | Frame Limiter on or off | Frame Limiter choices and an FPS counter |

## How it works

The game world (physics, AI, scripts, timers) updates 30 times a second, the same as the original with its limiter on. Your screen can show many more frames than that, so each frame is drawn between the last two world updates. Motion looks smooth, and the game underneath is unchanged.

| Part | On screen |
|---|---|
| Peds, vehicles, objects and skeletons | Smooth |
| Doors, bonnet, boot, wheels and steering | Smooth |
| Suspension, bike forks and lean, lights | Smooth |
| Cutscene characters, props and cameras | Smooth |
| Follow and scripted cameras | Smooth, mouse look every frame |
| Particles, water waves, trees and rain | Smooth |
| Markers, bullet traces, glass and ropes | Smooth |
| Plane trails, coronas and the sun | Smooth |
| HUD flashes, radio and sound fades | Every frame, at 30 fps speed |
| Engine pitch, water scroll, swinging doors | Every frame, at 30 fps speed |
| Heli rotors, animated textures, most clothing | 30 steps a second |
| Keyboard and gamepad | Read 30 times a second |

## Tested

Same vehicle, same place and the same scripted input, recorded from the game at 240 fps:

| | VCUnlock | Original, limiter off |
|---|---|---|
| World updates per second | 30.00 | 240, one per frame |
| Game clock | Real time | 8% slow |
| Infernus coasting | Same as 30 fps | Stops dead in 1.5 s |
| Maverick climb | Same as 30 fps | 79 m up in 7 s instead of 67 |
| Speeder at full throttle | Same as 30 fps | 60 to 90 km/h instead of 120 |
| Skimmer on water | Same as 30 fps | 18 km/h instead of 60 |
| Boat in reverse | Same as 30 fps | 18 km/h instead of 14 after 2 s |

Also checked:

- Vehicle lights, wheels, doors and bike lean sit where the vehicle is drawn, on every frame.
- Every per-frame fix that was removed got a breakpoint. None fired outside a world update.
- The 30 Hz world runs against a copy of the Steam executable in offline tests.
- On retail 1.0 and the Japanese release every change goes in, the same as on Steam, and the world keeps 30 updates a second.
- With SilentPatch (Build 12.1 and its DDraw component) and Ultimate ASI Loader 9.7.4, in either load order, every VCUnlock change went in and the world kept 30 updates a second.

## Install

You need GTA Vice City from Steam, the retail CD version 1.0 or the Japanese release. Other versions, such as retail 1.1, the Rockstar Games Launcher and the Definitive Edition, are not supported. VCUnlock leaves them untouched.

To open the game folder on Steam, right-click the game and choose **Manage** → **Browse local files**. The retail game is in the folder you installed it to. Then follow the way that fits your folder.

### Without an ASI loader

Use this if you don't have an ASI loader installed.

1. Download [VCUnlock.zip](https://github.com/ilkaydnc/VCUnlock/releases/latest/download/VCUnlock.zip).
2. Extract `dinput8.dll` into the game folder, next to `gta-vc.exe`.
3. Start the game.

To uninstall, delete `dinput8.dll`.

### With an ASI loader

Use this if you already have an ASI loader installed, for example for SilentPatch. The loader is often a `dinput8.dll` of its own: keep it and add VCUnlock as one more `.asi` file.

1. Download [VCUnlock-ASI.zip](https://github.com/ilkaydnc/VCUnlock/releases/latest/download/VCUnlock-ASI.zip).
2. Extract `VCUnlock.asi` into the folder that holds your other `.asi` files. That is usually the game folder, or its `scripts` or `plugins` folder.
3. Start the game.

To uninstall, delete `VCUnlock.asi`.

## Settings

Both are in **Options** → **Display Setup**:

| Setting | Values |
|---|---|
| Frame Limiter | VSYNC, 30, 60, 120, 144, 165, 240, OFF |
| Show FPS | ON, OFF |

<table>
  <tr>
    <th>Original</th>
    <th>VCUnlock</th>
  </tr>
  <tr>
    <td colspan="2"><b>Display Setup</b>: Frame Limiter and Show FPS</td>
  </tr>
  <tr>
    <td><img src=".github/media/menu/display_original.jpg" width="376" alt="Display Setup, original"></td>
    <td><img src=".github/media/menu/display_vcunlock.jpg" width="376" alt="Display Setup, VCUnlock"></td>
  </tr>
</table>

Settings are saved in `Documents\GTA Vice City User Files\VCUnlock.ini`, which has a few extra options:

| Key | Default | What it does |
|---|---|---|
| `RefreshRate` | `0` | Refresh rate in exclusive fullscreen. `0` uses the highest one |
| `FPSLimit` | `0` | Any frame rate cap, also one that isn't in the menu |
| `Log` | `0` | Write `VCUnlock.log` |

Older versions also had wide screen, window, draw distance and audio options. They were removed so that VCUnlock works next to other mods for those. A `VCUnlock.ini` from an older version is rewritten with the settings above, and the ones you changed are kept.

## Troubleshooting

**Nothing changed in the game.** Make sure `dinput8.dll` sits next to `gta-vc.exe`, or `VCUnlock.asi` next to your other `.asi` files. Set `Log=1` and check `VCUnlock.log`. VCUnlock checks the game's code before every change and skips anything that doesn't match. If the log says "unsupported executable", your game version is not one VCUnlock knows.

**Another mod uses `dinput8.dll`.** That is usually an ASI loader: use `VCUnlock.asi` instead. Remove other frame rate mods first.

**Antivirus warning.** Every release is built by [GitHub Actions](https://github.com/ilkaydnc/VCUnlock/actions/workflows/build.yml) from this source. You can check your download against `SHA256SUMS.txt` on the release page.

**Found a bug?** Open an [issue](https://github.com/ilkaydnc/VCUnlock/issues) and attach `VCUnlock.log` and `VCUnlock.ini`.

## Building

You need Visual Studio 2022 or newer with the **Desktop development with C++** workload.

```bat
build.bat
```

The DLL is written to `build\dinput8.dll`, with a copy named `build\VCUnlock.asi`. Pushing a tag such as `v1.0.0` publishes a release.

## Special thanks

<img src=".github/media/twitch.svg" width="14" alt="Twitch"> **[Root1X](https://www.twitch.tv/root1x)** made VCUnlock with me, from the first idea to the release.

<img src=".github/media/twitch.svg" width="14" alt="Twitch"> **[YBurakD](https://www.twitch.tv/yburakd)** played build after build and gave the feedback that shaped the fixes.

## Licence

VCUnlock is released under the [MIT Licence](LICENSE).
