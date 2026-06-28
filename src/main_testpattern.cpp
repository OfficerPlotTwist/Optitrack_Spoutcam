// main_testpattern.cpp
//
// Sends a synthetic animated 8-bit grayscale feed as the Spout sender
// "OptiTrackCam" (or a name/size given on the command line). Needs NO OptiTrack
// Camera SDK, so it builds and runs anywhere with a GPU. Use it to verify the
// Spout pipeline end-to-end (TouchDesigner Spout In TOP) before the real camera
// app is wired up.
//
//   spout_testpattern [width] [height] [senderName]
//
#include "spout_grayscale_sender.h"

#include <windows.h>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <vector>

static volatile bool g_run = true;
static void onSignal(int) { g_run = false; }

int main(int argc, char** argv) {
    signal(SIGINT, onSignal);

    unsigned int w = (argc > 1) ? (unsigned)std::atoi(argv[1]) : 512u;
    unsigned int h = (argc > 2) ? (unsigned)std::atoi(argv[2]) : 512u;
    const char* name = (argc > 3) ? argv[3] : "OptiTrackCam";
    if (w == 0) w = 512;
    if (h == 0) h = 512;

    SpoutGrayscaleSender sender;
    if (!sender.init(name)) {
        std::fprintf(stderr, "ERROR: Spout DirectX init failed.\n");
        return 2;
    }

    std::printf("Sending grayscale test pattern as '%s' %ux%u. Ctrl+C to stop.\n",
                name, w, h);

    std::vector<uint8_t> gray(size_t(w) * h);
    unsigned int t = 0;
    while (g_run) {
        // Moving diagonal gradient + a sweeping bright bar, so it is obvious in TD.
        const unsigned int bar = (t * 3u) % w;
        for (unsigned int y = 0; y < h; ++y) {
            uint8_t* row = gray.data() + size_t(y) * w;
            for (unsigned int x = 0; x < w; ++x) {
                uint8_t v = uint8_t((x + y + t) & 0xFF);
                unsigned int dx = (x > bar) ? (x - bar) : (bar - x);
                if (dx < 4) v = 255; // bright vertical bar
                row[x] = v;
            }
        }
        sender.sendGrayscale(gray.data(), w, h, w);
        ++t;
        Sleep(16); // ~60 fps
    }

    sender.shutdown();
    std::printf("Stopped.\n");
    return 0;
}
