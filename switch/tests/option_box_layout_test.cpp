#include "ui/option_box_layout.hpp"
#include "nanovg.h"
#include <cassert>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    assert(argc == 2); // Host font path; no Nintendo font is distributed.
    NVGparams params{};
    params.renderCreate = [](void*) { return 1; };
    params.renderCreateTexture = [](void*, int, int, int, int, const unsigned char*) { return 1; };
    params.renderDeleteTexture = [](void*, int) { return 1; };
    params.renderUpdateTexture = [](void*, int, int, int, int, int, const unsigned char*) { return 1; };
    params.renderViewport = [](void*, float, float, float) {};
    auto* vg = nvgCreateInternal(&params);
    assert(vg && nvgCreateFont(vg, "Standard", argv[1]) >= 0);
    nvgFontFace(vg, "Standard");

    const auto check = [&](const std::string& text, bool image, bool should_scroll) {
        nvgFontSize(vg, image ? 22.f : 24.f);
        nvgTextLineHeight(vg, 1.35f);
        nvgTextAlign(vg, (image ? NVG_ALIGN_LEFT : NVG_ALIGN_CENTER) | NVG_ALIGN_TOP);
        const float width = 770 - 64 - (image ? 182 : 0);
        float bounds[4];
        nvgTextBoxBounds(vg, 0, 0, width, text.c_str(), nullptr, bounds);
        const float text_height = bounds[3] - bounds[1];
        const auto layout = sphaira::ui::CalculateOptionBoxLayout(text_height, image);
        assert(layout.height >= 295 && layout.height <= 600);
        assert(layout.scrollable == should_scroll);
        const float top = (720 - layout.height) / 2;
        const float buttons = top + layout.height - 75;
        const float viewport_end = top + 32 + layout.text_area_height;
        assert(viewport_end <= buttons - 32);
        if (!should_scroll) assert(text_height <= layout.text_area_height);
        else assert(layout.height == 600 && text_height > layout.text_area_height);
        if (image) assert(layout.height - 75 >= 150 + 64);
        return layout.height;
    };

    assert(check("Reboot now?", false, false) == 295);
    const std::string reported_warning = "Latest HATS supports firmware up to 23.0.1.\n\n"
        "Installed HATS compatibility could not be verified.\n"
        "Update HATS before installing firmware.\nContinue only if you accept the risk.";
    check(reported_warning, false, false);
    assert(check("Install firmware 23.0.1 on emuMMC?\n\nCurrent firmware: 23.0.1\n"
        "Running: Magic Monkei build (AMS 1.12.1)\n\nFAT32 + exFAT support\n"
        "Do not power off the console during installation.", false, false) > 295);
    check("WARNING: This is a DOWNGRADE!\n\nCurrent: 23.0.1\nTarget: 22.0.0\n\n"
        "Install firmware 22.0.0 on emuMMC?\n\nCurrent firmware: 23.0.1\n"
        "Running: Magic Monkei build (AMS 1.12.1)\n\nFAT32 + exFAT support\n"
        "Do not power off the console during installation.", false, false);
    check("An update is available.\nDownload, install and reload this app?", true, false);
    std::string long_message;
    for (int i = 0; i < 100; ++i) long_message += "A long wrapped message to exercise scrolling without overlapping either button.\n";
    check(long_message, false, true);
    check(long_message, true, true);
    nvgDeleteInternal(vg);
    std::cout << "NanoVG-measured dialogs: reported warning, firmware confirmations, images and scrolling passed.\n";
}
