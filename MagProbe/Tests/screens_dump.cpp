// Render every screen through the same Render::render() the firmware calls and
// write the framebuffers out as PPM, so a layout can be inspected without a
// watch. This is what stands in for the TouchGFX simulator an app with a
// CustomGUI front end does not have.
//
// The panel is 2 bits a channel and masks a round area out of the square
// framebuffer, so the conversion below expands each channel to 8 bits and
// blacks out the corners. That makes the dump match what the glass shows rather
// than what the buffer holds -- including the corners a layout must not use.

#include "Canvas.hpp"
#include "Render.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace {

constexpr uint16_t kW = 240;
constexpr uint16_t kH = 240;

/// Expand one 2-bit channel to 8 bits: 0, 85, 170, 255.
uint8_t expand(uint8_t twoBits)
{
    return static_cast<uint8_t>(twoBits * 85u);
}

bool writePpm(const std::string& path, const std::vector<uint8_t>& fb)
{
    FILE* f = std::fopen(path.c_str(), "wb");
    if (f == nullptr) {
        return false;
    }
    std::fprintf(f, "P6\n%u %u\n255\n", kW, kH);

    const float cx = (kW - 1) / 2.0f;
    const float cy = (kH - 1) / 2.0f;
    const float r  = kW / 2.0f;

    for (uint16_t y = 0; y < kH; ++y) {
        for (uint16_t x = 0; x < kW; ++x) {
            const uint8_t px = fb[static_cast<size_t>(y) * kW + x];

            const float dx = static_cast<float>(x) - cx;
            const float dy = static_cast<float>(y) - cy;
            const bool  visible = (dx * dx + dy * dy) <= (r * r);

            uint8_t rgb[3] = {0, 0, 0};
            if (visible) {
                rgb[0] = expand(px & 0x3u);
                rgb[1] = expand((px >> 2) & 0x3u);
                rgb[2] = expand((px >> 4) & 0x3u);
            }
            std::fwrite(rgb, 1, 3, f);
        }
    }
    std::fclose(f);
    return true;
}

Render::View live()
{
    Render::View v;
    v.resolve        = Mag::Resolve::Resolved;
    v.delivery       = Mag::Delivery::Delivering;
    v.shape          = Mag::FrameShape::ThreeAxis;
    v.fieldCount     = 3;
    v.stride         = 24;
    v.samples        = 4210;
    v.batches        = 352;
    v.ageMs          = 180;
    v.units          = Mag::Units::Microtesla;
    v.raw            = Mag::Vec3{18.44f, -31.02f, 39.60f};
    v.corrected      = v.raw;
    v.magnitude      = 52.75f;
    v.spreadFraction = 0.031f;
    v.rawBits0       = 0x41938F5Cu;
    v.rawInt0        = 1100255580;
    v.accelResolve   = Mag::Resolve::Resolved;
    v.accelFresh     = true;
    v.haveHeading    = true;
    v.headingDeg     = 137.5f;
    v.dipDeg         = 68.2f;
    v.levelled       = true;
    v.calQuality     = Mag::HardIron::Quality::Usable;
    v.calSamples     = 612;
    v.calOffsets     = Mag::Vec3{118.5f, -44.2f, 31.9f};
    v.calSpans       = Mag::Vec3{101.2f, 98.7f, 94.4f};
    v.uptimeMs       = 74210;
    return v;
}

Render::View noProducer()
{
    Render::View v;
    v.resolve      = Mag::Resolve::NoProducer;
    v.accelResolve = Mag::Resolve::Resolved;
    v.uptimeMs     = 4200;
    return v;
}

struct Fixture {
    const char*  name;
    Render::View view;
};

} // namespace

int main()
{
    const Fixture fixtures[] = {
        {"live", live()},
        {"noproducer", noProducer()},
        {"fresh", Render::View{}},
    };

    std::vector<uint8_t> fb(static_cast<size_t>(kW) * kH, 0);
    int written = 0;

    for (const Fixture& fx : fixtures) {
        for (uint8_t s = 0; s < Render::kScreenCount; ++s) {
            const Render::Screen screen = static_cast<Render::Screen>(s);

            Canvas canvas(fb.data(), kW, kH, fb.size());
            Render::render(canvas, screen, fx.view);

            std::string path = std::string(fx.name) + "-" + Render::name(screen) + ".ppm";
            for (char& ch : path) {
                if (ch >= 'A' && ch <= 'Z') {
                    ch = static_cast<char>(ch - 'A' + 'a');
                }
            }

            if (!writePpm(path, fb)) {
                std::fprintf(stderr, "could not write %s\n", path.c_str());
                return 1;
            }
            std::printf("%s\n", path.c_str());
            ++written;
        }
    }

    std::printf("%d framebuffers written\n", written);
    return 0;
}
