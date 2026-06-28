# opti-hacking — OptiTrack camera video → TouchDesigner (Spout)

Publishes **raw 8-bit grayscale camera frames** from OptiTrack cameras to
TouchDesigner as a **Spout** sender named `OptiTrackCam`. TD receives it natively
through a **Syphon/Spout In TOP** — no SpoutCam / virtual-webcam hop.

```
OptiTrack cameras ──► optitrack_spout.exe ──(Spout "OptiTrackCam")──► TouchDesigner
                       Camera SDK + SpoutDX                            Spout In TOP → Null TOP
```

See [DESIGN.md](DESIGN.md) for the full rationale (why Spout over SpoutCam/NDI,
and why Motive must be closed).

## Two executables

| Target | Needs Camera SDK? | Purpose |
|--------|-------------------|---------|
| `spout_testpattern` | No | Sends a synthetic animated grayscale feed as `OptiTrackCam`. Validates the Spout → TD pipeline with no hardware. |
| `optitrack_spout`   | Yes | The real app: OptiTrack camera (grayscale) → Spout. |

## Prerequisites

- Windows + a GPU (Spout is DirectX-based).
- **Visual Studio 2022** (C++ workload) and **CMake** ≥ 3.15.
- Vendored **Spout2** submodule (DirectX sender). After cloning:
  ```
  git submodule update --init --recursive
  ```
- For `optitrack_spout` only: the **OptiTrack Camera SDK**, version-matched to
  your installed Motive (**Motive 3.4.0 → Camera SDK 3.4.x**). It is a gated
  download from OptiTrack's developer-tools page (login required); install it,
  then point CMake at it (below).

## Build

```powershell
# Configure (test-pattern always builds; camera app builds if the SDK is found)
cmake -S . -B build -G "Visual Studio 17 2022" -A x64

# To also build the real camera app, pass the Camera SDK root
# (the folder containing include/ and lib/):
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
      -DCAMERA_SDK_DIR="C:/Program Files/OptiTrack/Camera SDK"

# Build
cmake --build build --config Release
```

Output lands in `build/Release/`.

## Run

**Validate the pipeline (no cameras):**
```powershell
build/Release/spout_testpattern.exe 640 480 OptiTrackCam
```
Then in TouchDesigner, a Spout In TOP with `sendername = OptiTrackCam` shows the
moving gradient. (This repo's companion TD project already has
`/project1/spoutin_optitrack → /project1/null_optitrack_cam` wired up.)

**Real capture:**
1. **Close Motive.** The Camera SDK takes exclusive, host-level ownership of the
   cameras — it cannot connect while any Motive instance is running.
2. Run:
   ```powershell
   build/Release/optitrack_spout.exe
   ```
3. The Spout In TOP auto-adopts the camera's resolution and shows live grayscale.

Press `Ctrl+C` to stop either sender.

## Layout

```
src/
  spout_grayscale_sender.h/.cpp   Spout sender wrapper (grayscale→RGBA→SpoutDX). No Camera SDK dep.
  main_testpattern.cpp            Synthetic feed (no Camera SDK).
  main_camera.cpp                 OptiTrack Camera SDK capture loop.
external/Spout2/                  Vendored Spout SDK (git submodule).
CMakeLists.txt
DESIGN.md
```

## Status

- Spout pipeline (sender → TD Spout In TOP → Null TOP): **built and verified
  end-to-end** with `spout_testpattern` (TD received 640×480, correct pixels).
- `optitrack_spout` (Camera SDK frame-grab): **written against the documented
  Camera SDK API, not yet compiled/run** — needs the SDK installed.
