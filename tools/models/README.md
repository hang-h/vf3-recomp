# VF1 / VF2 roster appearances

On the character-select screen, **Up cycles the top row** and **Down cycles the
bottom row**. The opposite direction always navigates to the other row.
**Akira, Pai, Lau, Wolf, Jeffry, Kage, Sarah and Jacky** cycle
**VF3 → VF1 → VF2 → VF3**; **Shun and Lion** cycle **VF3 → VF2 → VF3**.
Aoi and Taka stay native VF3. Held input advances only once, including when a
navigation press is held across a row change. Confirm normally. Each human player
chooses independently; a different character or new selection starts with VF3.
Fighting controls are unchanged.

The appearances are decoded from the player's own `vf.zip` and `vf2.zip`.
Put them beside `vf3.zip`; on iOS use the app's `Documents/roms` folder and relaunch.
Unavailable/invalid companions are skipped. `--vf1-rom /path/to/vf.zip` and
`--vf2-rom /path/to/vf2.zip` choose explicit companions on Mac. Diagnostic flags
`--vf1-akira` / `--vf2-akira` bypass selection and replace all recognised Akiras
(VF2 takes precedence if both flags are provided).
No ROMs or decoded meshes/textures are included in the repository or application.

VF3 still supplies the skeleton, moves, collision and shadows. Each source has
32 native hand poses per side, selected approximately from VF3 finger curl.
Akira headband ribbons use the source geometry and follow VF3 tail directions; VF2's
two five-segment ribbons are fitted to VF3's three cloth bones. VF1 belt tails
follow the pelvis; VF2's two two-segment belt tails follow the torso, without
separate cloth physics. Faces now use original source expression frames: selection,
hit reactions, recovery and results follow native VF3 actor state. High-detail
speech follows VF3's actual jaw morph and blend, retargeted to the source mouth
opening. This preserves the original facial assets but is an aperture approximation,
not a phoneme-for-phoneme transplant of the different facial rigs. Source eyes and
brows move with their original expression scripts; independent VF3 eye/brow morphs
are not transplanted. VF1 Kage retains the original concealed mask policy; VF2 Kage
can animate his visible eyes while keeping the mask.
Other fighters currently use rigid source hair/accessory chains, without their
original cloth physics. VF2 Jacky's tufts use the upward rest direction indicated
by their native accessory force, rather than the hanging-braid fallback. Both VF3 outfits use the source game's P1 geometry and palette.
Unknown/ambiguous skeleton layouts stay original. Visible, recognised limbs are
replaced even when the camera culls the feet in a victory close-up. The selection
screen changes both the 3D head and the original ROM portrait in the corresponding roster
cell, and shows the chosen appearance in a label. The tall source portraits are
fitted into VF3's landscape cell without stretching. Physical-device rendering and all stages/animations
have not been exhaustively verified.

## Roster extension

The shared canonical order is Akira, Pai, Lau, Wolf, Jeffry, Kage, Sarah, Jacky,
Shun, Lion. `src/host/fighter_roster.h` maps these to revision D cursor/actor IDs
`0,6,4,7,5,3,2,1,8,10`; an alternate outfit adds 13. VF1 provides the first eight
entries and VF2 all ten. The two companion decoders accept this index as their
optional fourth `load` argument and validate ROM member sizes/CRCs at runtime.
No decoded assets are generated during the build.

`src/video/fighter_layouts.h` records both VF3 outfits' exact source nodes and
joint dimensions from local native runtime captures. The generic adapter fits
source limb lengths and torso joints to these skeletons; original face, hand and
accessory meshes are removed only through the matching outfit's explicit node
set. Shadow nodes are excluded. Akira retains its previously verified adapter.
`fighter_appearances.h` loads other fighters on demand and retains two converted
models per companion, enough for both players without decoding the whole roster.
Changing fighter clears that player's previous choice; the post-KO choice remains
bound to the same actor pair through the result sequence.

The source decoders were checked for all 8 VF1 and 10 VF2 fighters, including
hands, source joints, portraits and accessory chains. VF2's 180,224 texture blocks
per fighter matched native neutral-state uploads. Local evidence resides under
`build/vf1-roster`, `build/vf2-roster-probe`, `build/roster-selection` and
`build/roster-layouts`; ROM-derived captures remain ignored.

