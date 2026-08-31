#include "EmojiFont.h"

#include <skia/include/core/SkSpan.h>
#include <skia/include/core/SkStream.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <memory>

#ifdef OPENSHOT_HAVE_HARFBUZZ
#include <hb.h>
#endif

namespace openshot {
namespace subtitle {

namespace {

constexpr SkUnichar kZWJ         = 0x200D;
constexpr SkUnichar kVS15        = 0xFE0E;   // text presentation selector
constexpr SkUnichar kVS16        = 0xFE0F;   // emoji presentation selector
constexpr SkUnichar kKeycap      = 0x20E3;   // combining enclosing keycap
constexpr SkUnichar kSkinFirst   = 0x1F3FB;
constexpr SkUnichar kSkinLast    = 0x1F3FF;
constexpr SkUnichar kTagFirst    = 0xE0020;
constexpr SkUnichar kTagLast     = 0xE007F;
constexpr SkUnichar kProbeEmoji  = 0x1F600;  // 😀 — coverage probe for a candidate emoji face

struct Range { SkUnichar first, last; };

bool inRanges(SkUnichar uc, const Range* ranges, size_t count) {
    // Tables are sorted, so a binary search keeps this cheap enough to call per glyph.
    size_t lo = 0, hi = count;
    while (lo < hi) {
        const size_t mid = lo + (hi - lo) / 2;
        if (uc < ranges[mid].first)      hi = mid;
        else if (uc > ranges[mid].last)  lo = mid + 1;
        else                             return true;
    }
    return false;
}

// Emoji_Presentation=Yes (Unicode 15.1, with the 1FAxx blocks widened to absorb later additions:
// over-inclusion inside the emoji planes is harmless because every candidate face is checked for
// actual coverage before it is used).
constexpr Range kPresentation[] = {
    {0x231A, 0x231B}, {0x23E9, 0x23EC}, {0x23F0, 0x23F0}, {0x23F3, 0x23F3},
    {0x25FD, 0x25FE}, {0x2614, 0x2615}, {0x2648, 0x2653}, {0x267F, 0x267F},
    {0x2693, 0x2693}, {0x26A1, 0x26A1}, {0x26AA, 0x26AB}, {0x26BD, 0x26BE},
    {0x26C4, 0x26C5}, {0x26CE, 0x26CE}, {0x26D4, 0x26D4}, {0x26EA, 0x26EA},
    {0x26F2, 0x26F3}, {0x26F5, 0x26F5}, {0x26FA, 0x26FA}, {0x26FD, 0x26FD},
    {0x2705, 0x2705}, {0x270A, 0x270B}, {0x2728, 0x2728}, {0x274C, 0x274C},
    {0x274E, 0x274E}, {0x2753, 0x2755}, {0x2757, 0x2757}, {0x2795, 0x2797},
    {0x27B0, 0x27B0}, {0x27BF, 0x27BF}, {0x2B1B, 0x2B1C}, {0x2B50, 0x2B50},
    {0x2B55, 0x2B55},
    {0x1F004, 0x1F004}, {0x1F0CF, 0x1F0CF}, {0x1F18E, 0x1F18E}, {0x1F191, 0x1F19A},
    {0x1F1E6, 0x1F1FF}, {0x1F201, 0x1F201}, {0x1F21A, 0x1F21A}, {0x1F22F, 0x1F22F},
    {0x1F232, 0x1F236}, {0x1F238, 0x1F23A}, {0x1F250, 0x1F251},
    {0x1F300, 0x1F320}, {0x1F32D, 0x1F335}, {0x1F337, 0x1F37C}, {0x1F37E, 0x1F393},
    {0x1F3A0, 0x1F3CA}, {0x1F3CF, 0x1F3D3}, {0x1F3E0, 0x1F3F0}, {0x1F3F4, 0x1F3F4},
    {0x1F3F8, 0x1F43E}, {0x1F440, 0x1F440}, {0x1F442, 0x1F4FC}, {0x1F4FF, 0x1F53D},
    {0x1F54B, 0x1F54E}, {0x1F550, 0x1F567}, {0x1F57A, 0x1F57A}, {0x1F595, 0x1F596},
    {0x1F5A4, 0x1F5A4}, {0x1F5FB, 0x1F64F}, {0x1F680, 0x1F6C5}, {0x1F6CC, 0x1F6CC},
    {0x1F6D0, 0x1F6D2}, {0x1F6D5, 0x1F6D7}, {0x1F6DC, 0x1F6DF}, {0x1F6EB, 0x1F6EC},
    {0x1F6F4, 0x1F6FC}, {0x1F7E0, 0x1F7EB}, {0x1F7F0, 0x1F7F0},
    {0x1F90C, 0x1F93A}, {0x1F93C, 0x1F945}, {0x1F947, 0x1F9FF},
    {0x1FA70, 0x1FA7C}, {0x1FA80, 0x1FA89}, {0x1FA8F, 0x1FAC6}, {0x1FACE, 0x1FADC},
    {0x1FADF, 0x1FAE9}, {0x1FAF0, 0x1FAF8},
};

// Extended_Pictographic, coarsened to whole blocks. Only consulted for cluster continuation
// after a ZWJ and for "could this base start an emoji cluster", so a wide net costs nothing.
constexpr Range kPictographic[] = {
    {0x00A9, 0x00A9}, {0x00AE, 0x00AE}, {0x203C, 0x203C}, {0x2049, 0x2049},
    {0x2122, 0x2122}, {0x2139, 0x2139}, {0x2194, 0x21AA}, {0x231A, 0x231B},
    {0x2328, 0x2328}, {0x2388, 0x2388}, {0x23CF, 0x23FA}, {0x24C2, 0x24C2},
    {0x25AA, 0x25FE}, {0x2600, 0x27BF}, {0x2934, 0x2935}, {0x2B00, 0x2B55},
    {0x3030, 0x3030}, {0x303D, 0x303D}, {0x3297, 0x3297}, {0x3299, 0x3299},
    {0x1F000, 0x1FAFF}, {0x1FC00, 0x1FFFD},
};

// Candidate emoji faces, most specific first. The COLRv1 build of Noto Color Emoji is the one the
// front end loads (vector colour glyphs: crisp at export resolution, half the bytes of the CBDT
// build, and identical advances) — the rest are dev-box / distro fallbacks so a machine without
// the pinned file still renders emoji. Override with OPENSHOT_EMOJI_FONT.
constexpr const char* kEmojiFontPaths[] = {
    "/usr/share/fonts/truetype/custom/NotoColorEmoji-COLRv1.ttf",
    "/usr/share/fonts/truetype/custom/NotoColorEmoji.ttf",
    "/usr/share/fonts/opentype/noto/NotoColorEmoji-COLRv1.ttf",
    "/usr/share/fonts/truetype/noto/NotoColorEmoji.ttf",
    "/usr/share/fonts/NotoColorEmoji.ttf",
};

// UTF-8 primitives. Mirror of the inline pair in text/TextDrawShared.h — kept local so the
// subtitle layer stays independent of the text layer; change both together.
size_t charLen(const std::string& s, size_t i) {
    if (i >= s.size()) return 0;
    const auto b = static_cast<unsigned char>(s[i]);
    if ((b & 0x80) == 0) return 1;
    if ((b & 0xE0) == 0xC0 && i + 1 < s.size()) return 2;
    if ((b & 0xF0) == 0xE0 && i + 2 < s.size()) return 3;
    if ((b & 0xF8) == 0xF0 && i + 3 < s.size()) return 4;
    return 1;
}

SkUnichar decode(const std::string& s, size_t i, size_t len) {
    if (len == 0) return 0;
    const char* p = s.c_str() + i;
    const auto b0 = static_cast<unsigned char>(p[0]);
    if (len == 1) return b0;
    if (len == 2) return ((b0 & 0x1F) << 6) | (static_cast<unsigned char>(p[1]) & 0x3F);
    if (len == 3) {
        return ((b0 & 0x0F) << 12) |
               ((static_cast<unsigned char>(p[1]) & 0x3F) << 6) |
               (static_cast<unsigned char>(p[2]) & 0x3F);
    }
    return ((b0 & 0x07) << 18) |
           ((static_cast<unsigned char>(p[1]) & 0x3F) << 12) |
           ((static_cast<unsigned char>(p[2]) & 0x3F) << 6) |
           (static_cast<unsigned char>(p[3]) & 0x3F);
}

std::vector<SkUnichar> decodeAll(const std::string& s) {
    std::vector<SkUnichar> out;
    size_t i = 0;
    while (i < s.size()) {
        const size_t len = charLen(s, i);
        if (len == 0) break;
        out.push_back(decode(s, i, len));
        i += len;
    }
    return out;
}

bool covers(const sk_sp<SkTypeface>& typeface, SkUnichar uc) {
    return typeface && SkFont(typeface).unicharToGlyph(uc) != 0;
}

} // namespace

// ---------------------------------------------------------------------------
// Classification
// ---------------------------------------------------------------------------

bool isEmojiPresentation(SkUnichar uc) {
    return inRanges(uc, kPresentation, std::size(kPresentation));
}

bool isEmojiPictographic(SkUnichar uc) {
    return inRanges(uc, kPictographic, std::size(kPictographic));
}

bool isEmojiExtender(SkUnichar uc) {
    return uc == kVS15 || uc == kVS16 || uc == kKeycap ||
           (uc >= kSkinFirst && uc <= kSkinLast) ||
           (uc >= kTagFirst && uc <= kTagLast);
}

bool isRegionalIndicator(SkUnichar uc) {
    return uc >= 0x1F1E6 && uc <= 0x1F1FF;
}

// ---------------------------------------------------------------------------
// Segmentation
// ---------------------------------------------------------------------------

size_t emojiClusterLen(const std::string& text, size_t i) {
    const size_t n = text.size();
    if (i >= n) return 0;

    const size_t baseLen = charLen(text, i);
    const SkUnichar base = decode(text, i, baseLen);
    size_t end = i + baseLen;

    // A flag is exactly two regional indicators; a third starts a new flag.
    if (isRegionalIndicator(base)) {
        if (end < n) {
            const size_t nextLen = charLen(text, end);
            if (nextLen > 0 && isRegionalIndicator(decode(text, end, nextLen))) end += nextLen;
        }
        return end - i;
    }

    const bool pictographicBase = isEmojiPictographic(base);

    while (end < n) {
        const size_t len = charLen(text, end);
        if (len == 0) break;
        const SkUnichar uc = decode(text, end, len);

        // Variation selectors / skin tones / keycaps / tags always belong to the base before
        // them, whatever that base is (a digit takes VS16 + keycap: 1️⃣).
        if (isEmojiExtender(uc)) { end += len; continue; }

        // A ZWJ joins the next pictographic codepoint into the same emoji (👨‍👩‍👧). A trailing or
        // unjoinable ZWJ is left outside the cluster rather than swallowing unrelated text.
        if (uc == kZWJ && pictographicBase) {
            const size_t after = end + len;
            if (after >= n) break;
            const size_t memberLen = charLen(text, after);
            if (memberLen == 0 || !isEmojiPictographic(decode(text, after, memberLen))) break;
            end = after + memberLen;
            continue;
        }
        break;
    }
    return end - i;
}

bool isEmojiCluster(const std::string& cluster) {
    if (cluster.empty()) return false;
    const std::vector<SkUnichar> cps = decodeAll(cluster);
    if (cps.empty()) return false;

    if (cps.size() == 1) return isEmojiPresentation(cps[0]);

    // VS15 asks for text presentation explicitly (❤︎) — honour it and stay with the text font.
    if (std::find(cps.begin(), cps.end(), kVS15) != cps.end()) return false;
    // Keycaps are the one sequence whose base is not pictographic (1️⃣ #️⃣ *️⃣).
    if (std::find(cps.begin(), cps.end(), kKeycap) != cps.end()) return true;
    if (isRegionalIndicator(cps[0])) return true;
    if (!isEmojiPictographic(cps[0])) return false;   // e.g. a letter with a combining mark

    for (const SkUnichar uc : cps) {
        if (uc == kVS16 || uc == kZWJ) return true;                  // ❤️  👨‍👩‍👧
        if (uc >= kSkinFirst && uc <= kSkinLast) return true;        // 👍🏽
        if (uc >= kTagFirst && uc <= kTagLast) return true;          // 🏴󠁧󠁢󠁳󠁣󠁴󠁿
    }
    return isEmojiPresentation(cps[0]);
}

// ---------------------------------------------------------------------------
// EmojiFont
// ---------------------------------------------------------------------------

EmojiFont::EmojiFont(const sk_sp<SkFontMgr>& fontMgr) {
    resolveTypeface(fontMgr);
    if (typeface_) openHarfBuzz();
}

EmojiFont::~EmojiFont() {
    closeHarfBuzz();
}

void EmojiFont::resolveTypeface(const sk_sp<SkFontMgr>& fontMgr) {
    if (!fontMgr) return;

    auto tryFile = [&](const std::string& path) {
        if (path.empty() || !std::filesystem::is_regular_file(path)) return false;
        sk_sp<SkTypeface> candidate = fontMgr->makeFromFile(path.c_str());
        if (!covers(candidate, kProbeEmoji)) return false;
        typeface_ = std::move(candidate);
        source_ = path;
        return true;
    };

    if (const char* env = std::getenv("OPENSHOT_EMOJI_FONT"); env && *env) {
        if (tryFile(env)) return;
    }
    for (const char* path : kEmojiFontPaths) {
        if (tryFile(path)) return;
    }

    // Last resort: whatever fontconfig calls "Noto Color Emoji". Resolving by family is not the
    // primary route on purpose — fontconfig's character fallback happily returns monochrome
    // outline faces (DejaVu Sans, FreeSerif) for emoji codepoints, which would silently render
    // black-and-white emoji that do not match the editor.
    const SkFontStyle style(SkFontStyle::kNormal_Weight, SkFontStyle::kNormal_Width,
                            SkFontStyle::kUpright_Slant);
    for (const char* family : {"Noto Color Emoji", "Noto Emoji"}) {
        sk_sp<SkTypeface> candidate = fontMgr->matchFamilyStyle(family, style);
        if (!covers(candidate, kProbeEmoji)) continue;
        typeface_ = std::move(candidate);
        source_ = family;
        return;
    }
}

void EmojiFont::openHarfBuzz() {
#ifdef OPENSHOT_HAVE_HARFBUZZ
    if (!typeface_) return;
    int ttcIndex = 0;
    std::unique_ptr<SkStreamAsset> stream = typeface_->openStream(&ttcIndex);
    if (!stream || stream->getLength() == 0) return;
    fontData_ = SkData::MakeFromStream(stream.get(), stream->getLength());
    if (!fontData_ || fontData_->isEmpty()) return;

    hb_blob_t* blob = hb_blob_create(static_cast<const char*>(fontData_->data()),
                                     static_cast<unsigned>(fontData_->size()),
                                     HB_MEMORY_MODE_READONLY, nullptr, nullptr);
    if (!blob) return;
    hb_face_t* face = hb_face_create(blob, static_cast<unsigned>(ttcIndex));
    if (!face) { hb_blob_destroy(blob); return; }
    hb_font_t* font = hb_font_create(face);
    if (!font) { hb_face_destroy(face); hb_blob_destroy(blob); return; }

    upem_ = static_cast<int>(hb_face_get_upem(face));
    if (upem_ <= 0) { hb_font_destroy(font); hb_face_destroy(face); hb_blob_destroy(blob); return; }
    // Shape in font units and scale to pixels ourselves, so one shaped result could serve any
    // size without re-running the shaper (and so rounding happens once, in float).
    hb_font_set_scale(font, upem_, upem_);

    hbBlob_ = blob;
    hbFace_ = face;
    hbFont_ = font;
#endif
}

void EmojiFont::closeHarfBuzz() {
#ifdef OPENSHOT_HAVE_HARFBUZZ
    if (hbFont_) hb_font_destroy(static_cast<hb_font_t*>(hbFont_));
    if (hbFace_) hb_face_destroy(static_cast<hb_face_t*>(hbFace_));
    if (hbBlob_) hb_blob_destroy(static_cast<hb_blob_t*>(hbBlob_));
#endif
    hbFont_ = hbFace_ = hbBlob_ = nullptr;
}

SkFont EmojiFont::font(float size) {
    const std::string key = std::to_string(size);
    if (const auto it = fontCache_.find(key); it != fontCache_.end()) return it->second;

    SkFont font(typeface_, size);
    font.setEdging(SkFont::Edging::kAntiAlias);
    fontCache_[key] = font;
    return font;
}

ShapedEmoji EmojiFont::shapeUncached(const std::string& cluster, float size) {
    ShapedEmoji out;
    if (!typeface_ || cluster.empty() || size <= 0.0f) return out;

#ifdef OPENSHOT_HAVE_HARFBUZZ
    if (hbFont_ && upem_ > 0) {
        hb_buffer_t* buf = hb_buffer_create();
        hb_buffer_add_utf8(buf, cluster.c_str(), static_cast<int>(cluster.size()), 0, -1);
        hb_buffer_guess_segment_properties(buf);
        hb_shape(static_cast<hb_font_t*>(hbFont_), buf, nullptr, 0);

        unsigned count = 0;
        const hb_glyph_info_t* info = hb_buffer_get_glyph_infos(buf, &count);
        const hb_glyph_position_t* pos = hb_buffer_get_glyph_positions(buf, &count);
        const float scale = size / static_cast<float>(upem_);
        bool ok = count > 0 && info && pos;
        float pen = 0.0f;
        for (unsigned g = 0; ok && g < count; ++g) {
            // .notdef anywhere means this face cannot draw the cluster; report failure so the
            // caller can fall back to the text font instead of drawing tofu.
            if (info[g].codepoint == 0) { ok = false; break; }
            out.glyphs.push_back(static_cast<SkGlyphID>(info[g].codepoint));
            out.positions.push_back({pen + static_cast<float>(pos[g].x_offset) * scale,
                                     -static_cast<float>(pos[g].y_offset) * scale});
            pen += static_cast<float>(pos[g].x_advance) * scale;
        }
        hb_buffer_destroy(buf);
        if (ok) { out.advance = pen; return out; }
        out.glyphs.clear();
        out.positions.clear();
    }
#endif

    // No HarfBuzz (or the shaper failed): draw the cluster's base codepoint from the emoji face.
    // Sequences lose their composition (👍🏽 → 👍, 👨‍👩‍👧 → 👨) but stay recognisable instead of
    // vanishing, and single-codepoint emoji — the common case — are unaffected.
    const SkUnichar base = decode(cluster, 0, charLen(cluster, 0));
    SkFont skFont = font(size);
    const SkGlyphID glyph = skFont.unicharToGlyph(base);
    if (glyph == 0) return out;
    SkScalar width = 0;
    skFont.getWidths(SkSpan<const SkGlyphID>(&glyph, 1), SkSpan<SkScalar>(&width, 1));
    out.glyphs.push_back(glyph);
    out.positions.push_back({0.0f, 0.0f});
    out.advance = static_cast<float>(width);
    return out;
}

const ShapedEmoji* EmojiFont::shape(const std::string& cluster, float size) {
    if (!typeface_ || cluster.empty() || size <= 0.0f) return nullptr;

    const std::string key = cluster + "@" + std::to_string(size);
    auto it = shapeCache_.find(key);
    if (it == shapeCache_.end()) {
        // Failures are cached as an empty result so an unsupported cluster is only shaped once.
        it = shapeCache_.emplace(key, shapeUncached(cluster, size)).first;
    }
    return it->second.glyphs.empty() ? nullptr : &it->second;
}

} // namespace subtitle
} // namespace openshot
