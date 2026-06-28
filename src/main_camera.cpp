// main_camera.cpp
//
// Captures raw 8-bit grayscale frames from one or more OptiTrack cameras via the
// Camera SDK (CameraLibrary) and publishes EACH camera as its own Spout sender,
// named "<prefix>_<id>" (default prefix "OptiTrackCam") where <id> is the
// camera's numeric ID — the same number shown on its 2-digit LED display. Any
// Spout-capable app receives a camera by that sender name.
//
// Cameras are listed and streamed in ASCENDING ID order. Selection is by serial
// number (deterministic), not the arbitrary "first initialized" camera that
// CameraManager::GetCamera() returns.
//
// Usage:
//   optitrack_spout                       Stream ALL attached cameras.
//   optitrack_spout --list                List attached cameras (ID/serial/name) and exit.
//   optitrack_spout --serials 37390,36770 Stream only these serials.
//   optitrack_spout --serial 37390        Stream only this serial (repeatable).
//   optitrack_spout --name MyCam          Sender name prefix (default "OptiTrackCam").
//
// Motive must be CLOSED: the Camera SDK takes exclusive, host-level ownership of
// the cameras (CanConnectToDevices is false while any Motive instance is running).
#include "cameralibrary.h"
#include "spout_grayscale_sender.h"

#include <windows.h>
#include <algorithm>
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
    int id = 0;
};

// One opened camera, before sender creation.
struct OpenCam {
    std::shared_ptr<Camera> cam;
    unsigned int serial = 0;
    int rawId = 0;      // hardware ID if valid, else 0
    int id = 0;         // resolved display/sender ID (ascending, unique)
    int w = 0;
    int h = 0;
    std::string name;
};

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

// Resolve display IDs: use the cameras' hardware IDs when they are all valid and
// unique; otherwise fall back to sequential 1..N by ascending serial.
static void resolveIds(std::vector<OpenCam>& cams) {
    bool faithful = true;
    std::set<int> seen;
    for (auto& c : cams) {
        if (c.rawId <= 0 || seen.count(c.rawId)) { faithful = false; break; }
        seen.insert(c.rawId);
    }
    if (faithful) {
        for (auto& c : cams) c.id = c.rawId;
    } else {
        std::sort(cams.begin(), cams.end(),
                  [](const OpenCam& a, const OpenCam& b) { return a.serial < b.serial; });
        for (size_t i = 0; i < cams.size(); ++i) cams[i].id = int(i + 1);
    }
    std::sort(cams.begin(), cams.end(),
              [](const OpenCam& a, const OpenCam& b) { return a.id < b.id; });
}

