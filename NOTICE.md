# Third-party notices

This project depends on third-party software that is **not** owned by this
project and, in one case, is **not** redistributed here.

## OptiTrack Camera SDK (NaturalPoint, Inc.) — proprietary, NOT included

`src/main_camera.cpp` links against the **OptiTrack Camera SDK** (`CameraLibrary`).

- The SDK is **proprietary software of NaturalPoint, Inc. (OptiTrack)** and is
  **not distributed in this repository**. No SDK headers, import libraries, or
  runtime binaries (e.g. `CameraLibrary2019x64S.dll`) are committed here.
- You must obtain the Camera SDK directly from OptiTrack
  (https://optitrack.com/support/downloads/developer-tools.html), under a
  NaturalPoint account, and use it only in accordance with the **license
  agreement / EULA that accompanies the SDK**.
- That agreement governs how you may use the SDK and any redistribution of its
  binaries (generally: redistribution of the SDK itself is **not** permitted).
  Do not commit or publish the SDK files, and review the EULA before distributing
  any application built against it.
- Use a Camera SDK version matched to your installed Motive (this project was
  built and verified against **Camera SDK 3.4.1** with **Motive 3.4.0**).

This project is an independent integration and is **not** affiliated with,
endorsed by, or supported by NaturalPoint / OptiTrack.

## Spout2 (Lynn Jarvis / spout.zeal.co) — BSD 2-Clause, vendored as a submodule

The `external/Spout2` git submodule provides the Spout DirectX sender used to
publish frames. Spout2 is distributed by its authors under the **BSD 2-Clause
license**. The full license text accompanies the submodule at
`external/Spout2/LICENSE`. Spout2 is fetched from its upstream repository, not
re-hosted here.
