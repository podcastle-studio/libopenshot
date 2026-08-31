#pragma once

// Colour-emoji support: Unicode cluster segmentation, the pinned emoji typeface, and
// HarfBuzz shaping of one emoji cluster into glyphs.
//
// Why this exists at all: the text painters resolve a font and draw glyphs ONE CODEPOINT at a
// time (SkFont::textToGlyphs, which is a bare cmap lookup). That is fine for letters, but no
// emoji beyond the simplest ones is a single codepoint — ❤️ is U+2764 U+FE0F, 👍🏽 is a base plus
// a skin-tone modifier, 👨‍👩‍👧 is three people joined by ZWJ, 🇺🇸 is two regional indicators, 1️⃣ is
// digit + VS16 + keycap. Those are GSUB ligatures in the emoji font, so a cmap-only lookup
// returns the pieces (or .notdef) instead of the one composed glyph. Correct emoji therefore
// needs two things this header provides: grouping the codepoints into clusters, and shaping
// each cluster through HarfBuzz.
//
// HarfBuzz is not a new dependency: Skia bundles it inside libskia and exports the hb_* symbols,
// so we only need its headers (see cmake/Modules/FindSkia.cmake). When they are not available
// the build still works — shaping degrades to the cluster's base codepoint.

#include "EmojiPass.h"

#include <skia/include/core/SkData.h>
#include <skia/include/core/SkFont.h>
#include <skia/include/core/SkFontMgr.h>
#include <skia/include/core/SkPoint.h>
#include <skia/include/core/SkRefCnt.h>
#include <skia/include/core/SkTypeface.h>
#include <skia/include/core/SkTypes.h>

#include <map>
#include <string>
#include <vector>

namespace openshot {
namespace subtitle {

// ---------------------------------------------------------------------------
// Unicode classification (the UTS #51 subset we need)
// ---------------------------------------------------------------------------

// Emoji_Presentation=Yes: a codepoint that is emoji by default, so it must come from the colour
// emoji font even when a text font happens to carry a monochrome glyph for it (😀 ⌚ ✅ …).
bool isEmojiPresentation(SkUnichar uc);

// Extended_Pictographic-ish: a codepoint that CAN be emoji but defaults to text presentation
// (❤ ♀ ☠ …). Only used to decide whether a codepoint continues an emoji cluster; on its own
// such a codepoint stays with the text font, which is what browsers do.
bool isEmojiPictographic(SkUnichar uc);

// A codepoint that only ever extends the cluster before it: VS15/VS16, skin-tone modifiers,
// the keycap enclosure, and the tag characters used by subdivision flags.
bool isEmojiExtender(SkUnichar uc);

// U+1F1E6..U+1F1FF — two in a row form a flag.
bool isRegionalIndicator(SkUnichar uc);

// ---------------------------------------------------------------------------
// Cluster segmentation
// ---------------------------------------------------------------------------

// Byte length of the cluster starting at text[i]: one codepoint plus everything that extends it
// (variation selectors, skin tones, keycaps, tag sequences, ZWJ-joined members), or a regional
// indicator pair. Non-emoji text yields exactly one codepoint per call, so ordinary strings
// segment the same way they always did.
//
// This is deliberately UTS #51 emoji sequences only, NOT full UAX #29 grapheme breaking: it is
// small enough to re-implement bit-identically in the front end, which is what keeps the editor
// preview and the exported video on the same per-character index space (letter counts drive the
// char-stagger animations).
size_t emojiClusterLen(const std::string& text, size_t i);

// Should this cluster be drawn from the colour emoji font? True for emoji-presentation
// codepoints and for any multi-codepoint emoji sequence; false for a bare text-presentation
// codepoint (❤ with no VS16) and for anything explicitly text-presented with VS15.
bool isEmojiCluster(const std::string& cluster);

// ---------------------------------------------------------------------------
// Shaping
// ---------------------------------------------------------------------------

// One emoji cluster shaped into glyphs of the emoji typeface at a given size. Positions are
// relative to the pen, in Skia's y-down space.
struct ShapedEmoji {
    std::vector<SkGlyphID> glyphs;
    std::vector<SkPoint> positions;
    float advance = 0.0f;
};

// The colour emoji face plus a shaped-cluster cache. One instance lives on SkiaRenderer.
class EmojiFont {
public:
    explicit EmojiFont(const sk_sp<SkFontMgr>& fontMgr);
    ~EmojiFont();

    EmojiFont(const EmojiFont&) = delete;
    EmojiFont& operator=(const EmojiFont&) = delete;

    // Null when no emoji font could be found; callers then fall back to the text font.
    const sk_sp<SkTypeface>& typeface() const { return typeface_; }
    bool available() const { return typeface_ != nullptr; }

    // SkFont for the emoji face at `size`. Never synthetically emboldened or skewed — a colour
    // glyph carries its own artwork, and faux styling would smear it.
    SkFont font(float size);

    // Shape one cluster. Returns null when there is no emoji face, or when the face cannot draw
    // this cluster (so the caller can fall back to the text font). Cached per (cluster, size).
    const ShapedEmoji* shape(const std::string& cluster, float size);

    // Where the face was loaded from (a file path, or the matched family name). For diagnostics.
    const std::string& source() const { return source_; }

private:
    void resolveTypeface(const sk_sp<SkFontMgr>& fontMgr);
    void openHarfBuzz();
    void closeHarfBuzz();
    ShapedEmoji shapeUncached(const std::string& cluster, float size);

    sk_sp<SkTypeface> typeface_;
    sk_sp<SkData> fontData_;
    std::string source_;
    std::map<std::string, SkFont> fontCache_;
    std::map<std::string, ShapedEmoji> shapeCache_;

    // Opaque hb_blob_t*/hb_face_t*/hb_font_t* so this header does not need the HarfBuzz headers.
    void* hbBlob_ = nullptr;
    void* hbFace_ = nullptr;
    void* hbFont_ = nullptr;
    int upem_ = 0;
};

} // namespace subtitle
} // namespace openshot
