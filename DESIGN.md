# Design: OptiTrack camera video → Spout

**Date:** 2026-06-28

## Goal

Get the **raw camera imagery** (grayscale pixels) from OptiTrack cameras out as a
live **Spout** stream on a single Windows PC, with lowest practical latency, so
any Spout-capable application can consume it.

## Key decisions (and why)

- **Transport = Spout, not SpoutCam.** A Spout sender is a GPU shared DirectX
  texture: zero-copy, full bit depth, no framerate cap, and read natively by most
  real-time visual tools. SpoutCam re-exposes a Spout sender as a *DirectShow
  webcam*, adding a lossy 8-bit / YUV / latency hop. Use SpoutCam only for
  receivers that accept *only* a webcam; prefer native Spout everywhere else.
- **Spout over NDI.** Same-PC, so the no-compression zero-copy local path wins.
  NDI would only be preferred if the receiver ran on a different machine.
- **Camera SDK, with Motive closed.** Verified against OptiTrack docs: the Camera
  SDK / Motive API connect to the camera system **all-or-nothing at the host
  level** — `CanConnectToDevices` returns true only *"if there are no other
  instances of Motive running on the host PC."* Cameras cannot be partitioned
  between Motive and an SDK app on one PC, and disabling a camera in Motive does
  **not** release the device. So the capture app owns all cameras; Motive is not
  run. (Sources: Motive API Function Reference; Camera SDK; Camera Video Types;
  Devices Pane — docs.optitrack.com.)
- **Grayscale → RGBA in the sender.** SpoutDX `SendImage` expects RGBA
  (4 bytes/px). The 8-bit camera frame is expanded R=G=B=gray, A=255. A receiver
  reads luminance from any channel.

## Architecture

```
OptiTrack cameras ── USB/PoE hub ──► optitrack_spout.exe   (owns cameras, no Motive)
                                       CameraLibrary: SetVideoType(Core::GrayscaleMode)
                                                      LatestFrame → Rasterize(*cam,w,h,w,8,buf)
                                       SpoutDX:       SendImage(rgba, w, h)  name "OptiTrackCam"
                                             │  GPU shared DX texture (same PC)
                                             ▼
                                       any Spout receiver (selects sender "OptiTrackCam")
```

Two units, one interface: the named Spout sender `OptiTrackCam`. The sender half
has no Camera SDK dependency, so it is verifiable on its own (see
`spout_testpattern`).

## Camera SDK API used (`main_camera.cpp`, verified against SDK 3.4.1 headers)

```cpp
CameraManager::X().WaitForInitialization();
std::shared_ptr<Camera> cam = CameraManager::X().GetCamera();   // null if Motive is running
cam->SetVideoType(Core::GrayscaleMode);                          // enum eVideoMode, value 1
cam->Start();
int w = cam->Width(), h = cam->Height();
std::shared_ptr<const Frame> f = cam->LatestFrame();
f->Rasterize(*cam, w, h, /*span*/ w, /*bpp*/ 8, buffer);         // Camera& is the first arg
cam->Stop();                                                     // no Release(); shared_ptr frees
```

## Receiving side (any Spout app)

The output is a standard Spout sender; receiving is not specific to any one tool.
Point a Spout receiver at the sender name **`OptiTrackCam`** (RGBA8, resolution =
camera resolution, camera frame rate). The sender must be running before the
receiver connects, or the receiver shows black until it appears. See the README
"Receiving the Spout stream" section for the generic steps.

## Known constraints

- OptiTrack grayscale mode is *"not fully synchronized… lower frame rate"* —
  irrelevant here: we read frames directly and show one video feed, not
  reconstructing 3D.
- Resolution is camera-model dependent (e.g. Prime-series 1.3/2 MP). A Spout
  receiver adopts whatever resolution the sender publishes.

## Verification

- **Spout output:** `spout_testpattern` → a Spout receiver. Confirmed 2026-06-28:
  the receiver adopted the sender's 640×480 and showed the expected animated
  gradient (R range 0.0–1.0, mean 0.507, std 0.292).
- **Real camera capture:** confirmed 2026-06-28 with Camera SDK 3.4.1 — a
  connected camera streamed a **1280×837** grayscale image to a Spout receiver
  (mean 0.051 with peaks to 1.0: dark field with sparse bright highlights, as
  expected for an OptiTrack IR feed).

## Licensing note

The **OptiTrack Camera SDK** is proprietary NaturalPoint software and is **not**
included in this repository. It must be obtained from OptiTrack under their
license, and its headers/binaries (e.g. `CameraLibrary*.dll`) must not be
redistributed. See [NOTICE.md](NOTICE.md).
