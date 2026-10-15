# Widescreen Fix

Tested: release of 4 Oct 2026. [Download](https://github.com/ThirteenAG/WidescreenFixesPack/releases/tag/gtavc)

## Install

1. Extract the Widescreen Fix zip into the game folder. It adds `d3d8.dll` and a `scripts` folder.
2. Do [the fix](#fix-stuck-at-30-fps) below.
3. Install VCUnlock as in the [README](../README.md#install), but put `VCUnlock.asi` into the `scripts` folder. The Widescreen Fix makes `.asi` files load from `scripts` only.

## Fix: stuck at 30 fps

The Widescreen Fix has its own frame limiter, set in **Display Setup** → **Frame Limiter**. VCUnlock replaces that menu row, so turn the limiter off once without VCUnlock:

1. If VCUnlock is installed, move `VCUnlock.asi` out of the game folder.
2. Start the game, set **Display Setup** → **Frame Limiter** to **OFF** and quit.
3. Put `VCUnlock.asi` back into `scripts`.

The game saves the setting, so this is needed only once.

Menus stay at 60 fps. To unlock them, set `MenuFrameLimit = 0` in `scripts\GTAVC.WidescreenFix.ini`.

## Japanese version

The Widescreen Fix crashes the Japanese version by itself, also without VCUnlock. There is no fix.
