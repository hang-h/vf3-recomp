# VF3 Recomp on iPhone, iPad and Mac

Open `VF3Recomp.xcodeproj` at the repository root. Choose **VF3 iOS** and an iPhone,
iPad or iOS Simulator, then Run. For a physical device, set your developer Team and
a unique Bundle Identifier in Signing & Capabilities. The **VF3 macOS** scheme also
builds the original desktop game. Minimum versions are iOS 16 and macOS 15.

## Build requirements

Install Xcode and CMake 3.24 or newer. The first build downloads pinned SDL 3.4.18
and FreeType 2.14.3 sources, verifies their SHA-256 hashes and builds static libraries.
The dependency targets detect Homebrew CMake or a checkout-local installation:

```sh
python3 -m venv build/tooling
build/tooling/bin/python -m pip install 'cmake==4.4.4'
```

If `xcode-select -p` points to CommandLineTools, select Xcode in Xcode Settings →
Locations, or set `DEVELOPER_DIR` to the installed Xcode's `Contents/Developer`
when running the commands below.

```sh
xcodebuild -project VF3Recomp.xcodeproj -scheme 'VF3 iOS' \
  -destination 'generic/platform=iOS Simulator' -configuration Debug build
xcodebuild -project VF3Recomp.xcodeproj -scheme 'VF3 iOS' \
  -destination 'generic/platform=iOS' -configuration Release CODE_SIGNING_ALLOWED=NO build
xcodebuild -project VF3Recomp.xcodeproj -scheme 'VF3 macOS' \
  -destination 'platform=macOS' -configuration Debug build
```

The unsigned device command checks compilation; installing on a device requires signing.
Dependencies are cached per platform, configuration and architecture in DerivedData.
Set `VF3_CMAKE_EXECUTABLE` to override CMake or `VF3_BUILD_JOBS` to limit dependency jobs.

## Supply your ROM

Use your own complete MAME **vf3 / Revision D** set, named **vf3.zip**.
Team Battle (`vf3tb`) is not supported. No game ROM is included.

1. Launch the app once to create its Documents folders.
2. In Files, open **On My iPhone/iPad → VF3 Recomp → roms**.
3. Copy `vf3.zip` there, keeping it compressed, then return to the app.

The app detects `Documents/roms/vf3.zip` or `Documents/vf3.zip` automatically.
The import screen also provides a Files picker for iCloud Drive and other providers.
Every ROM is validated by the upstream loader. Picker imports are staged and checked
before atomically replacing the saved ZIP; failed imports preserve the previous file.
The archive limit is 512 MiB. Settings, logs, backup RAM and EEPROM live in the app's
Library area. Replacing a ROM after the game starts requires relaunching.

## Optional VF1 Akira appearance

Supply your own complete MAME **vf** set as **vf.zip**. On iPhone/iPad, copy it to
**Files → On My iPhone/iPad → VF3 Recomp → roms**, alongside `vf3.zip`. On Mac,
place it beside the loaded `vf3.zip`, or pass `--vf1-rom /path/to/vf.zip`.
The Files import button is for `vf3.zip`; copy the optional `vf.zip` using Files.

On the character-select screen, highlight **Akira** and press **Up once** to
choose VF1. Press **Down** to return to VF3, then confirm with the usual button.
Each player chooses independently; the choice lasts for that selection/playthrough.
The screen shows the selected appearance. Down with VF3 already selected retains
normal roster navigation. During a fight, all directions work normally.
No menu setting is needed or saved. After adding `vf.zip`, relaunch the app;
a missing or invalid companion leaves VF3 and its original controls playable.
Only Akira's appearance changes; VF3 still provides the moves and game rules.
The prototype uses a neutral VF1 face and VF1 hands, headband ribbon and belt.
Hands follow VF3's finger curl approximately; the belt tails follow the pelvis
without a separate cloth simulation. VF3 shadows remain in use.
Both VF3 outfits use the same VF1 appearance. See [model notes](../tools/models/README.md)
for the supported layout and local verification commands.
No VF1 ROM or extracted model data is distributed with the app.
For diagnostic runs, `--vf1-akira` forces the appearance for all recognised Akiras,
bypassing per-player selection.

## iOS behavior

The iOS target uses the PowerPC interpreter, with no SLJIT code or JIT entitlement.
The original SDL GPU renderer uses Metal, with upstream CPU fallback. Defaults are
1x resolution, antialiasing off and smooth motion off. The arcade boot remains visible.
Interpreter speed, audio and graphics must be checked with a real ROM on the device.

Landscape touch controls provide an eight-way stick, G/P/K, **E (Escape)**, the four
attack combinations, Start, Coin and Menu. They hide when Player 1 has a controller.
In menus, the stick navigates; G goes back, P selects, K clears a binding and Start
resumes. Native UIKit controls use the window's safe area independently of game aspect
ratio. Backgrounding releases input, pauses audio and emulation, and saves settings
and NVRAM; returning resumes the app.