Run the optional local-ROM roster regressions with:

```sh
make -j4 build/bin/akira-selection-test build/bin/fighter-appearances-test build/bin/roster-check
build/bin/akira-selection-test
build/bin/fighter-appearances-test /path/to/vf.zip /path/to/vf2.zip
build/bin/roster-check /path/to/vf3.zip /path/to/vf.zip /path/to/vf2.zip build/roster-render
```

`roster-check` captures both selection previews and three fight poses for all
fighters (an optional final index selects one). It checks independent outfit
masks, original-face removal, shadow preservation, unchanged scene transforms
and emulated RAM, and disabled/unknown-layout guards. The cache test covers
scene lifetime after eviction, reload fidelity, failed replacement ROMs and
mixed VF1/VF2 player ownership. All 90 source/frame combinations passed. Extended
native pose audits also cover changing detail levels and reaction-face node sets.
An actual Pai VF2 selection/defeat run retained the appearance for 165 sampled
inactive-player result scenes and cleared it when attract mode began. Pai victory
close-ups and progression into the next fight also retained the imported model.
The final macOS Metal app was exercised with a VF1/VF2 Pai mirror match and a
VF2 Kage selection/fight; the iOS simulator displayed VF2 Shun after one cycle press.
macOS, simulator and unsigned device apps were incrementally compiled/linked
using the recorded Xcode commands and cached dependencies. Logs/screenshots are
under `build/roster-selection`; the device build has not been tested on hardware.
No test ROM or image is committed.

## Akira implementation and evidence

- `src/host/vf1_models.cpp` decodes size/CRC-checked ROM members. Akira's 13 semantic
  parts contain 3,452 triangles, with no VF2-specific scaling. The decoder and ZIP
  reader are adapted from `hang-h/vf2-recomp` under MIT.
- `src/host/vf2_models.cpp` reads size/CRC-checked VF2 ROMs, decodes the
  13 body parts, 64 native hand poses, two headband ribbons and two belt tails.
  `vf2_texture_decode.h` reconstructs the three raw and nine Huffman-compressed
  Akira base textures directly from ROM. All 180,224 packed 2x2 blocks matched
  a native VF2 frame-1700 texture-sheet dump; ASan/UBSan and truncated-stream
  probes passed. This establishes the texture data, not pixel-identical rendering.
  For the shared flat-colour mesh path, UV faces are adaptively subdivided to
  depth three for the face/accessories, two for the body and one for the 64
  cached hand poses, then sampled and stripped of transparent samples. Eye and
  mouth cutouts use a finer grid (0.75-texel target, at most five levels) so thin
  pupils and lips cannot disappear between otherwise uniform colour samples.
  On those facial cutouts, native Model 2 bilinear index/coverage filtering runs
  before the luminance lookup. Boundary triangles are clipped to the 50%
  coverage contour instead of retaining or discarding the whole triangle.
  Other body textures keep their existing sampling. The resulting body has
  53,748 triangles and each hand pose has 760 triangles.
  These cutouts are coplanar with skin in the original head. Model 2's painter
  order preserves them, but Real3D's depth buffer and colour batching do not.
  Akira's existing head descriptor `0x19c`, alpha-cutout palettes 1/2 are lifted
  outward by 0.0002 source units (0.4096 destination units), opposite the inward
  ROM normal. Other fighters retain source painter order for repeated head
  triangles using the same offset per layer. All head alpha overlays use
  filtered cutout coverage; eyes/mouth retain their finer subdivision budget.
  Kage's opaque palette-92 mask comes after both skin and mouth, so it receives
  three offset layers even where its triangulation differs. This removes skin
  and mouth showing through the mask without altering limbs or VF1 geometry.
  Animated source expression aliases, eye/mouth texture uploads and jaw geometry
  are now decoded alongside the exact neutral head; see the expression evidence
  below for native script mapping and the separate speech retargeting lookup.
  Fine texture edges are therefore approximate. Colours use ROM luma/colour
  curves with a fixed native first-fight palette environment (offset 64, slope
  37, gain 128 and special-curve LUT offset -52), followed by VF3 lighting;
  dynamic VF2 stage colour translation is not reproduced.
  Each fighter selects its own native skin/costume and high-luma facial/hair
  curves: main-data `0x101000 + native_actor*0x300` and
  `0x105800 + native_actor*0x2a0`, respectively (i960 `0x754..0x9e8`).
  All 720 channel entries per fighter matched the ten native runtime captures
  exactly. These tables are essential for Shun's white beard and Kage's mask;
  sharing Akira's curves would incorrectly use his mouth and gi colours.
