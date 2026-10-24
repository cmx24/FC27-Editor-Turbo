// Native tests (3.1 R2, brief 3.1-gui track render): the looks studio's full-body crop (core/image.h content_box, grow_box,
// crop_image, fit_height). The game draws him small in the middle of a square picture on a plain background; the studio crops to
// the box of what stands on that background (plus a 4 % margin) and fits it to the pane's height (upscaling allowed).
// Included by test_main.cpp just before test_capture_sequence.h; image_crop_test::run() is called from test_capture_sequence().
#pragma once
#include <cstdint>
#include <cstdlib>
#include <string>

#include "core/image.h"

namespace image_crop_test {

// w x h of one colour (alpha a) with a rectangle of another colour at (x, y, rw, rh)
inline turbo::Rgba picture(int w, int h, const uint8_t bg[4], int x, int y, int rw, int rh, const uint8_t fg[4]) {
    turbo::Rgba img;
    img.w = w;
    img.h = h;
    img.px.resize(size_t(w) * size_t(h) * 4);
    for (int j = 0; j < h; ++j)
        for (int i = 0; i < w; ++i) {
            const bool in = i >= x && i < x + rw && j >= y && j < y + rh;
            uint8_t* p = img.at(i, j);
            for (int c = 0; c < 4; ++c) p[c] = in ? fg[c] : bg[c];
        }
    return img;
}

inline bool within_px(int a, int b, int tol) { return std::abs(a - b) <= tol; }

inline std::string box_text(const turbo::Box& b) {
    return "(" + std::to_string(b.x0) + "," + std::to_string(b.y0) + ")-(" + std::to_string(b.x1) + "," + std::to_string(b.y1) + ")";
}

inline void run() {
    run_case("image crop: a 60x200 body on a flat 256x256 border -> box (98,28)-(158,228) within 4 px, fit to 640 high = 192x640 (3.1 R2)", [&] {
        const uint8_t grey[4] = {128, 128, 128, 255};
        const uint8_t red[4] = {200, 40, 40, 255};
        const turbo::Rgba img = picture(256, 256, grey, 98, 28, 60, 200, red);
        turbo::Box b;
        CHECK(turbo::content_box(img, b), "a body on a plain border has a box");
        CHECK(within_px(b.x0, 98, 4) && within_px(b.y0, 28, 4) && within_px(b.x1, 158, 4) && within_px(b.y1, 228, 4), "box " + box_text(b));
        const turbo::Rgba him = turbo::crop_image(img, b);
        CHECK(!him.empty() && him.w == b.w() && him.h == b.h(), "crop = the box: " + std::to_string(him.w) + "x" + std::to_string(him.h));
        const turbo::Rgba fit = turbo::fit_height(him, 640);
        CHECK(fit.w == 192 && fit.h == 640, "fit to 640 high: " + std::to_string(fit.w) + "x" + std::to_string(fit.h));
        if (!fit.empty()) {
            const uint8_t* c = fit.at(fit.w / 2, fit.h / 2);
            CHECK(within_px(c[0], 200, 8) && within_px(c[1], 40, 8) && within_px(c[2], 40, 8) && c[3] == 255, "the middle is him (red), not the border");
        }
        // the studio's own steps: a 4 % margin of his box, then fit to the pane's height
        const turbo::Box g = turbo::grow_box(b, 0.04f, img.w, img.h);
        CHECK(g.x0 <= b.x0 && g.y0 <= b.y0 && g.x1 >= b.x1 && g.y1 >= b.y1 && g.x0 >= 0 && g.y0 >= 0 && g.x1 <= 256 && g.y1 <= 256,
              "the margin grows the box inside the picture: " + box_text(g));
        const turbo::Rgba shown = turbo::fit_height(turbo::crop_image(img, g), 640);
        CHECK(shown.h == 640 && within_px(shown.w, 190, 3), "with the margin: " + std::to_string(shown.w) + "x" + std::to_string(shown.h) + " (about 190x640)");
        // the old route (fit the whole square to the pane) drew him 60/256 of the pane wide: the crop is what makes him big
        const turbo::Rgba whole = turbo::fit_image(img, 640);
        CHECK(whole.w == 640 && whole.h == 640, "the whole square fits to 640x640 (the pre-3.1 route)");
    });

    run_case("image crop: a transparent border -> the box of the opaque pixels; a margin is clamped to the picture", [&] {
        const uint8_t clear[4] = {0, 0, 0, 0};
        const uint8_t blue[4] = {30, 120, 200, 255};
        const turbo::Rgba img = picture(256, 256, clear, 98, 28, 60, 200, blue);
        turbo::Box b;
        CHECK(turbo::content_box(img, b) && within_px(b.x0, 98, 4) && within_px(b.y0, 28, 4) && within_px(b.x1, 158, 4) && within_px(b.y1, 228, 4),
              "box " + box_text(b));
        turbo::Box edge;
        edge.x0 = 2;
        edge.y0 = 1;
        edge.x1 = 254;
        edge.y1 = 255;
        const turbo::Box g = turbo::grow_box(edge, 0.5f, 256, 256);
        CHECK(g.x0 == 0 && g.y0 == 0 && g.x1 == 256 && g.y1 == 256, "clamped: " + box_text(g));
    });

    run_case("image crop: no plain border, or nothing on it -> no box (the studio keeps the whole picture)", [&] {
        const uint8_t grey[4] = {128, 128, 128, 255};
        turbo::Rgba plain = picture(128, 128, grey, 0, 0, 0, 0, grey);
        turbo::Box b;
        CHECK(!turbo::content_box(plain, b), "one colour: nothing stands on it");
        turbo::Rgba noisy = plain;
        uint32_t s = 12345u;
        for (size_t i = 0; i < noisy.px.size(); ++i) {
            if (i % 4 == 3) continue;
            s = s * 1664525u + 1013904223u;
            noisy.px[i] = static_cast<uint8_t>(s >> 24);
        }
        CHECK(!turbo::content_box(noisy, b), "a noisy border is no background");
        CHECK(turbo::crop_image(plain, turbo::Box()).empty(), "an empty box crops to an empty picture");
        CHECK(turbo::fit_height(turbo::Rgba(), 640).empty(), "nothing to fit gives nothing");
    });
}

}  // namespace image_crop_test
