# Changelog

<!-- A release shows the section whose heading matches its tag, followed by GitHub's list of changes. -->

## v2.0.0

### New

- Retail 1.0 and the Japanese release are supported, next to Steam.

### Fixed

- `VCUnlock.ini` and `VCUnlock.log` are created next to `VCUnlock.asi`, like other mods do, instead of in `Documents\GTA Vice City User Files`. The first start moves your old `VCUnlock.ini` there and keeps the settings you changed.
- With GInput, one press of the radio key changes the station once instead of skipping several.

### Install

VCUnlock is now an `.asi` plugin, like most GTA mods, so it needs an ASI loader such as [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases/latest). If you have an older VCUnlock `dinput8.dll`, replace it with the loader's `dinput8.dll` and put `VCUnlock.asi` into the game folder.

### Removed

VCUnlock now does one thing: it fixes the frame rate. Everything else was removed, so VCUnlock works next to the mods you choose for these:

- Wide screen: the wider view and the HUD and aim fixes.
- Borderless and window modes in Screen Resolution.
- Draw distance up to 540 m.

If you used these, install a mod made for them next to VCUnlock.