- `src/video/vf2_akira.cpp` loads the VF2 source into the shared adapter with
  uniform 2048 scaling and reversed local Z. VF2's 0.25 arm and 0.39/0.44 leg
  segments match VF3's 512 and 798.72/901.12 lengths. Distinct mesh cache keys
  allow VF1 and VF2 appearances in the same mirror match.
  VF2's lower torso is extended toward the pelvis by up to 0.03 source units
  (61.44 VF3 units), fading to zero at local X=0.16. This covers the waist seam
  during VF3 torso bends without enlarging the shoulders or changing gameplay.
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
  code is patched. An unknown/ambiguous or misplaced binding leaves the fighter untouched.
  Off-screen parts may be absent; every present part must retain its expected
  node and matrix offset. This avoids reverting to VF3 in victory close-ups.
  Head-only selection layouts require an explicit preview mask from the host.
  They use the original/alternate head and cloth nodes at offsets +4/+6/+8,
  independently of the battle actor IDs, which are not yet assigned at selection.
  The separately animated VF3 face at node `0x50990` / `0x51e48` must also
  be removed: its matrix offset is +1 in selection and +25 in fights. Fight
  offset +1 is a shadow and stays intact. Regression checks assert that no
  selected VF3 animated face remains and that the unselected face is unchanged.
- `src/host/akira_selection.h` reads revision D's per-player task at
  `0x108000 + player * 0x80`: phase 5 is unlocked selection, fighter byte +0x42
  identifies the fighter (Akira is 0 or 13). The outward direction cycles an
  imported appearance: Up for the top row and Down for the bottom row. The
  inward direction, unsupported fighters and all fighting input remain native. Rendered actor IDs at `0x1087f0 + player * 0x2000`
  map each player's choice to the standard/alternate head (0/13); ambiguous
  identities remain original. Appearance changes clear frame interpolation.
  At the final KO the player task becomes inactive before the defeat replay and
  `You Lose` pose finish. The selected appearance stays bound to the last battle
  actor pair through that result sequence. A new selection, changed actor pair,
  missing companion or host reset clears this binding, so attract-mode fighters
  do not inherit the defeated player's appearance.
  The host publishes selection hints with the rendered frame, avoiding concurrent
  reads of emulated RAM from the UI.

- Source portraits are decoded directly from the companion ROMs: VF1's 56×64
  map at `0x2ff5ec`, tiles at `0x2fef8c`, palette at `0x2ff5cc`; VF2's 48×88
  map at main-data `0xa712fc` (row stride 124), tiles at `0x84f4d4`, palette at
  `0x800ee0`. VF2's 63 tiles, 66 map entries and 16 palette entries matched the
  native selection screen's RAM. `akira_portrait.h` composites only the selected
  roster cell into the host's upper picture layer; emulated tile RAM is untouched.

The earlier REA MCP archive inspection established the VF3 ZIP identity and
member inventory, not the anatomy mapping. Retained local bundle:
`build/vf1-akira-investigation-rea.json`, inspection Evidence
`ev_e55be224182e8c3fd033c007f5119baedee71fbddaecb4326f0f93852d103863`.
The bindings above come from the renderer's runtime node/matrix traces and
original/replaced pictures. Local evidence is under `build/vf1-akira-probe`,
`build/vf1-akira-p2` and `build/vf1-akira-body`; none is required at runtime.

VF2 archive inspection via REA MCP is retained locally in
`build/vf2-akira-rea.json` (inspection evidence
`ev_a6863f58b809b0c639fe8cf41af45964a4d68998903a2df2fd4408004580e446`).
The inspected ZIP SHA-256 is
`4427d2e58609c435fa42e06e62cba8a33a1a613fc5c7bd21a2d9cfb1ece4a790`.
Texture proof and native colour/upload traces are in
`build/vf2-texture-probe/decode-evidence.json` and its sibling local files.
No probe output or extracted ROM data is required or shipped at runtime.