int main(int argc, char** argv) {
    signal(SIGINT, onSignal);

    bool listOnly = false;
    std::set<unsigned int> wanted;          // empty => all cameras
    std::string prefix = "OptiTrackCam";
    std::string mode = "grayscale";         // "grayscale" or "mjpeg"

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
        } else if (std::strcmp(argv[i], "--mode") == 0 && i + 1 < argc) {
            mode = argv[++i];
        } else {
            std::fprintf(stderr, "Unknown / incomplete argument: %s\n", argv[i]);
            std::fprintf(stderr, "Usage: optitrack_spout [--list] [--serials a,b] [--serial s] "
                                 "[--name prefix] [--mode grayscale|mjpeg]\n");
            return 64;
        }
    }

    // MJPEG is on-camera compressed grayscale: far lower uplink bandwidth, so many
    // more cameras can stream at once (grayscale saturates the uplink past a few
    // cameras). Rasterize decodes MJPEG to the same 8-bit grayscale buffer.
    const Core::eVideoMode videoMode =
        (mode == "mjpeg" || mode == "MJPEG") ? Core::MJPEGMode : Core::GrayscaleMode;

    CameraManager::X().WaitForInitialization();

    CameraList available;  // self-populates with attached cameras
    if (available.Count() == 0) {
        std::fprintf(stderr,
            "ERROR: No OptiTrack cameras found. Is Motive running? It must be "
            "closed - the Camera SDK needs exclusive access to the cameras.\n");
        CameraManager::X().Shutdown();
        return 1;
    }

    // Collect the serials we will open (skip virtual; apply --serial filter).
    std::vector<unsigned int> serials;
    for (int i = 0; i < available.Count(); ++i) {
        CameraEntry& e = available[i];
        if (e.IsVirtual()) continue;
        unsigned int s = e.Serial();
        if (!wanted.empty() && wanted.find(s) == wanted.end()) continue;
        serials.push_back(s);
    }

    // Open each camera (needed to read its hardware ID).
    std::vector<OpenCam> opened;
    for (unsigned int s : serials) {
        std::shared_ptr<Camera> cam = CameraManager::X().GetCameraBySerial(s);
        if (!cam) {
            std::fprintf(stderr, "WARN: could not open camera serial %u; skipping.\n", s);
            continue;
        }
        OpenCam oc;
        oc.cam = cam;
        oc.serial = s;
        oc.rawId = cam->CameraIDValid() ? cam->CameraID() : 0;
        oc.w = cam->Width();
        oc.h = cam->Height();
        oc.name = cam->Name() ? cam->Name() : "";
        opened.push_back(std::move(oc));
    }

    if (opened.empty()) {
        std::fprintf(stderr, "ERROR: no cameras opened (check --serial filter against --list).\n");
        CameraManager::X().Shutdown();
        return 1;
    }

    resolveIds(opened);  // assigns ascending, unique .id and sorts by it

    if (listOnly) {
        std::printf("Attached OptiTrack cameras (%zu), ascending ID:\n", opened.size());
        std::printf("  %-4s %-10s %-20s %s\n", "ID", "serial", "model", "sender name");
        for (auto& c : opened) {
            std::printf("  %-4d %-10u %-20s %s_%d\n",
                        c.id, c.serial, c.name.c_str(), prefix.c_str(), c.id);
        }
        std::fflush(stdout);
        CameraManager::X().Shutdown();
        return 0;
    }

    // Start each camera, light its ID display, and create its Spout sender.
    std::vector<std::unique_ptr<CamStream>> streams;
    for (auto& o : opened) {
        o.cam->SetVideoType(videoMode);
        o.cam->Start();
        o.cam->SetNumeric(true, o.id);  // light the 2-digit LED with the same ID

        // The grayscale/MJPEG reference frame is decimated — smaller than the
        // native sensor — so size the buffer/texture from the per-mode FrameSize,
        // NOT Width()/Height(), or the image lands in the top-left of an oversized
        // texture (the rest black).
        int fw = 0, fh = 0;
        float frameScale = 1.0f;
        o.cam->FrameSize(videoMode, fw, fh, frameScale);
        if (fw <= 0 || fh <= 0) { fw = o.cam->Width(); fh = o.cam->Height(); }

        auto cs = std::make_unique<CamStream>();
        cs->cam = o.cam;
        cs->serial = o.serial;
        cs->id = o.id;
        cs->w = fw;
        cs->h = fh;
        cs->gray.assign(size_t(fw) * fh, 0);

        const std::string name = prefix + "_" + std::to_string(o.id);
        if (!cs->sender.init(name)) {
            std::fprintf(stderr, "WARN: Spout init failed for %s; skipping.\n", name.c_str());
            o.cam->Stop();
            continue;
        }

        std::printf("ID %-3d serial %-10u %-18s %dx%d  -> Spout '%s'\n",
                    o.id, o.serial, o.name.c_str(), fw, fh, name.c_str());
        streams.push_back(std::move(cs));
    }

    if (streams.empty()) {
        std::fprintf(stderr, "ERROR: no cameras streaming.\n");
        CameraManager::X().Shutdown();
        return 1;
    }

    std::printf("Streaming %zu camera(s) in ascending ID order. Ctrl+C to stop.\n", streams.size());
    std::fflush(stdout);

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
