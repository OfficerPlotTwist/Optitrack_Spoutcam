# Design: OptiTrack camera video → TouchDesigner (Spout)

**Date:** 2026-06-28

## Goal

Get the **raw camera imagery** (grayscale pixels) from OptiTrack cameras into a
live TouchDesigner session as a TOP, on a single Windows PC, lowest practical
latency.

## Key decisions (and why)

- **Transport = Spout, not SpoutCam.** TouchDesigner reads Spout natively via the
  Syphon/Spout In TOP — a GPU shared DirectX texture, zero-copy, full bit depth,
  no framerate cap. SpoutCam re-exposes a Spout sender as a *DirectShow webcam*,
  which would add a lossy 8-bit / YUV / latency hop through the Video Device In
  TOP for no benefit. SpoutCam only matters for apps that *only* accept webcams.
- **Spout over NDI.** Same-PC, so the no-compression zero-copy local path wins.
  NDI would only be preferred if TD ran on a different machine than the cameras.
- **Camera SDK, with Motive closed.** Verified against OptiTrack docs: the Camera
  SDK / Motive API connect to the camera system **all-or-nothing at the host
  level** — `CanConnectToDevices` returns true only *"if there are no other
  instances of Motive running on the host PC."* Cameras cannot be partitioned
  between Motive and an SDK app on one PC, and disabling a camera in Motive does
  **not** release the device. So the capture app owns all cameras; Motive is not
  run. (Sources: Motive API Function Reference; Camera SDK; Camera Video Types;
  Devices Pane — docs.optitrack.com.)
- **Grayscale → RGBA in the sender.** SpoutDX `SendImage` expects RGBA
  (4 bytes/px). The 8-bit camera frame is expanded R=G=B=gray, A=255. TD reads
  luminance from any channel.

## Architecture

```
OptiTrack cameras ── USB/PoE hub ──► optitrack_spout.exe   (owns cameras, no Motive)
                                       CameraLibrary: SetVideoType(Core::GrayscaleMode)
                                                      GetFrame → Rasterize(w,h,w,8,buf)
                                       SpoutDX:       SendImage(rgba, w, h)  name "OptiTrackCam"
                                             │  GPU shared DX texture (same PC)
                                             ▼
                                    TouchDesigner
                                      spoutin_optitrack (Syphon/Spout In TOP, sendername=OptiTrackCam)
                                             ▼
                                      null_optitrack_cam (Null TOP)  ← stable tap
```

Two units, one interface: the named Spout sender `OptiTrackCam`. The sender half
has no Camera SDK dependency, so it is verifiable on its own (see
`spout_testpattern`).

## Camera SDK API used (`main_camera.cpp`)

```cpp
CameraManager::X().WaitForInitialization();
Camera* cam = CameraManager::X().GetCamera();      // null if Motive is running
cam->SetVideoType(Core::GrayscaleMode);            // 8 bits/pixel
cam->Start();
int w = cam->Width(), h = cam->Height();
Frame* f = cam->GetFrame();
f->Rasterize(w, h, /*byteSpan*/ w, /*bpp*/ 8, buffer);
f->Release();
```

## Known constraints

- OptiTrack grayscale mode is *"not fully synchronized… lower frame rate"* —
  irrelevant here: we read frames directly and show one video feed, not
  reconstructing 3D.
- Resolution is camera-model dependent (e.g. Prime-series 1.3/2 MP). The Spout In
  TOP shows a 128×128 placeholder until `OptiTrackCam` broadcasts, then
  auto-adopts the real resolution.

## Verification

- **Spout pipeline:** `spout_testpattern` → TD. Confirmed 2026-06-28: TD's Spout
  In TOP went from the 128×128 placeholder to the sender's 640×480, and the Null
  TOP carried the expected animated gradient (R range 0.0–1.0, mean 0.507,
  std 0.292).
- **Camera frame-grab:** pending — needs the Camera SDK installed and a camera.

## TouchDesigner side

Built live via the TD-MCP bridge in the companion project:
`/project1/spoutin_optitrack` (Syphon/Spout In TOP, `sendername=OptiTrackCam`,
`usespoutactivesender=False`) → `/project1/null_optitrack_cam` (Null TOP).