## Reproduce local checks

From the repository root, with a C++20 compiler and zlib:

```sh
make -j4 build/bin/vf1-models-test build/bin/vf2-models-test build/bin/akira-check build/bin/akira-selection-test build/bin/akira-selection-check build/bin/akira-layout-check
build/bin/akira-selection-test
build/bin/vf1-models-test /path/to/vf.zip
build/bin/vf2-models-test /path/to/vf2.zip
build/bin/akira-selection-check /path/to/vf3.zip /path/to/vf.zip tools/models/akira-select-vf1.txt 2100 1 1
build/bin/akira-selection-check /path/to/vf3.zip /path/to/vf.zip tools/models/akira-select-p2-vf1.txt 2400 2 1
build/bin/akira-selection-check /path/to/vf3.zip /path/to/vf.zip tools/models/akira-select-mirror-p1.txt 3500 1 1
build/bin/akira-selection-check /path/to/vf3.zip /path/to/vf.zip tools/models/akira-select-vf2.txt 2100 0 0 /path/to/vf2.zip 1 1
build/bin/akira-selection-check /path/to/vf3.zip /path/to/vf.zip tools/models/akira-select-cycle-vf3.txt 2100 0 0 /path/to/vf2.zip 0 0
build/bin/akira-selection-check /path/to/vf3.zip /path/to/vf.zip tools/models/akira-select-p2-vf2.txt 2400 0 0 /path/to/vf2.zip 2 1
build/bin/akira-selection-check /path/to/vf3.zip /path/to/vf.zip tools/models/akira-select-mirror-mixed.txt 3500 1 1 /path/to/vf2.zip 2 1
build/bin/akira-selection-check /path/to/vf3.zip /path/to/vf.zip tools/models/akira-defeat-vf1.txt 6000 1 1
build/bin/akira-selection-check /path/to/vf3.zip /path/to/vf.zip tools/models/akira-defeat-vf2.txt 6000 0 0 /path/to/vf2.zip 1 1
build/bin/akira-selection-check /path/to/vf3.zip /path/to/vf.zip tools/models/akira-defeat-vf1.txt 9200 0 0
build/bin/akira-check /path/to/vf3.zip /path/to/vf2.zip tools/models/akira-attacks.txt build/akira-vf2 2100 1 vf2
build/bin/akira-check /path/to/vf3.zip /path/to/vf.zip tools/models/akira-guard.txt build/akira-guard
build/bin/akira-check /path/to/vf3.zip /path/to/vf.zip tools/models/akira-attacks.txt build/akira-attacks
build/bin/akira-check /path/to/vf3.zip /path/to/vf.zip tools/models/akira-p2.txt build/akira-p2 2400
build/bin/akira-check /path/to/vf3.zip /path/to/vf.zip tools/models/akira-mirror.txt build/akira-mirror 3500 2
```

Victory/preview regressions use `akira-layout-check`:

```sh
build/bin/akira-layout-check /path/to/vf3.zip /path/to/vf.zip tools/models/akira-select-vf1.txt build/akira-preview 1580 1700 1 1
build/bin/akira-layout-check /path/to/vf3.zip /path/to/vf.zip tools/models/akira-victory.txt build/akira-victory 9340 9560 1 0
```

The victory test reproduces the camera-culling failure: at frame 9360 the left
foot is absent, although the selected player/outfit remains unchanged. Twelve
victory frames and eleven P1/P2 preview frames passed, plus four simultaneous
head-preview frames. A mixed VF1/VF2 challenger selection also keeps the already
confirmed P1 appearance. The normal-fight VF1
frame-2400 image remained byte-identical. Final Mac Metal captures cover the
VF1/VF2/VF3 preview cycle; iOS simulator verification covers the VF2 preview.
Evidence is retained locally under `build/akira-victory`, `build/akira-portraits`
and `build/akira-lifecycle`.

The decoder check covers valid load, missing/malformed/CRC-damaged ZIPs and
transactional reload, all 64 hand poses and the ribbon/belt chains. The selection
checks cover input edges, cancel, cursor movement, the lower roster row, missing
assets, confirm/lock, P1/P2 isolation and mirror matches. The scene check takes seven snapshots 100 frames apart,
asserts the expected fighter count, unchanged transforms/draw attributes and
unaffected opponent/environment viewports, then checks disabled and unknown
layouts. It writes original/VF1/VF2 CPU pictures for visual inspection. The mirror
script also exercises fighters split across multiple lighting viewports.

