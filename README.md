# opti-hacking — OptiTrack camera video → Spout

Publishes **raw 8-bit grayscale camera frames** from OptiTrack cameras as a
**Spout** sender named `OptiTrackCam`, so any Spout-capable application can
receive the live camera image on the same Windows PC — with no SpoutCam /
virtual-webcam hop.

```
OptiTrack cameras ──► optitrack_spout.exe ──(Spout senders "OptiTrackCam_<id>")──► any Spout receiver
                       Camera SDK + SpoutDX     one GPU shared DX texture per camera
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
2. List attached cameras (serial + model):
   ```powershell
   build/Release/optitrack_spout.exe --list
   ```
3. Stream:
   ```powershell
   build/Release/optitrack_spout.exe                       # all attached cameras (grayscale)
   build/Release/optitrack_spout.exe --mode mjpeg          # all cameras, MJPEG (see throughput note)
   build/Release/optitrack_spout.exe --serials 37390,36770 # only these serials
   build/Release/optitrack_spout.exe --serial 37390        # one (repeatable)
   build/Release/optitrack_spout.exe --name MyCam          # sender-name prefix
   build/Release/optitrack_spout.exe --serials 37382,37386 --off-unused  # stream these, darken the rest
   build/Release/optitrack_spout.exe --off                 # turn ALL cameras off (hold until Ctrl+C)
   ```
   Each camera is published as its own Spout sender `<prefix>_<id>` (default
   `OptiTrackCam_<id>`), where `<id>` is the camera's numeric ID — the same number
   shown on its LED display. Cameras are listed and streamed in **ascending ID
   order**; selection is **deterministic by serial number**, not the arbitrary
   "first initialized" camera. Press `Ctrl+C` to stop.

Each streaming camera also lights its **2-digit numeric LED display** with its ID
(via `SetNumeric`), so you can match a physical camera to its sender. The displays
turn off again when the app stops.

### Turning cameras off

- **`--off-unused`** (with a subset): the cameras you are *not* streaming have
  their **IR illumination and ID display turned off** (`SetIntensity(0)` +
  `SetNumeric(false)`), so they don't flood the volume with IR. They are *not*
  `Stop()`ped — stopping a member of a synchronized Prime frame group perturbs the
  cameras still streaming — and they are darkened **before** the streamers start.
- **`--off`** (alone, or with `--serials`): turn off the targeted cameras (or all
  attached) and **hold them dark until Ctrl+C**. The process must keep running to
  hold the off state; cameras revert to default once released.

### Throughput note (grayscale vs MJPEG)

`grayscale` mode is **uncompressed and high-bandwidth** — the camera uplink
saturates past a few cameras, and additional cameras simply deliver no frames
(their Spout sender never appears; a receiver shows black / the placeholder). On a
7×Prime rig only ~3 cameras sustain grayscale at once.

`--mode mjpeg` uses the cameras' **on-camera compressed grayscale** stream — far
lower bandwidth, so **all cameras stream simultaneously** (verified 7/7). The
image is JPEG-compressed (slightly softer) but otherwise the same grayscale view.
Use `mjpeg` when you need many cameras at once; use `grayscale` for a few cameras
at maximum fidelity.

## Receiving the Spout stream (any Spout app)

The sender publishes a standard **Spout sender** — there is nothing app-specific
about it:

| Property | Value |
|----------|-------|
| Sender name | `OptiTrackCam_<id>` — one sender per camera, `<id>` = camera ID on its LED display (prefix via `--name`) |
| Pixel format | RGBA 8-bit (grayscale replicated to R=G=B, A=255) |
| Resolution | adopts each camera's resolution automatically (e.g. 1280×837, 1280×1024) |
| Frame rate | the camera's grayscale frame rate |
| Transport | GPU shared DirectX texture (same PC, zero-copy) |

To receive it, point any Spout-capable receiver at that **sender name**. Spout is
supported (natively or via plugin) by TouchDesigner, Resolume, Notch, MadMapper,
Max/MSP (Jitter), vvvv, Processing, OBS (Spout plugin), Unity/Unreal (Spout
plugins), and others. The general steps in any of them:

1. Start `optitrack_spout.exe` (the sender must be running first, or the receiver
   will simply show black until it appears).
2. In the receiver, add a **Spout receiver / Spout-In** input.
3. Select the sender for the camera you want — **`OptiTrackCam_<id>`** — from its
   sender list (some apps auto-pick the only active sender; others need the name
   typed/selected). Run `optitrack_spout --list` to see the ID→serial map.
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
  and verified live** — `--list` enumerated 7 cameras (Prime 17W / 13W) in
  ascending ID order; in `--mode mjpeg` **all 7 streamed simultaneously** as
  distinct senders `OptiTrackCam_1`…`OptiTrackCam_7` (clean grayscale views,
  numeric ID displays lit). In `grayscale` mode bandwidth caps at ~3 concurrent.

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
