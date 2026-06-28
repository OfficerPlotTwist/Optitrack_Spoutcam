// main_camera.cpp
//
// Captures raw 8-bit grayscale frames from one or more OptiTrack cameras via the
// Camera SDK (CameraLibrary) and publishes EACH camera as its own Spout sender,
// named "<prefix>_<serial>" (default prefix "OptiTrackCam"). Any Spout-capable
// app can then receive whichever camera(s) it wants by sender name.
//
// Camera selection is deterministic by serial number — not the arbitrary
// "first initialized" camera that CameraManager::GetCamera() returns.
//
// Usage:
//   optitrack_spout                       Stream ALL attached cameras.
//   optitrack_spout --list                List attached cameras (serial/name) and exit.
//   optitrack_spout --serials 11001,11002 Stream only these serials.
//   optitrack_spout --serial 11001        Stream only this serial (repeatable).
//   optitrack_spout --name MyCam          Sender name prefix (default "OptiTrackCam").
//
// Motive must be CLOSED: the Camera SDK takes exclusive, host-level ownership of
// the cameras (CanConnectToDevices is false while any Motive instance is running).
//
// Camera SDK 3.4.x API (verified against the installed headers):
//   CameraManager::X().WaitForInitialization();
//   CameraList list;  list.Count();  list[i].Serial()/.Name()/.IsVirtual();
//   std::shared_ptr<Camera> cam = CameraManager::X().GetCameraBySerial(serial);
//   cam->SetVideoType(Core::GrayscaleMode);  cam->Start();
//   std::shared_ptr<const Frame> f = cam->LatestFrame();
//   f->Rasterize(*cam, w, h, span, bpp, buffer);  cam->Stop();
#include "cameralibrary.h"
#include "spout_grayscale_sender.h"

#include <windows.h>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <set>
#include <string>
#include <vector>

using namespace CameraLibrary;

static volatile bool g_run = true;
static void onSignal(int) { g_run = false; }

struct CamStream {
    std::shared_ptr<Camera> cam;
    SpoutGrayscaleSender sender;
    std::vector<uint8_t> gray;
    int w = 0;
    int h = 0;
    unsigned int serial = 0;
};

// Parse a comma-separated list of unsigned serials into `out`.
static void parseSerials(const char* csv, std::set<unsigned int>& out) {
    std::string s(csv);
    size_t start = 0;
    while (start < s.size()) {
        size_t comma = s.find(',', start);
        std::string tok = s.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
        if (!tok.empty()) {
            unsigned int v = (unsigned int)strtoul(tok.c_str(), nullptr, 10);
            if (v != 0) out.insert(v);
        }
        if (comma == std::string::npos) break;
        start = comma + 1;
    }
}

int main(int argc, char** argv) {
    signal(SIGINT, onSignal);

    bool listOnly = false;
    std::set<unsigned int> wanted;          // empty => all cameras
    std::string prefix = "OptiTrackCam";

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--list") == 0) {
            listOnly = true;
        } else if (std::strcmp(argv[i], "--serials") == 0 && i + 1 < argc) {
            parseSerials(argv[++i], wanted);
        } else if (std::strcmp(argv[i], "--serial") == 0 && i + 1 < argc) {
            unsigned int v = (unsigned int)strtoul(argv[++i], nullptr, 10);
            if (v != 0) wanted.insert(v);
        } else if (std::strcmp(argv[i], "--name") == 0 && i + 1 < argc) {
            prefix = argv[++i];
        } else {
            std::fprintf(stderr, "Unknown / incomplete argument: %s\n", argv[i]);
            std::fprintf(stderr, "Usage: optitrack_spout [--list] [--serials a,b] [--serial s] [--name prefix]\n");
            return 64;
        }
    }

    CameraManager::X().WaitForInitialization();

    CameraList available;  // self-populates with attached cameras
    if (available.Count() == 0) {
        std::fprintf(stderr,
            "ERROR: No OptiTrack cameras found. Is Motive running? It must be "
            "closed - the Camera SDK needs exclusive access to the cameras.\n");
        CameraManager::X().Shutdown();
        return 1;
    }

    if (listOnly) {
        std::printf("Attached OptiTrack cameras (%d):\n", available.Count());
        for (int i = 0; i < available.Count(); ++i) {
            CameraEntry& e = available[i];
            std::printf("  serial %-10u  %-20s%s\n",
                        e.Serial(), e.Name(), e.IsVirtual() ? "  (virtual)" : "");
        }
        CameraManager::X().Shutdown();
        return 0;
    }

    // Open the selected cameras (all attached, unless --serial(s) narrowed it).
    std::vector<std::unique_ptr<CamStream>> streams;
    for (int i = 0; i < available.Count(); ++i) {
        CameraEntry& e = available[i];
        if (e.IsVirtual()) continue;
        const unsigned int serial = e.Serial();
        if (!wanted.empty() && wanted.find(serial) == wanted.end()) continue;

        std::shared_ptr<Camera> cam = CameraManager::X().GetCameraBySerial(serial);
        if (!cam) {
            std::fprintf(stderr, "WARN: could not open camera serial %u; skipping.\n", serial);
            continue;
        }

        cam->SetVideoType(Core::GrayscaleMode);
        cam->Start();

        // Light up the camera's 2-digit numeric LED display with its hardware
        // ID (falls back to enumeration order if no ID is assigned).
        const int idVal = cam->CameraIDValid() ? cam->CameraID() : (i + 1);
        cam->SetNumeric(true, idVal);

        auto cs = std::make_unique<CamStream>();
        cs->cam = cam;
        cs->serial = serial;
        cs->w = cam->Width();
        cs->h = cam->Height();
        cs->gray.assign(size_t(cs->w) * cs->h, 0);

        const std::string name = prefix + "_" + std::to_string(serial);
        if (!cs->sender.init(name)) {
            std::fprintf(stderr, "WARN: Spout init failed for %s; skipping.\n", name.c_str());
            cam->Stop();
            continue;
        }

        std::printf("Camera '%s' serial %u  ID %d  %dx%d  -> Spout '%s'\n",
                    cam->Name(), serial, idVal, cs->w, cs->h, name.c_str());
        streams.push_back(std::move(cs));
    }

    if (streams.empty()) {
        std::fprintf(stderr, "ERROR: no cameras opened (check --serial filter against --list).\n");
        CameraManager::X().Shutdown();
        return 1;
    }

    std::printf("Streaming %zu camera(s). Ctrl+C to stop.\n", streams.size());

    while (g_run) {
        bool any = false;
        for (auto& cs : streams) {
            std::shared_ptr<const Frame> frame = cs->cam->LatestFrame();
            if (frame) {
                frame->Rasterize(*cs->cam, (unsigned)cs->w, (unsigned)cs->h,
                                 (unsigned)cs->w, 8, cs->gray.data());
                cs->sender.sendGrayscale(cs->gray.data(), (unsigned)cs->w,
                                         (unsigned)cs->h, (unsigned)cs->w);
                any = true;
            }
        }
        if (!any) Sleep(1); // no camera had a new frame; avoid a busy spin
    }

    for (auto& cs : streams) {
        cs->sender.shutdown();
        cs->cam->Stop();
        cs->cam.reset();
    }
    CameraManager::X().Shutdown();
    std::printf("Stopped.\n");
    return 0;
}