## Maintain and verify

`apple/sources.json` is shared by the native Xcode project and CMake. After changing
source lists, run `python3 apple/generate-xcode-project.py`. The generator preserves
local signing settings and existing shared scheme launch options.

CMake can also build the apps and ROM-free regression checks:

```sh
cmake -S apple -B build/apple-macos -DCMAKE_BUILD_TYPE=Release
cmake --build build/apple-macos --parallel 8
ctest --test-dir build/apple-macos --output-on-failure
```

Checks cover malformed/oversized ROM imports, preservation and staging cleanup,
eight-way movement, combination-button overlap/release, Escape, Coin and lifecycle
input reset. They do not establish actual game compatibility or device performance.

The checked-in iOS icon is generated from upstream's VF3 geometry and teal palette;
regenerate with `python3 apple/make-ios-icon.py` in an environment with Pillow.

## Validation on 2026-10-22

With Xcode 27.0.1 RC (iOS 27.1 SDK), the native project passed arm64 iOS Simulator
Debug, unsigned iPhone Release and arm64 macOS Debug builds. The iOS entry point uses
`SDL_main`; the iOS source list and build objects exclude SLJIT and the PowerPC
recompiler. Both CTest checks passed.

On the iOS 26.5 iPhone 17 Pro simulator, the importer rendered correctly, created
`Documents/roms`, detected a ZIP added there while running, and rejected an incomplete
set with an error message. The temporary test ZIP was removed afterwards.

A subsequently supplied Revision D ROM passed validation and booted through the
attract sequence. Scripted Start/Punch inputs reached an Akira versus Wolf match;
random fighting inputs continued play, and Escape opened the pause menu at frame
2400. Screenshots confirmed the native G/P/K/E controls over gameplay and the menu's
Back/OK/Clear/Resume labels. Backgrounding wrote backup RAM and EEPROM; foregrounding
returned to the same process. Sound data was queued, but listening/audio quality and
hands-on native touch interaction were not tested.

On this simulator, SDL 3.4.18 rejects its Metal device at the Apple3 GPU-family check.
The app correctly falls back to CPU 3D with Metal presentation. At 1x on an M4 Pro,
logs showed about 57.5 emulated frames/s and roughly 47–54 displayed pictures/s during
the observed scenes. These are simulator measurements, not iPhone performance.
The Mac build separately completed 1800 frames using the PowerPC interpreter and
SDL GPU Metal, passing upstream's startup GPU/CPU image comparison tolerance.
iPhone/iPad Metal 3D rendering and physical-device performance remain unverified.

iOS uses a borderless UIKit scene with landscape orientation. The desktop Fullscreen
menu and shortcuts are excluded; previously saved fullscreen settings are cleared
on launch. The importer-to-game handoff synchronizes the window and renderer with
the scene bounds, and native controls are constrained to the renderer view.

For a Debug simulator launch, set `SIMCTL_CHILD_VF3_TEST_WINDOW_GEOMETRY=1` with
`xcrun simctl launch`. The opt-in check injects stale portrait bounds and verifies
recovery of the window, SDL size, Metal drawable and native controls, including
nonoverlapping Back/Resume buttons. It also checks that desktop fullscreen is off.
This is a simulated geometry regression check, not a physical-device verification.
The check passed at 2622x1206 pixels; a launch with `fullscreen = 1` returned to
`fullscreen = 0`, and F11 left the landscape layout intact. The Display menu no
longer contains Fullscreen. Simulator Debug, unsigned device Release and macOS
Debug builds passed after this change. Evidence: `build/ios-window-fault-check.log`,
`build/ios-geometry-display.png` and `build/ios-geometry-menu.png`.

Local evidence (ignored by Git): `build/ios-rom-play.png`, `build/ios-rom-menu.png`,
`build/ios-rom-check.log`, and `build/rom-check-macos/log.txt`.

## Source and attribution

Based on [mr-finster/vf3-recomp](https://github.com/mr-finster/vf3-recomp), commit
`d30b722eb492fd05edabc4b6ad428eff972f7c2c`, preserving the upstream Git history.
Apple project generation, native controls and the importer are adapted from
[hang-h/vf2-recomp](https://github.com/hang-h/vf2-recomp) under MIT. Touch input and
its checks originated in [soulafein83/vf2-recomp](https://github.com/soulafein83/vf2-recomp),
commit `cd8b389be28fd486ee78adeb45d0ce5e1eeb6653`. The VF3 icon geometry is by Mr Finster,
CC BY 4.0. Bundled `LICENSES.txt` retains upstream and dependency notices.
