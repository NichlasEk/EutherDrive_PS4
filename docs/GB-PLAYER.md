# EutherDrive GB player

## 0.09: sound, color ROMs and library

0.08 is now **confirmed working on physical PS4 by the user** ("fungerar
kanonbra"). Preserved package SHA-256:
`99a07bc384553a68b80f7eac453c1b2d8b0c935b935482eae2d4efccb88f1e6b`.

0.09 adds a controller-driven library and 48 kHz float stereo AudioOut.
The unchanged core generates 44.1 kHz signed stereo. Streaming linear
resampling preserves phase across chunks. A native audio thread consumes an
8192-frame SPSC ring (initial cushion 2048 frames) in 512-frame double buffers.
Default gain is 50%; underruns insert silence, bounded queue backpressure
prevents unlimited latency. Init/output errors disable audio for that game;
gameplay continues. Stop/join/drain/close occurs before Mono cleanup.

Controls: Up/Down and Cross choose/start; Circle exits from the library.
In-game mapping unchanged; L1+R1 now returns to library, not app exit.
Triangle toggles mute in both library and game. Re-selecting a game restarts
it. Battery saves/save states are NOT implemented; existing source `.sav`
and `.euthstate` files are not loaded or modified.

Build with `ED_GB_PLAYER=1` and either `ED_GB_ROM=/path/game.gb` or
`ED_GB_LIBRARY=/path/library.json` (JSON array of 1..32 explicit ROM paths),
then run `scripts/package-runtime-probe.sh`. Package:
`dist/eutherdrive-gb-player-0.09.pkg`. Same title ID, updates 0.08.
ROMs are privately staged in package root as `rom00.gb` etc., with
`library.tsv` and SHA-256 provenance in `library-private.json`.
Never distribute this user-ROM package.

The library optionally scans `/data/eutherdrive-ps4/roms` and USB
`/mnt/usb0/EutherDrive`, `/mnt/usb1/EutherDrive` for GB/GBC files. USB visibility
is best-effort; packaged library does not require external mount rights.

Local selected games: Super Mario Land, Link's Awakening DX, Super Mario
Bros. Deluxe. Each ran 1200 frames in desktop Mono using the packaged assembly
with scripted Start/A: 1,771,520 interleaved audio samples each, nonzero PCM,
and respectively 4, 31, 431 distinct framebuffer colors. Visual inspection
confirmed Mario DMG gameplay, Zelda DX name entry, and Mario Deluxe file
selection. GBC selection screens are NOT a full gameplay compatibility test.
WAVs/captures and results: `build/gb-player/rom00..02/`, `host-validation.log`.

Native tests cover resampling rate/chunk continuity/stereo/bounds; queue
wrap/mute/gain/reopen/failure/shutdown; renderer scaling/bounds/buffer rotation,
menu and controller disconnect. Audio/UI tests also pass ASan/UBSan.
All nine credential fault cases and the original 0.07 framebuffer oracle pass.
PS4 build and package validation pass. Actual HDMI sound quality, interactive
library switching and GBC on physical PS4 still require user testing.

## Historical 0.08

First interactive PS4 frontend for the pinned EutherDrive GB core. Copyright
2026 Nichlas Eklöf; upstream core notices remain in the package. This is an
early player, not the desktop frontend port and not a compatibility claim.

## Build and data

```sh
ED_GB_PLAYER=1 ED_GB_ROM='/absolute/path/to/local/game.gb' scripts/package-runtime-probe.sh
```

Output: `dist/eutherdrive-gb-player-0.08.pkg`, title **EutherDrive GB**.
Uses EDRM00001, updating the previous probe. The supplied ROM is included as
`/app0/game.gb`. This is a PRIVATE test package, not a distributable release.
ROMs and build outputs remain ignored; no source ROM or save file is modified.

Optional override search order: `/data/eutherdrive-ps4/game.gb`, then `game.gbc`
in the same directory; then `/mnt/usb0/EutherDrive/game.gb` and USB1 equivalent.
USB direct access is best-effort, not proven in the sandbox. Packaged ROM is
the reliable fallback and requires no USB insertion after installation.

## Controls and current limits

- D-pad: Game Boy directions; Cross: A; Circle: B.
- Options: Start; Square: Select.
- L1 + R1: stop emulation, close controller, clean up Mono, restore original
  rights through the same tested credential transaction as 0.06.
- VideoOut: centered 640x576 image, integer 4x scale, synchronized double buffers.
- No sound output, battery save persistence, ROM browser, or save states yet.
- Same firmware gate as the proven probe: 9.60. Other firmware is not enabled.
- Process Sony rights stay elevated while Mono is running. Normal stop restores
  them; a native crash or PS-button termination does not run our cleanup path.

## Evidence, 2026-09-29

User supplied `Super Mario Land (World) (Rev 1).gb`. Desktop Mono ran the exact
packaged managed assembly for 600 frames with scripted Start. Captured title
and World 1-1 inspected visually in `build/gb-player/title.ppm` and `game.ppm`.
Four framebuffer colors; title hash `8416abb7`, final hash `4d45bd8f` (pixel FNV,
not the byte-wise 0.07 oracle). This proves core output on desktop, not physical
PS4 performance, presentation, or controller behavior.

Native host compiles with warnings-as-errors, all nine credential transaction
fault cases pass, and package signatures/hashes validate. Physical 0.08 later
confirmed by the user (see above).
The original 0.07 deterministic core probe remains independently runnable.

An earlier candidate homebrew Snake 2.0 returned blank output in desktop smoke
testing and was NOT shipped. Its cause is unresolved; do not claim GBC support
based on this DMG Mario test.
