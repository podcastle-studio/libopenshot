/**
 * @file
 * @brief Unit tests for openshot::subtitle emoji cluster segmentation
 * @author Jonathan Thomas <jonathan@openshot.org>
 *
 * @ref License
 */

// Copyright (c) 2008-2019 OpenShot Studios, LLC
//
// SPDX-License-Identifier: LGPL-3.0-or-later

// These lock down the rule that decides what ONE letter is. It matters beyond emoji looking
// right: the letter count drives line wrapping and the per-character animation stagger, so the
// front end has to segment identically or the editor preview and the exported video disagree
// about which character animates when. Any change here is a change to that shared contract.

#include <string>
#include <vector>

#include "openshot_catch.h"

#include "subtitle/EmojiFont.h"

using openshot::subtitle::emojiClusterLen;
using openshot::subtitle::isEmojiCluster;
using openshot::subtitle::isEmojiPresentation;
using openshot::subtitle::isRegionalIndicator;

namespace {

// Split a string the way the text layer does (see openshot::text::forEachCluster).
std::vector<std::string> clusters(const std::string& text) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i < text.size()) {
        const size_t len = emojiClusterLen(text, i);
        if (len == 0) break;
        out.push_back(text.substr(i, len));
        i += len;
    }
    return out;
}

// Written as escapes on purpose: several of these (VS15/VS16, ZWJ, the keycap enclosure) are
// invisible, and a literal copy would be impossible to review or to keep intact through an edit.
const std::string kGrin   = "\U0001F600";   // 😀 emoji presentation
const std::string kHeart  = "\u2764";       // ❤  text presentation
const std::string kVS16   = "\uFE0F";       // emoji presentation selector
const std::string kVS15   = "\uFE0E";       // text presentation selector
const std::string kThumb  = "\U0001F44D";
const std::string kSkin   = "\U0001F3FD";   // skin-tone modifier
const std::string kZWJ    = "\u200D";
const std::string kMan    = "\U0001F468";
const std::string kWoman  = "\U0001F469";
const std::string kGirl   = "\U0001F467";
const std::string kRegU   = "\U0001F1FA";   // regional indicator U
const std::string kRegS   = "\U0001F1F8";   // regional indicator S
const std::string kKeycap = "\u20E3";       // combining enclosing keycap
const std::string kArabicBeh = "\u0628";    // ب
const std::string kArabicTeh = "\u062A";    // ت

} // namespace

TEST_CASE( "plain text segments one codepoint per letter", "[libopenshot][emoji]" )
{
    CHECK(clusters("abc") == std::vector<std::string>{"a", "b", "c"});

    // Multi-byte, non-emoji: Arabic letters stay one codepoint each.
    const auto arabic = clusters(kArabicBeh + kArabicTeh);
    CHECK(arabic.size() == 2);
    CHECK(arabic[0] == kArabicBeh);

    CHECK(clusters("").empty());
}

TEST_CASE( "emoji sequences stay one letter", "[libopenshot][emoji]" )
{
    SECTION("single codepoint") {
        CHECK(clusters(kGrin) == std::vector<std::string>{kGrin});
    }
    SECTION("variation selector 16") {
        CHECK(clusters(kHeart + kVS16) == std::vector<std::string>{kHeart + kVS16});
    }
    SECTION("skin tone modifier") {
        CHECK(clusters(kThumb + kSkin) == std::vector<std::string>{kThumb + kSkin});
    }
    SECTION("ZWJ sequence") {
        const std::string family = kMan + kZWJ + kWoman + kZWJ + kGirl;
        CHECK(clusters(family) == std::vector<std::string>{family});
    }
    SECTION("regional indicator pair") {
        CHECK(clusters(kRegU + kRegS) == std::vector<std::string>{kRegU + kRegS});
    }
    SECTION("keycap") {
        CHECK(clusters("1" + kVS16 + kKeycap) == std::vector<std::string>{"1" + kVS16 + kKeycap});
    }
    SECTION("mixed with text") {
        CHECK(clusters("a" + kGrin + "b") == std::vector<std::string>{"a", kGrin, "b"});
    }
}

TEST_CASE( "emoji cluster boundaries do not over-merge", "[libopenshot][emoji]" )
{
    SECTION("a third regional indicator starts a new flag") {
        // Four regional indicators are two flags, not one four-part cluster.
        const auto out = clusters(kRegU + kRegS + kRegU + kRegS);
        CHECK(out.size() == 2);
        CHECK(out[0] == kRegU + kRegS);
    }
    SECTION("a trailing ZWJ is left out of the cluster") {
        const auto out = clusters(kGrin + kZWJ);
        CHECK(out.size() == 2);
        CHECK(out[0] == kGrin);
    }
    SECTION("a ZWJ before ordinary text does not swallow it") {
        const auto out = clusters(kGrin + kZWJ + "a");
        REQUIRE(out.size() == 3);
        CHECK(out[0] == kGrin);
        CHECK(out[2] == "a");
    }
    SECTION("two adjacent emoji are two letters") {
        CHECK(clusters(kGrin + kThumb) == std::vector<std::string>{kGrin, kThumb});
    }
}

TEST_CASE( "emoji vs text presentation", "[libopenshot][emoji]" )
{
    // Emoji by default → drawn from the colour emoji font even alone.
    CHECK(isEmojiCluster(kGrin));
    CHECK(isEmojiPresentation(0x1F600));

    // Text by default: a bare heart stays with the text font, VS16 promotes it to emoji, and
    // VS15 keeps it text even in a longer cluster. This is what browsers do.
    CHECK_FALSE(isEmojiCluster(kHeart));
    CHECK_FALSE(isEmojiPresentation(0x2764));
    CHECK(isEmojiCluster(kHeart + kVS16));
    CHECK_FALSE(isEmojiCluster(kHeart + kVS15));

    CHECK(isEmojiCluster(kThumb + kSkin));
    CHECK(isEmojiCluster(kMan + kZWJ + kWoman + kZWJ + kGirl));
    CHECK(isEmojiCluster(kRegU + kRegS));
    CHECK(isEmojiCluster("1" + kVS16 + kKeycap));
    CHECK(isRegionalIndicator(0x1F1FA));

    // Ordinary text is never an emoji cluster.
    CHECK_FALSE(isEmojiCluster("a"));
    CHECK_FALSE(isEmojiCluster(""));
    CHECK_FALSE(isEmojiCluster(kArabicBeh));
}
