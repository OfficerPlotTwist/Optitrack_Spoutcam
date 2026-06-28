// main_camera.cpp
//
// Real capture path: opens an OptiTrack camera via the Camera SDK
// (CameraLibrary), pulls raw 8-bit grayscale frames, and publishes them as the
// Spout sender "OptiTrackCam" for TouchDesigner's Spout In TOP.
//
// Requires the OptiTrack Camera SDK whose version matches the installed Motive
// (3.4.x here). Motive must NOT be running: the Camera SDK takes exclusive,
// host-level ownership of the cameras (CanConnectToDevices is false while any
// Motive instance is running).
//
// API references (docs.optitrack.com, NaturalPoint forums):
//   CameraManager::X().WaitForInitialization();
//   Camera* cam = CameraManager::X().GetCamera();
//   cam->SetVideoType(Core::GrayscaleMode);   // 8 bits/pixel
//   cam->Start();
//   Frame* f = cam->GetFrame();
//   f->Rasterize(width, height, byteSpan, bitsPerPixel, buffer);
//   f->Release();
#include "cameralibrary.h"
#include "spout_grayscale_sender.h"

#include <windows.h>
#include <csignal>
#include <cstdio>
#include <vector>

using namespace CameraLibrary;

static volatile bool g_run = true;
static void onSignal(int) { g_run = false; }

int main() {
    signal(SIGINT, onSignal);

    CameraManager::X().WaitForInitialization();

    Camera* camera = CameraManager::X().GetCamera();
    if (!camera) {
        std::fprintf(stderr,
            "ERROR: No OptiTrack camera found. Is Motive running? It must be "
            "closed - the Camera SDK needs exclusive access to the cameras.\n");
        CameraManager::X().Shutdown();
        return 1;
    }

    // Raw grayscale imagery (not centroid/object data).
    camera->SetVideoType(Core::GrayscaleMode);
    camera->Start();

    const int w = camera->Width();
    const int h = camera->Height();
    std::printf("Camera '%s'  %dx%d  grayscale -> Spout 'OptiTrackCam'. Ctrl+C to stop.\n",
                camera->Name(), w, h);

    SpoutGrayscaleSender sender;
    if (!sender.init("OptiTrackCam")) {
        std::fprintf(stderr, "ERROR: Spout DirectX init failed.\n");
        camera->Release();
        CameraManager::X().Shutdown();
        return 2;
    }

    // 8-bit grayscale destination buffer; byteSpan = width (1 byte/pixel).
    std::vector<uint8_t> gray(size_t(w) * h);

    while (g_run) {
        Frame* frame = camera->GetFrame();
        if (frame) {
            frame->Rasterize(w, h, (unsigned)w, 8, gray.data());
            sender.sendGrayscale(gray.data(), (unsigned)w, (unsigned)h, (unsigned)w);
            frame->Release();
        } else {
            Sleep(1); // no frame ready yet; avoid a busy spin
        }
    }

    sender.shutdown();
    camera->Release();
    CameraManager::X().Shutdown();
    std::printf("Stopped.\n");
    return 0;
}
