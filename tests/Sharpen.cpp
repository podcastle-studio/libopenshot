// tests/SharpenEffectTest.cpp
/**
 * @file
 * @brief Minimal unit tests for openshot::Sharpen effect using a tiny synthetic frame
 * @author Jonathan Thomas <jonathan@openshot.org>
 *
 * @ref License
 */

// Copyright (c) 2008-2025 OpenShot Studios, LLC
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include <memory>
#include <QImage>
#include <QColor>
#include "Frame.h"
#include "effects/Sharpen.h"
#include "openshot_catch.h"

using namespace openshot;

// allow Catch2 to print QColor on failure
static std::ostream& operator<<(std::ostream& os, QColor const& c)
{
    os << "QColor(" << c.red() << "," << c.green()
       << "," << c.blue() << "," << c.alpha() << ")";
    return os;
}

// Create a tiny 3x3 test frame: all pixels=128, center pixel=100
static std::shared_ptr<Frame> makeTinyFrame()
{
    QImage img(3, 3, QImage::Format_ARGB32);
    img.fill(QColor(128,128,128,255));
    img.setPixelColor(1, 1, QColor(100,100,100,255));
    auto frame = std::make_shared<Frame>();
    *frame->GetImage() = img;
    return frame;
}

TEST_CASE("sharpen with zero threshold does not alter tiny image", "[effect][sharpen]")
{
    Sharpen effect;
    effect.amount    = Keyframe(1.0);
    effect.radius    = Keyframe(1.0);
    effect.threshold = Keyframe(0.0);  // no sharpening allowed

    auto frame = makeTinyFrame();
    int before = QColor(frame->GetImage()->pixelColor(1,1)).value();

    // Debug save of output frame
    auto out_frame0 = effect.GetFrame(frame, 1);
    int after0 = QColor(out_frame0->GetImage()->pixelColor(1,1)).value();

    CHECK(after0 == before);
}

TEST_CASE("sharpen with nonzero threshold alters tiny image", "[effect][sharpen]")
{
    Sharpen effect;
    effect.amount    = Keyframe(1.0);
    effect.radius    = Keyframe(1.0);
    effect.threshold = Keyframe(1.0);  // allow sharpening

    auto frame = makeTinyFrame();
    int before = QColor(frame->GetImage()->pixelColor(1,1)).value();

    // Debug save of output frame
    auto out_frame1 = effect.GetFrame(frame, 1);
    int after1 = QColor(out_frame1->GetImage()->pixelColor(1,1)).value();

    CHECK(after1 != before);
}