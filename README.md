# opti-hacking — OptiTrack camera video → Spout

Publishes **raw 8-bit grayscale camera frames** from OptiTrack cameras as a
**Spout** sender named `OptiTrackCam`, so any Spout-capable application can
receive the live camera image on the same Windows PC — with no SpoutCam /
virtual-webcam hop.

```
OptiTrack cameras ──► optitrack_spout.exe ──(Spout sender "OptiTrackCam")──► any Spout receiver
                       Camera SDK + SpoutDX     GPU shared DX texture
```

See [DESIGN.md](DESIGN.md) for the full rationale (why Spout over SpoutCam/NDI,
and why Motive must be closed).

## Two executables

| Target | Needs Camera SDK? | Purpose |
|--------|-------------------|---------|
| `spout_testpattern` | No | Sends a synthetic animated grayscale feed as `OptiTrackCam`. Validates the Spout output with no hardware. |
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
      -DCAMERA_SDK_DIR="C:/Program Files (x86)/OptiTrack/CameraSDK"

# Build
cmake --build build --config Release
```

Output lands in `build/Release/`. The matching `CameraLibrary*.dll` is copied
next to `optitrack_spout.exe` automatically.

## Run the sender

**Validate without cameras:**
```powershell
build/Release/spout_testpattern.exe 640 480 OptiTrackCam
```

**Real capture:**
1. **Close Motive.** The Camera SDK takes exclusive, host-level ownership of the
   cameras — it cannot connect while any Motive instance is running.
2. Run:
   ```powershell
   build/Release/optitrack_spout.exe
   ```
   It prints the camera name and resolution, then streams. Press `Ctrl+C` to stop.

## Receiving the Spout stream (any Spout app)

The sender publishes a standard **Spout sender** — there is nothing app-specific
about it:

| Property | Value |
|----------|-------|
| Sender name | `OptiTrackCam` |
| Pixel format | RGBA 8-bit (grayscale replicated to R=G=B, A=255) |
| Resolution | adopts the camera's resolution automatically (e.g. 1280×837) |
| Frame rate | the camera's grayscale frame rate |
| Transport | GPU shared DirectX texture (same PC, zero-copy) |

To receive it, point any Spout-capable receiver at that **sender name**. Spout is
supported (natively or via plugin) by TouchDesigner, Resolume, Notch, MadMapper,
Max/MSP (Jitter), vvvv, Processing, OBS (Spout plugin), Unity/Unreal (Spout
plugins), and others. The general steps in any of them:

1. Start `optitrack_spout.exe` (the sender must be running first, or the receiver
   will simply show black until it appears).
2. In the receiver, add a **Spout receiver / Spout-In** input.
3. Select the sender named **`OptiTrackCam`** from its sender list (some apps
   auto-pick the only active sender; others need the name typed/selected).
4. The receiver adopts the resolution and shows the live grayscale image.

If a receiver only accepts a **webcam** (not Spout), route the sender through the
separate **SpoutCam** virtual-webcam driver — but prefer a native Spout input
where available, since SpoutCam adds a lossy DirectShow conversion.

To verify a receiver is getting real data rather than black, check that its input
reports the camera resolution and that pixel values vary (an OptiTrack IR feed is
a mostly-dark field with sparse bright highlights).

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

- Spout output (sender → Spout receiver): **built and verified end-to-end.**
- `optitrack_spout` (real Camera SDK capture): **built against Camera SDK 3.4.1
  and verified live** — a connected camera streamed a 1280×837 grayscale image to
  a Spout receiver (dark field, bright highlights, as expected for an IR feed).

## Legal & licensing

⚠️ **The OptiTrack Camera SDK is proprietary NaturalPoint software and is NOT
included in this repository.** You must download and install it yourself under
OptiTrack's license terms (it is gated behind a NaturalPoint account).

- This repo contains only **source that links against** the Camera SDK — no SDK
  headers, libraries, or `CameraLibrary*.dll` binaries are committed or
  redistributed. The DLL copied next to the built exe lives under the
  git-ignored `build/` directory and must not be redistributed.
- Your use of the Camera SDK (and of camera imagery captured with it) is governed
  by the **NaturalPoint / OptiTrack SDK license agreement** that accompanies the
  SDK. Review it before distributing any build or derivative.
- The vendored **Spout2** SDK (`external/Spout2`, git submodule) is licensed by
  its authors under the BSD 2-Clause license; see that submodule's `LICENSE`.

See [NOTICE.md](NOTICE.md) for the full third-party notice.
