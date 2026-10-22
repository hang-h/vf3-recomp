# VF1 Akira appearance prototype

Highlight Akira on the character-select screen and press **Up once** for VF1;
**Down** returns to VF3. Confirm normally. Each human player chooses independently,
and a new selection starts with VF3. Native Down navigation to VF3's lower roster
row remains available when VF1 is not selected. Fighting controls are unchanged.
The appearance uses geometry decoded from the player's own `vf.zip`. Put it
beside `vf3.zip`; on iOS use the app's `Documents/roms` folder and relaunch.
`--vf1-rom /path/to/vf.zip` chooses an explicit companion on Mac. The diagnostic
flag `--vf1-akira` bypasses selection and replaces all recognised Akiras.
No ROMs or decoded meshes are included in the repository or application.

This is an appearance prototype. VF3 still supplies the skeleton, moves,
collision and shadows. Hands use VF1's 32 source poses per side, selected
approximately from VF3 finger curl. The white headband ribbon follows VF3 tail
directions; the VF1 belt knot and tails follow the pelvis without separate cloth
physics. The face uses VF1's neutral pose;
it does not reproduce VF3 facial expressions. Both VF3 outfits currently use
the same original VF1 P1 Akira geometry and palette. Close-up/selection/LOD
layouts that do not contain the complete recognised skeleton stay original.
The selection screen keeps VF3's portrait and shows the chosen appearance in a
label; replacement is applied to the recognised full fighter in the match.
Physical-device rendering and all stages/animations have not been exhaustively
verified.

## Implementation and evidence

- `src/host/vf1_models.cpp` decodes 13 size/CRC-checked ROM members. Its 13 semantic
  parts contain 3,452 triangles, with no VF2-specific scaling. The decoder and ZIP
  reader are adapted from `hang-h/vf2-recomp` under MIT.
- `src/video/vf1_akira.cpp` converts Model 1 coordinates into VF3 local space and
  groups flat triangles by colour for the common CPU/GPU renderer. Opposite face
  normals preserve Model 1's mixed strip winding.
- The scene builder retains the source culling node, nearest matrix index and
  viewport matrix-table address. These distinguish geometry that VF3 regenerates
  at changing polygon-RAM addresses. VF3 revision D head identities are
  `0x9aaccf / node 0x509a0` and `0xa47386 / node 0x51e58` (Real3D word addresses).
- Body bindings use source-node identity plus matrix offsets from the head,
  guarded by the shared matrix table and camera. Some stages divide one fighter
  among many lighting viewports. Both original and alternate outfits have been
  traced; the alternate torso is at head-4, versus head-2 for the original.
- Measured VF3 joint lengths are torso 757.76, arm 512/512, leg 798.72/901.12.
  VF1 local X is fitted to these lengths. Model 1 local Z is reversed; head
  scale/offset are calibrated separately. Original bridge polygons and extra
  alternate-outfit cloth are suppressed; neither emulated memory nor gameplay
  code is patched. An incomplete/ambiguous binding leaves the fighter untouched.
- `src/host/akira_selection.h` reads revision D's per-player task at
  `0x108000 + player * 0x80`: phase 5 is unlocked selection, fighter byte +0x42
  identifies Akira (0 or 13). Up is consumed only while choosing Akira; Down
  cancels VF1 and is consumed until release. Other directions/fighters and all
  fighting input remain native. Rendered actor IDs at `0x1087f0 + player * 0x2000`
  map each player's choice to the standard/alternate head (0/13); ambiguous
  identities remain original. Appearance changes clear frame interpolation.
  The host publishes selection hints with the rendered frame, avoiding concurrent
  reads of emulated RAM from the UI.

The earlier REA MCP archive inspection established the VF3 ZIP identity and
member inventory, not the anatomy mapping. Retained local bundle:
`build/vf1-akira-investigation-rea.json`, inspection Evidence
`ev_e55be224182e8c3fd033c007f5119baedee71fbddaecb4326f0f93852d103863`.
The bindings above come from the renderer's runtime node/matrix traces and
original/replaced pictures. Local evidence is under `build/vf1-akira-probe`,
`build/vf1-akira-p2` and `build/vf1-akira-body`; none is required at runtime.

## Reproduce local checks

From the repository root, with a C++20 compiler and zlib:

```sh
make -j4 build/bin/vf1-models-test build/bin/akira-check build/bin/akira-selection-test build/bin/akira-selection-check
build/bin/akira-selection-test
build/bin/vf1-models-test /path/to/vf.zip
build/bin/akira-selection-check /path/to/vf3.zip /path/to/vf.zip tools/models/akira-select-vf1.txt 2100 1 1
build/bin/akira-selection-check /path/to/vf3.zip /path/to/vf.zip tools/models/akira-select-p2-vf1.txt 2400 2 1
build/bin/akira-selection-check /path/to/vf3.zip /path/to/vf.zip tools/models/akira-select-mirror-p1.txt 3500 1 1
build/bin/akira-check /path/to/vf3.zip /path/to/vf.zip tools/models/akira-guard.txt build/akira-guard
build/bin/akira-check /path/to/vf3.zip /path/to/vf.zip tools/models/akira-attacks.txt build/akira-attacks
build/bin/akira-check /path/to/vf3.zip /path/to/vf.zip tools/models/akira-p2.txt build/akira-p2 2400
build/bin/akira-check /path/to/vf3.zip /path/to/vf.zip tools/models/akira-mirror.txt build/akira-mirror 3500 2
```

The decoder check covers valid load, missing/malformed/CRC-damaged ZIPs and
transactional reload, all 64 hand poses and the ribbon/belt chains. The selection
checks cover input edges, cancel, cursor movement, the lower roster row, missing
assets, confirm/lock, P1/P2 isolation and mirror matches. The scene check takes seven snapshots 100 frames apart,
asserts the expected fighter count, unchanged transforms/draw attributes and
unaffected opponent/environment viewports, then checks disabled and unknown
layouts. It writes original/VF1 CPU pictures for visual inspection. The mirror
script also exercises fighters split across multiple lighting viewports.

Apple builds use `apple/sources.json` for both the native Xcode project and CMake.
The local Mac app was also run with SDL GPU Metal and smooth motion; the iOS
simulator used CPU 3D with Metal presentation. The original prototype also exercised menu switching on the simulator; current
selection checks use scripted joystick directions and per-player masks. The final
Mac Metal run verified a mixed VF1/VF3 Akira mirror match; the final iOS simulator
run selected VF1 using Up and reached the fight with hands, ribbon and belt. See `apple/README.md` for platform build commands.

Final local build verification: macOS Debug, iOS Simulator Debug and unsigned
iOS device Release sources were recompiled and linked against the existing
SDL/FreeType build outputs. The final full Xcode dependency-configure rerun
stalled inside local CMake before compilation, so recorded Xcode compiler/link
commands were used instead. Every translation unit depending on `r3d_scene.h`
was rebuilt after its layout change. Logs are
`build/akira-{macos,ios-simulator,ios-device}-final-incremental.log`.
The final Mac/simulator bundles were locally ad-hoc signed; a physical iPhone
still requires normal Xcode signing. This is incremental verification, not a
fresh dependency build.
