// spout_grayscale_sender.h
//
// Thin wrapper around SpoutDX for publishing an 8-bit grayscale image as a
// Spout sender. SpoutDX expects RGBA (4 bytes/pixel), so this class expands
// the single-channel grayscale buffer to RGBA (R=G=B=gray, A=255) internally.
//
// Deliberately keeps SpoutDX.h out of this header (opaque impl pointer) so the
// Camera SDK translation unit doesn't also pull in the Spout headers.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

class SpoutGrayscaleSender {
public:
    SpoutGrayscaleSender();
    ~SpoutGrayscaleSender();

    SpoutGrayscaleSender(const SpoutGrayscaleSender&) = delete;
    SpoutGrayscaleSender& operator=(const SpoutGrayscaleSender&) = delete;

    // Create the Spout sender (initialises a private DirectX 11 device).
    // Returns false if DirectX init fails.
    bool init(const std::string& senderName);

    // Publish one 8-bit grayscale frame.
    //   gray     : pointer to width*height bytes, row-major, top-left origin.
    //   srcPitch : source row stride in bytes (0 => width).
    // The frame is expanded to RGBA and sent under the sender name. Resolution
    // changes between calls are handled automatically.
    bool sendGrayscale(const uint8_t* gray, unsigned int width,
                       unsigned int height, unsigned int srcPitch = 0);

    void shutdown();

private:
    void* m_sender = nullptr;       // spoutDX*, opaque to consumers
    std::vector<uint8_t> m_rgba;    // reused RGBA scratch buffer
    unsigned int m_w = 0;
    unsigned int m_h = 0;
    std::string m_name;
    bool m_ready = false;
};