Apple builds use `apple/sources.json` for both the native Xcode project and CMake.
The final Mac app ran a mixed P1 VF1 / P2 VF2 Akira mirror match using SDL GPU
Metal and smooth motion. All three Up-cycle labels were captured separately.
The final iOS simulator used CPU 3D with Metal presentation, selected VF2 with
two Up presses and reached the fight with native hands/ribbons/belt. Seven
snapshots each of VF2 guard, attack, P2 and mirror scripts passed scene invariants; VF1's
seven regression pictures remained byte-identical after the shared adapter change.
Local evidence is in `build/vf2-selection-checks`, `build/vf2-selection-mac`,
`build/vf2-selection-ios` and `build/vf2-akira`. See `apple/README.md` for build
commands.

An M4 Pro probe measured VF2 loading at 65 ms and the converted body/64-hand
vertex cache at about 28.6 MiB. A single 496×384 CPU frame took 10.3 ms versus
3.2 ms for the original scene; this is one desktop sample, not an iPhone
performance guarantee. The final Mac Metal mixed-match run maintained about
57.5 emulated frames/s. Physical-device performance remains unverified.

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

Selection overlay / VF2 waist follow-up: source-only VF1/VF2 previews were
checked for both outfits, including explicit animated-face removal and head
shadow preservation. Local screenshots and logs are under `build/akira-face-fix`;
waist fitting comparisons are retained under `build/vf2-akira/waist-fit-evidence.json`.

## Native facial-expression evidence

VF1's character record `+0x72` points to nine head-component lists: four fixed
pieces, separate left/right eye and brow lists, and the mouth. V60 `0xff06ac`
reads seven scripts from head-data `+0x24`: a duration byte, component-indexed
frame sequences, and an `ff` terminator. All eight fighters have seven 16-frame
scripts. The decoder preserves those sequences, including asymmetric eyes and
the different recovery pacing of Pai/Sarah, and deduplicates full heads by
their source descriptors. Frame zero is exactly the previous neutral head.

The native damage entry at `0xfeb65e` starts script 3. Native action-script
opcode `f8` (`0xfedd97`) selects script 1 for Pai's winning action `0x12e`, and
script 5 for Sarah's `0x15d`/`0x15e` and Jacky's `0x12b` winning actions.
The original victory move table (`0xfe30af..0xfe30d6`) confirms ordinary
victory scripts `[1,1,5,5,1,5,5,5]` in shared-roster order; alternate source
victories all use 5. The adapter uses the ordinary choice as its result fallback;
VF3's active speech controller takes priority. The bounded source parser and
all 16 command traces are in `victory-scripts.{py,json,txt}`.
Scripts 2/4/6 are return transitions; `0xfeacfd` advances an odd script ID to
its matching even ID. Script 7 is the character-selection expression, played
once and held by `0xfce81a..0xfce897`. These IDs are not universal emotion
labels: script 1 smiles while script 5 opens the mouth/shouts. No independent
VF1 looping blink timer has been established; the closed-eye source geometry
is available separately. Native masked Kage forces head components 1..8 to
frame zero (`0xff070b..0xff073f`), so his mask and visible eyes remain static.

VF3 revision D's actors are at `0x108780 + player*0x2000`. Native fields are
HP `+0x56` (16-bit), pending/current action `+0x44`/`+0x48` (32-bit), motion ID
`+0x4c`, and motion frame `+0x4e` (16-bit). PPC `0x10474..0x104bc` subtracts
damage, clamps HP at zero and sets actor flag `0x04000000` on KO. Runtime HP
changes confirm action categories `0x0a` and `0x0e` as hit reactions. Category
`0x0b` also covers recovery and cannot be treated as damage generally; category
zero plays a supplied motion ID and includes both attacks and victory poses.

