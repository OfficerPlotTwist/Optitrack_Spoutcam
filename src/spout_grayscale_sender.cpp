// spout_grayscale_sender.cpp
#include "spout_grayscale_sender.h"

#include "SpoutDX.h"

SpoutGrayscaleSender::SpoutGrayscaleSender() = default;

SpoutGrayscaleSender::~SpoutGrayscaleSender() {
    shutdown();
}

bool SpoutGrayscaleSender::init(const std::string& senderName) {
    m_name = senderName;
    auto* s = new spoutDX();
    // Create a private DX11 device. SpoutDX is DirectX-based and needs no
    // OpenGL context or window, so this works fine from a console app.
    if (!s->OpenDirectX11()) {
        delete s;
        m_sender = nullptr;
        return false;
    }
    s->SetSenderName(m_name.c_str());
    m_sender = s;
    return true;
}

bool SpoutGrayscaleSender::sendGrayscale(const uint8_t* gray, unsigned int width,
                                         unsigned int height, unsigned int srcPitch) {
    if (!m_sender || !gray || width == 0 || height == 0) return false;
    if (srcPitch == 0) srcPitch = width;

    // (Re)size the RGBA scratch buffer when the resolution changes.
    if (width != m_w || height != m_h || m_rgba.size() != size_t(width) * height * 4) {
        m_w = width;
        m_h = height;
        m_rgba.assign(size_t(width) * height * 4, 0);
    }

    // Expand grayscale -> RGBA (R=G=B=gray, A=255).
    for (unsigned int y = 0; y < height; ++y) {
        const uint8_t* srcRow = gray + size_t(y) * srcPitch;
        uint8_t* dstRow = m_rgba.data() + size_t(y) * width * 4;
        for (unsigned int x = 0; x < width; ++x) {
            const uint8_t g = srcRow[x];
            uint8_t* p = dstRow + size_t(x) * 4;
            p[0] = g;  // R
            p[1] = g;  // G
            p[2] = g;  // B
            p[3] = 255; // A
        }
    }

    auto* s = static_cast<spoutDX*>(m_sender);
    return s->SendImage(m_rgba.data(), width, height);
}

void SpoutGrayscaleSender::shutdown() {
    if (m_sender) {
        auto* s = static_cast<spoutDX*>(m_sender);
        s->ReleaseSender();
        s->CloseDirectX11();
        delete s;
        m_sender = nullptr;
    }
    m_ready = false;
}
