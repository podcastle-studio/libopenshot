/**
 * @file
 * @brief Unit tests for openshot::ChromaKey effect
 * @author Jonathan Thomas <jonathan@openshot.org>
 * @author FeRD (Frank Dana) <ferdnyc@gmail.com>
 *
 * @ref License
 */

// Copyright (c) 2008-2021 OpenShot Studios, LLC
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include <sstream>
#include <memory>

#include "Frame.h"
#include "effects/ChromaKey.h"

#include <QColor>
#include <QImage>

// Stream output formatter for QColor, needed so Catch2 can display
// values when CHECK(qcolor1 == qcolor2) comparisons fail
std::ostream& operator << ( std::ostream& os, QColor const& value ) {
    os << "QColor(" << value.red() << ", " << value.green() << ", "
       << value.blue() << ", " << value.alpha() << ")";
    return os;
}

#include "openshot_catch.h"

using namespace openshot;

TEST_CASE( "basic keying", "[libopenshot][effect][chromakey]" )
{
    // solid green frame
    auto f = std::make_shared<openshot::Frame>(1, 1280, 720, "#00ff00");

    // Create a ChromaKey effect to key on solid green ± 5 values
    openshot::Color key(0, 255, 0, 255);
    openshot::Keyframe fuzz(5);
    openshot::ChromaKey e(key, fuzz);

    auto f_out = e.GetFrame(f, 1);
    std::shared_ptr<QImage> i = f_out->GetImage();

    // Check color fill (should be transparent)
    QColor pix = i->pixelColor(10, 10);
    QColor trans{Qt::transparent};
    CHECK(pix == trans);
}

TEST_CASE( "threshold", "[libopenshot][effect][chromakey]" )
{
    auto frame = std::make_shared<openshot::Frame>(1, 1280, 720, "#00cc00");

    // Create a ChromaKey effect to key on solid green ± 5 values
    openshot::Color key(0, 255, 0, 255);
    openshot::Keyframe fuzz(5);
    openshot::ChromaKey e(key, fuzz);

    auto frame_out = e.GetFrame(frame, 1);
    std::shared_ptr<QImage> i = frame_out->GetImage();

    // Output should be the same, no ChromaKey
    QColor pix_e = i->pixelColor(10, 10);
    QColor expected(0, 204, 0, 255);
    CHECK(pix_e == expected);
}

TEST_CASE( "default method is basic soft", "[libopenshot][effect][chromakey][json]" )
{
    openshot::ChromaKey e;
    Json::Value json = e.JsonValue();
    CHECK(json["keymethod"].asInt() == CHROMAKEY_BASIC_SOFT);
}

TEST_CASE( "basic vs basic soft halo behavior", "[libopenshot][effect][chromakey]" )
{
    // Pick a green value in the halo band for key=(0,255,0), threshold=5, halo=20.
    // For BASIC this should remain unchanged, for BASIC_SOFT it should be partially faded.
    auto frame_basic = std::make_shared<openshot::Frame>(1, 320, 180, "#00fa00");
    auto frame_soft = std::make_shared<openshot::Frame>(1, 320, 180, "#00fa00");

    openshot::Color key(0, 255, 0, 255);
    openshot::Keyframe fuzz(5);
    openshot::Keyframe halo(20);

    openshot::ChromaKey basic(key, fuzz, halo, CHROMAKEY_BASIC);
    openshot::ChromaKey soft(key, fuzz, halo, CHROMAKEY_BASIC_SOFT);

    auto out_basic = basic.GetFrame(frame_basic, 1);
    auto out_soft = soft.GetFrame(frame_soft, 1);

    QColor basic_pix = out_basic->GetImage()->pixelColor(10, 10);
    QColor soft_pix = out_soft->GetImage()->pixelColor(10, 10);

    CHECK(basic_pix == QColor(0, 250, 0, 255));
    CHECK(soft_pix.alpha() < 255);
    CHECK(soft_pix.alpha() > 0);
    CHECK(soft_pix != basic_pix);
}

TEST_CASE( "json roundtrip preserves method", "[libopenshot][effect][chromakey][json]" )
{
    openshot::Color key(0, 255, 0, 255);
    openshot::Keyframe fuzz(5);
    openshot::Keyframe halo(20);
    openshot::ChromaKey basic(key, fuzz, halo, CHROMAKEY_BASIC);
    openshot::ChromaKey soft(key, fuzz, halo, CHROMAKEY_BASIC_SOFT);

    Json::Value basic_json = basic.JsonValue();
    Json::Value soft_json = soft.JsonValue();

    openshot::ChromaKey basic_loaded;
    openshot::ChromaKey soft_loaded;
    basic_loaded.SetJsonValue(basic_json);
    soft_loaded.SetJsonValue(soft_json);

    CHECK(basic_loaded.JsonValue()["keymethod"].asInt() == CHROMAKEY_BASIC);
    CHECK(soft_loaded.JsonValue()["keymethod"].asInt() == CHROMAKEY_BASIC_SOFT);
}

TEST_CASE( "SetJson string preserves keymethod", "[libopenshot][effect][chromakey][json]" )
{
    openshot::ChromaKey source(openshot::Color(0, 255, 0, 255), openshot::Keyframe(5), openshot::Keyframe(20), CHROMAKEY_BASIC_SOFT);
    const std::string payload = source.Json();

    openshot::ChromaKey loaded;
    loaded.SetJson(payload);

    CHECK(loaded.JsonValue()["keymethod"].asInt() == CHROMAKEY_BASIC_SOFT);
}
