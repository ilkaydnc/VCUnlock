# Changelog

<!-- A release shows the section whose heading matches its tag, followed by GitHub's list of changes. -->

## v2.0.1

### Fixed

- Guns fire as fast as in the original game. Each shot took one frame too long, so the M4 fired 7.5 shots a second instead of 10 and the Uzi 10 instead of 15, and the firing animation swung further than it should.
- Animations end on the same frame as in the original, so getting in and out of cars, melee moves and reloads are no longer a frame late.
- The stunt jump's slow motion is smooth with the frame rate capped at 30.
- Aiming a rifle in first person, a shot's recoil no longer makes your mouse turn lag behind.

## v2.0.0

### New

- Retail 1.0 and the Japanese release are supported, next to Steam.

### Fixed

- `VCUnlock.ini` and `VCUnlock.log` are created next to `VCUnlock.asi`, like other mods do, instead of in `Documents\GTA Vice City User Files`. The first start moves your old `VCUnlock.ini` there and keeps the settings you changed.
- With GInput, one press of the radio key changes the station once instead of skipping several.
- With the Widescreen Fix, the game is no longer stuck at 30 fps. Its own frame limiter stays off, and `gta_vc.set` keeps your setting for when VCUnlock isn't installed.

### Install

VCUnlock is now an `.asi` plugin, like most GTA mods, so it needs an ASI loader such as [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases/latest). If you have an older VCUnlock `dinput8.dll`, replace it with the loader's `dinput8.dll` and put `VCUnlock.asi` into the game folder.

### Removed

VCUnlock now does one thing: it fixes the frame rate. Everything else was removed, so VCUnlock works next to the mods you choose for these:

- Wide screen: the wider view and the HUD and aim fixes.
- Borderless and window modes in Screen Resolution.
- Draw distance up to 540 m.

If you used these, install a mod made for them next to VCUnlock.