The round resolver stores winner/loser actor pointers at `0x1061c4`/`0x1061c8`
(`0x29bdc`/`0x29be0`). Actual victory poses require native main phase
`read8(0x102008)==7` and subphase `read8(0x10200b)` equal to `0x0e` or `0x0f`;
`0x0d` is the finishing-move replay. The sampler also checks that the pointers
are the two distinct actor bases. Result pointers remain populated after the
pose, so pointer identity alone is insufficient. Some inactive actors retain
motion frame 1 throughout the result scene: body-frame progression alone does
not prove that a separate facial controller has advanced. RAM byte/halfword
reads use the emulator's `address^3` / `address^2` storage convention.

Local, ignored evidence is under `build/vf1-expressions`: native V60/PPC
disassembly, decoded script metadata in `source.json`, native event logs,
`vf3-defeat-globals.csv`, and `atlas/sheet-{0,1}.png` showing all eight VF1
heads at neutral/closed/script endpoints. Decoder tests cover all native
timelines, neutral preservation, source-mask policy, recovery deduplication,
CRC rejection and transactional reload; normal and ASan/UBSan runs passed.
The source banks preserve original geometry/timing. Cross-game action-to-face
selection is a separate retargeting policy, not evidence that VF1 and VF3
share animation IDs or controller formats.

VF2 2.1 uses ten linked facial scripts through Main-data `0x6278`; i960
`0x4b604..0x4b7ac` loads and advances their 20-byte nodes. Each node stores
its frame duration, independent mouth/eye target phases (0..1000), sequence
pointers and a next-node pointer. The decoder reproduces integer interpolation,
source jaw descriptor selection and native texture-frame pairing. Script 3
retains its lead-in and actual next-node loop; scripts 8/9 retain the native
6-frame closing and 15-frame reopening transitions. Source script IDs are
preserved rather than being assigned universal emotion names.

The source Huffman streams provide live mouth patches at `(128,768)` and eye
patches at `(0,768)` in texture sheet 1. Native Lion frame 1500 matches all
3,072 packed blocks in those regions using mouth group 81/frame 4 and eye
group 82/frame 6; the corresponding decoded head matches all 30,460 triangles
of bank frame 35. This includes the native colours, cutout contours and
coplanar painter-order corrections used by the neutral heads.

Across the ten fighters the native scripts produce 580 distinct descriptor /
mouth / eye recipes. Independent static, mouth and eye components are shared
between jaw aliases and compressed in memory after loading. No generated ROM
asset or disk cache is required, and no emulator, ROM file handle, atlas or
mutable decoder is retained by the frame callback. On the local development
Mac the compressed bank uses 1.75–6.87 MB per fighter; source loading takes
140–327 ms and frame assembly 0.62–2.01 ms. These are desktop measurements,
not an iOS performance guarantee. All 580 output hashes match the original
full-head decoder exactly; Akira's existing 53,748 body triangles remain
unchanged. All-roster decoding, every expression frame, native loops/recovery,
invalid indices and transactional failures pass normally and under ASan/UBSan.

Ignored evidence is in `build/vf2-expressions`: `face-controller-dis.txt`,
`face-callers.txt`, `patch-check.cpp`, `recipe-check.cpp`, `metrics-compiled.json`,
`memory-compiled.txt`, the before/compiled hash lists, and
`metrics-final.json`, `memory-final.txt`, and `atlas/roster-sheet-{0,1,2}.png`. The atlas displays source scripts 1/3/7/8
across all ten characters, including VF2 Kage's animated eyes above his mask.

A native Lion run with a controlled opponent KO at frame 1700 confirms the
source event mapping: selection confirmation at frame 1374 (mode 7/subphase 5)
starts script 7; damage at frame 1697 (HP 146, fight mode 9/subphase 9) starts
script 8. The winning pose at frame 1952 (mode 9/subphase 17) starts script 3
and follows its linked nodes, then motion 25 at frame 2012 selects recovery
script 2. Script 9 reopens the eyes during subphase 16. The exact node writes
are recorded in `native-events.log`; a face texture match alone does not prove
which game event triggered it, because uploaded patches can outlive a script.

The speech aperture lookup is deliberately separate from those native scripts.
It exposes 17 normalized opening samples with neutral eyes, using original
mouth textures and associated jaw descriptors. Script 1 is not uniformly
monotonic: Akira opens twice, while Wolf/Jeffry/Kage/Shun open then close.
The verified opening passes are Akira neutral then frames 10..19, Jeffry
neutral then 13..19, Wolf 0..9, Kage/Shun 0..5, and the full sequence for the
others. `mouth-patches.png` shows every original mouth frame. This adds 89
neutral-eye combinations (669 total recipes) without adding compressed pieces;
it is cross-game aperture retargeting, not a replacement for the native timing.

