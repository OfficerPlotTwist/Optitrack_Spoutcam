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
                                       CameraList: enumerate; GetCameraBySerial(serial)
                                       per camera: SetVideoType(Grayscale|MJPEG); Start();
                                                   SetNumeric(true, id)  // light ID display
                                                   LatestFrame → Rasterize(*cam,w,h,w,8,buf)
                                       SpoutDX:    SendImage(rgba,w,h)  name "OptiTrackCam_<id>"
                                             │  one GPU shared DX texture per camera (same PC)
                                             ▼
                                       any Spout receiver (selects "OptiTrackCam_<id>")
```

One Spout sender per camera; the sender name is the stable interface. The sender
half has no Camera SDK dependency, so it is verifiable on its own (see
`spout_testpattern`).

## Camera SDK API used (`main_camera.cpp`, verified against SDK 3.4.1 headers)

```cpp
CameraManager::X().WaitForInitialization();
CameraList list;  // self-populates; list.Count(), list[i].Serial()/.Name()/.IsVirtual()
std::shared_ptr<Camera> cam = CameraManager::X().GetCameraBySerial(serial);  // deterministic
cam->SetVideoType(Core::GrayscaleMode);                          // enum eVideoMode, value 1
cam->Start();
cam->SetNumeric(true, cam->CameraID());                          // light the 2-digit ID display
int w = cam->Width(), h = cam->Height();
std::shared_ptr<const Frame> f = cam->LatestFrame();
f->Rasterize(*cam, w, h, /*span*/ w, /*bpp*/ 8, buffer);         // Camera& is the first arg
cam->Stop();                                                     // turns numeric off; shared_ptr frees
```

Selecting by **serial** is deterministic; `GetCamera()` returns the arbitrary
"first initialized" camera and is avoided. Each camera's **ID** (its hardware
`CameraID()`, the number on its LED display) is used for the sender name and to
order output ascending; if IDs are unassigned the app falls back to sequential
1..N by serial.

## Receiving side (any Spout app)

The output is one standard Spout sender per camera; receiving is not specific to
any one tool. Point a Spout receiver at the sender name **`OptiTrackCam_<id>`**
(RGBA8, resolution = that camera's frame resolution, camera frame rate). The
sender must be running before the receiver connects, or the receiver shows black
until it appears. See the README "Receiving the Spout stream" section.

## Known constraints

- **Grayscale bandwidth.** Uncompressed grayscale is high-bandwidth; the camera
  uplink saturates past a few cameras and the rest deliver no frames (their Spout
  sender never registers). On a 7×Prime rig ~3 cameras sustain grayscale at once.
  **`--mode mjpeg`** (on-camera compressed grayscale) is far lighter and streams
  all cameras simultaneously (verified 7/7), at the cost of mild JPEG softening.
- Grayscale/MJPEG modes are *"not fully synchronized… lower frame rate"* —
  irrelevant here: we show video feeds, not reconstructing 3D.
- The grayscale/MJPEG reference frame is **decimated** below the native sensor
  resolution, and the decimation differs per mode (e.g. 17W: grayscale ≈ 1280×837,
  MJPEG ≈ 832×544). Size the buffer/texture from `Camera::FrameSize(mode, w, h,
  scale)`, NOT `Width()/Height()`, or the image lands in the top-left of an
  oversized texture. A Spout receiver adopts whatever resolution the sender
  publishes.

## Verification

- **Spout output:** `spout_testpattern` → a Spout receiver. Confirmed 2026-06-28:
  the receiver adopted the sender's 640×480 and showed the expected animated
  gradient (R range 0.0–1.0, mean 0.507, std 0.292).
- **Real camera capture:** confirmed 2026-06-28 with Camera SDK 3.4.1 on a 7×Prime
  rig (17W ×6, 13W ×1). `--list` enumerated all 7 in ascending ID order; in
  `--mode mjpeg` **all 7 streamed simultaneously** as `OptiTrackCam_1`…`_7` with a
  saved frame showing a clean, correctly-proportioned grayscale view of the mocap
  volume. Plain `grayscale` mode capped at ~3 concurrent cameras (bandwidth), with
  each camera verified working in isolation.

## Licensing note

The **OptiTrack Camera SDK** is proprietary NaturalPoint software and is **not**
included in this repository. It must be obtained from OptiTrack under their
license, and its headers/binaries (e.g. `CameraLibrary*.dll`) must not be
redistributed. See [NOTICE.md](NOTICE.md).