All ten controlled native winning-pose runs also confirm fighter-specific
source script choices: Akira/Lau/Wolf/Sarah use 1, Pai/Jeffry/Kage/Jacky/Lion
use 3, and Shun uses the aliased 4/5/6 script. Selection confirmation uses 7
throughout the roster; observed damage uses 8 followed by reopening 9.
`native-event-evidence.json` records the exact frame, mode, subphase, HP and
source-node writes for each fighter. These observed source choices guide the
cross-game adapter without claiming equivalent motion IDs between games.


The VF3 sampler also follows the native type-`0x1c` speech controller
(`0x43f40`, script advancement `0x43e0c`). A bounded object-pool scan requires
an exact actor owner, costume ID, resource ID, valid script-table entry and
active speech bit, and rejects duplicate matches. Seven native morph targets
are measured by lower-lip displacement for each fighter; their IDs are not a
linear mouth-opening scale. The source aperture follows the previous/target
morph and the native interpolation fraction. Actor motion frame 1 remaining
frozen in a result pose therefore does not freeze the mouth. This is a visual
jaw-opening approximation; independent native eye/brow morphs and phoneme
identity are not claimed. The high-detail speech path is absent for masked
Kage, matching the native controller guard.

`fighter_expression_pose.h` handles per-player timing, confirmation, hit and
result transitions on every game frame, including frames without rendering.
Hovering selection previews remain neutral. Ordinary hit and victory recovery
uses the source reverse/reopen sequence. The renderer caches four converted
heads per loaded fighter, gives each frame a distinct mesh identity, and keeps
published scenes valid after eviction. It never writes game RAM.

Inactive selection slots are excluded from facial ownership: their actor RAM
can retain the same fighter and a stale hit state, which otherwise overwrites
the active preview with closed eyes. Phase-zero CPU actors remain valid during
battle. Entering a hover preview also discards battle recovery expressions.

Run the expression regressions with local companions:

```sh
make -j4 build/bin/fighter-expression-test build/bin/fighter-expressions-test build/bin/fighter-expression-check
build/bin/fighter-expression-test
build/bin/fighter-expressions-test /path/to/vf.zip /path/to/vf2.zip
build/bin/fighter-expression-check /path/to/vf3.zip /path/to/vf.zip /path/to/vf2.zip build/expression-check all 6500
```

The native sampler tests cover stale winner pointers, replay/new-round guards,
frozen result motion, actor changes, independent player states, voice ownership,
non-ordinal morph blending and duplicate-controller rejection. Renderer tests
compare actual geometry, keep the other outfit neutral, and retain old scenes
across facial-cache eviction and source-asset destruction. Local native speech
analysis is retained in `build/fighter-expression/evidence.json`, including REA
evidence IDs `ev_35753227913a1125be3bd078d8f98ac85ea8dbdc6a4d54de411e4534dfa5cc24`
and `ev_690b66c925cda718783d04e0d227126b82e66ebe436107eea7b79189f9c38a99`.

Local integration validation covers 65,000 native VF3 frames across all ten
fighters, 18 available source/fighter combinations and 2,748 visible-scene
checks. These runs use normal gameplay without forced KO and verify facial
changes against the same scene with neutral heads, independent outfits,
original-face removal, unchanged transforms/shadows and unchanged game RAM.
VF1 Kage correctly remains static; VF2 Kage animates his eyes. P2 Lau's result
close-up verifies native speech ownership independently of P1. After the final
hover-recovery guard, an additional 6,500-frame Akira run passes. Results and
screenshots remain ignored under `build/fighter-expression`.

macOS Metal and the iOS simulator were run with mixed VF1/VF2 fighters; final
selection captures on both show neutral open eyes after the inactive-player
fix. macOS, iOS simulator and unsigned iOS device builds compile and link with
the recorded Xcode toolchain commands. No physical-device run was performed
for this change. Source-decoder ASan/UBSan checks and the RAM-to-renderer hover
ownership regression also pass. ROMs and decoded assets remain external and
are not committed or bundled.
