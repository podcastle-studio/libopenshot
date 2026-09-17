#include "SkiaRenderer.h"

#include "skia/include/core/SkFontMgr.h"
#include "skia/include/core/SkFontArguments.h"
#include "skia/include/core/SkFontParameters.h"
#include "skia/include/core/SkFourByteTag.h"
#include "skia/include/core/SkMaskFilter.h"
#include "skia/include/core/SkBlurTypes.h"
#include "skia/include/core/SkSpan.h"
#include "skia/include/ports/SkFontMgr_fontconfig.h"
#include "skia/include/ports/SkFontScanner_FreeType.h"
#include "skia/include/effects/SkGradient.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <map>
#include <mutex>
#include <vector>

namespace openshot {
namespace subtitle {

namespace {

// OpenType variation axis tag for weight ("wght").
constexpr SkFourByteTag kWeightAxisTag = SkSetFourByteTag('w', 'g', 'h', 't');

// Build the OpenType style (weight + slant) the caller is asking for.
SkFontStyle makeFontStyle(const FontProps& fontProps) {
    const int weight = fontProps.fontWeight > 0 ? fontProps.fontWeight : SkFontStyle::kNormal_Weight;
    const SkFontStyle::Slant slant =
        fontProps.italic ? SkFontStyle::kItalic_Slant : SkFontStyle::kUpright_Slant;
    return SkFontStyle(weight, SkFontStyle::kNormal_Width, slant);
}

// If `typeface` is a variable font exposing a weight ("wght") axis, return a clone pinned
// to `weight` (clamped to the axis range) so we render a genuine heavier cut. Returns the
// original typeface unchanged when it is not variable / has no weight axis / cloning fails.
sk_sp<SkTypeface> applyWeightVariation(sk_sp<SkTypeface> typeface, int weight) {
    if (!typeface) return typeface;

    // M147: getVariationDesignParameters takes an SkSpan; an empty span returns the axis count.
    const int axisCount = typeface->getVariationDesignParameters(
        SkSpan<SkFontParameters::Variation::Axis>{});
    if (axisCount <= 0) return typeface;

    std::vector<SkFontParameters::Variation::Axis> axes(axisCount);
    if (typeface->getVariationDesignParameters(SkSpan<SkFontParameters::Variation::Axis>(axes))
            != axisCount) {
        return typeface;
    }

    for (const auto& axis : axes) {
        if (axis.tag != kWeightAxisTag) continue;

        const float clamped = std::clamp(static_cast<float>(weight), axis.min, axis.max);
        const SkFontArguments::VariationPosition::Coordinate coord{kWeightAxisTag, clamped};
        SkFontArguments args;
        args.setVariationDesignPosition({&coord, 1});
        if (sk_sp<SkTypeface> cloned = typeface->makeClone(args)) {
            return cloned;
        }
        break;
    }
    return typeface;
}

// The weight this typeface actually renders at: the pinned "wght" variation coordinate for
// a variable instance, otherwise its static design weight. Used to decide whether a real
// bold cut was obtained before falling back to synthetic emboldening.
int effectiveWeight(const sk_sp<SkTypeface>& typeface) {
    if (!typeface) return SkFontStyle::kNormal_Weight;

    // M147: getVariationDesignPosition takes an SkSpan; an empty span returns the axis count.
    const int count = typeface->getVariationDesignPosition(
        SkSpan<SkFontArguments::VariationPosition::Coordinate>{});
    if (count > 0) {
        std::vector<SkFontArguments::VariationPosition::Coordinate> coords(count);
        if (typeface->getVariationDesignPosition(
                SkSpan<SkFontArguments::VariationPosition::Coordinate>(coords)) == count) {
            for (const auto& c : coords) {
                if (c.axis == kWeightAxisTag) return static_cast<int>(c.value);
            }
        }
    }
    return typeface->fontStyle().weight();
}

// Faux bold / faux italic apply ONLY where the resolved `typeface` cannot supply the requested
// style for real. When a genuine bold (or italic/oblique) face was matched we leave the glyphs
// untouched so the designed cut is rendered instead of a synthetic one.
//
// These are predicates rather than mutators so the answer can be cached next to the typeface —
// effectiveWeight() is a FreeType round-trip and the result depends only on
// (typeface, requested style).
bool wantsSyntheticBold(const sk_sp<SkTypeface>& typeface, const SkFontStyle& requested) {
    return requested.weight() >= SkFontStyle::kMedium_Weight
        && effectiveWeight(typeface) < SkFontStyle::kMedium_Weight;
}

bool wantsSyntheticItalic(const sk_sp<SkTypeface>& typeface, const SkFontStyle& requested) {
    const SkFontStyle::Slant actualSlant =
        typeface ? typeface->fontStyle().slant() : SkFontStyle::kUpright_Slant;
    return requested.slant() != SkFontStyle::kUpright_Slant
        && actualSlant == SkFontStyle::kUpright_Slant;
}

// A resolved typeface plus the synthetic-styling decisions that go with it. Both halves are
// cached together because deciding them (effectiveWeight -> getVariationDesignPosition) is a
// FreeType round-trip, and it depends only on (typeface, requested style) — the cache key.
struct ResolvedFace {
    sk_sp<SkTypeface> typeface;
    bool embolden = false;
    bool skew = false;
};

// Key for the shared typeface cache: the family name *or* font-file path, the requested
// weight/slant, and (for the per-character fallback path) the character that must be covered.
// character == kAnyChar means "no coverage requirement" (the plain getTypeface path).
constexpr SkUnichar kAnyChar = -1;

struct FaceKey {
    std::string familyOrPath;
    int weight;
    int slant;
    SkUnichar character;

    bool operator<(const FaceKey& o) const {
        if (character != o.character) return character < o.character;
        if (weight != o.weight) return weight < o.weight;
        if (slant != o.slant) return slant < o.slant;
        return familyOrPath < o.familyOrPath;
    }
};

// Process-wide font resources, shared by every SkiaRenderer.
//
// WHY THIS IS A SINGLETON: both callers of SkiaRenderer construct one *per rendered frame*
// (SubtitleManager::renderAtFrame, TextClipReader::renderToQImage). Everything below is
// expensive and frame-invariant:
//
//   * SkFontMgr_New_FontConfig  — loads the whole fontconfig configuration: ~16 ms a pop.
//   * makeFromFile(<font>)      — the service hands us a downloaded font *file path*, so this
//                                 opens and (for .woff) decompresses the font: ~1 ms a pop,
//                                 and it used to run once per DISTINCT CHARACTER per frame
//                                 because the fallback cache key includes the character.
//   * coverage probing / weight-axis cloning — more FreeType face work on top.
//
// Together that was ~75 ms per subtitled frame at 720p, all of it recomputed from scratch
// every frame and then thrown away with the renderer. Hoisting it into a shared cache makes
// it a one-time cost for the whole export.
//
// Thread safety: guarded by a mutex. SkTypeface itself is immutable and safe to use from
// several threads once created, so handing out sk_sp copies needs no further locking.
class SkiaFontResources {
public:
    static SkiaFontResources& Instance() {
        static SkiaFontResources instance;
        return instance;
    }

    // Resolve (and cache) the typeface for a family/path at a style, optionally constrained to
    // cover `character`. Mirrors the original SkiaRenderer::getTypeface /
    // getTypefaceForCharacter logic exactly; only the caching layer changed.
    ResolvedFace resolve(const std::string& familyOrPath, const SkFontStyle& style,
                         const SkUnichar character) {
        const FaceKey key{familyOrPath, style.weight(), static_cast<int>(style.slant()), character};

        {
            const std::lock_guard<std::mutex> lock(mutex);
            if (const auto it = cache.find(key); it != cache.end()) return it->second;
        }

        // Resolution runs outside the lock: it is the slow part, and doing it twice on a race
        // is harmless (both threads produce an equivalent face; one insert wins).
        ResolvedFace resolved;
        resolved.typeface = character == kAnyChar ? matchAny(familyOrPath, style)
                                                 : matchCovering(familyOrPath, style, character);
        resolved.embolden = wantsSyntheticBold(resolved.typeface, style);
        resolved.skew     = wantsSyntheticItalic(resolved.typeface, style);

        const std::lock_guard<std::mutex> lock(mutex);
        // Bound the cache so a long-lived service that renders many different fonts cannot grow
        // without limit (each entry pins a font file's tables in memory). Entries are cheap to
        // rebuild, so a wholesale clear is a fine eviction policy for something this rare.
        if (cache.size() >= kMaxEntries) cache.clear();
        cache[key] = resolved;
        return resolved;
    }

private:
    SkiaFontResources() {
        // M147: SkFontMgr_New_FontConfig now requires an explicit font scanner.
        mgr = SkFontMgr_New_FontConfig(nullptr, SkFontScanner_Make_FreeType());
        if (!mgr) {
            mgr = SkFontMgr::RefEmpty();
        }
    }

    // Resolve a single family name or font-file path to the typeface whose design most
    // closely matches `style`. For an installed family this returns the real bold / italic
    // cut when the family ships one; for a variable-font file it pins the weight axis to
    // the requested weight. No synthetic styling happens here.
    sk_sp<SkTypeface> matchTypeface(const std::string& familyOrPath, const SkFontStyle& style) const {
        if (std::filesystem::is_regular_file(familyOrPath)) {
            // A font *file* is a fixed face. The exception is a variable font that exposes a
            // weight axis — pin it to the requested weight so we get a genuine heavier cut
            // instead of falling back to synthetic emboldening later.
            return applyWeightVariation(mgr->makeFromFile(familyOrPath.c_str()), style.weight());
        }
        // matchFamilyStyle returns the installed face closest to `style`; when a real bold (or
        // italic) cut exists in the family it is returned here.
        return mgr->matchFamilyStyle(familyOrPath.c_str(), style);
    }

    sk_sp<SkTypeface> matchAny(const std::string& familyOrPath, const SkFontStyle& style) const {
        sk_sp<SkTypeface> typeface = matchTypeface(familyOrPath, style);
        if (!typeface) { // last-chance fallback
            typeface = mgr->matchFamilyStyle(nullptr, style);
        }
        return typeface;
    }

    sk_sp<SkTypeface> matchCovering(const std::string& familyOrPath, const SkFontStyle& style,
                                    const SkUnichar character) const {
        auto covers = [character](const sk_sp<SkTypeface>& typeface) {
            return typeface && SkFont(typeface).unicharToGlyph(character) != 0;
        };

        // 1) The requested family name or explicit file-path, at the requested style. When the
        //    family ships a real bold / italic cut (or the file is a variable font) this is it.
        sk_sp<SkTypeface> typeface = matchTypeface(familyOrPath, style);
        if (covers(typeface)) return typeface;

        // 2) Preferred fallback: Noto Sans Arabic, 3) Secondary fallback: FreeSans.
        //    Matched at the same style so fallback glyphs keep the requested weight / slant.
        for (const char* fallback : {"Noto Sans Arabic", "FreeSans"}) {
            typeface = mgr->matchFamilyStyle(fallback, style);
            if (covers(typeface)) return typeface;
        }

        // 4) Last-chance fallback: whatever FontConfig thinks best for this style
        return mgr->matchFamilyStyle(nullptr, style);
    }

    static constexpr size_t kMaxEntries = 4096;

    sk_sp<SkFontMgr> mgr;
    std::mutex mutex;
    std::map<FaceKey, ResolvedFace> cache;
};

} // namespace

SkiaRenderer::SkiaRenderer(SkCanvas* canvas, ColorConvention convention)
    : canvas(canvas), convention(convention) {
    // Touch the shared cache here so the one-time fontconfig load happens at construction
    // rather than inside the first glyph measurement.
    SkiaFontResources::Instance();
}

namespace {

// Turn a cached ResolvedFace into the SkFont the callers expect.
SkFont makeFont(const ResolvedFace& face, const double fontSize) {
    SkFont skFont(face.typeface, fontSize);
    if (face.embolden) skFont.setEmbolden(true);
    if (face.skew) skFont.setSkewX(-0.10f);
    skFont.setEdging(SkFont::Edging::kAntiAlias);
    return skFont;
}

} // namespace

SkFont SkiaRenderer::getFont(const FontProps& fontProps) {
    if (const auto it = fontCache.find(fontProps); it != fontCache.end()) {
        return it->second;
    }

    const SkFontStyle style = makeFontStyle(fontProps);
    const ResolvedFace face =
        SkiaFontResources::Instance().resolve(fontProps.fontFamily, style, kAnyChar);
    SkFont skFont = makeFont(face, fontProps.fontSize);

    fontCache[fontProps] = skFont;
    return skFont;
}

sk_sp<SkTypeface> SkiaRenderer::getTypefaceForCharacter(const std::string& familyOrPath, const SkUnichar character, const SkFontStyle& style)
{
    return SkiaFontResources::Instance().resolve(familyOrPath, style, character).typeface;
}

SkFont SkiaRenderer::getFontForCharacter(const FontProps& fontProps, const SkUnichar character) {
    const FontCharKey key{fontProps, character};

    if (const auto it = fontCharCache.find(key); it != fontCharCache.end()) {
        return it->second;
    }

    const SkFontStyle style = makeFontStyle(fontProps);
    const ResolvedFace face =
        SkiaFontResources::Instance().resolve(fontProps.fontFamily, style, character);
    SkFont skFont = makeFont(face, fontProps.fontSize);

    fontCharCache[key] = skFont;
    return skFont;
}

SkPaint* SkiaRenderer::getPaint(const PaintProps& paintProps) {
    if (const auto it = paintCache.find(paintProps); it != paintCache.end()) {
        return it->second.get();
    }

    auto paint = std::make_unique<SkPaint>();
    paint->setColor(parseColorString(paintProps.color, paintProps.opacity));
    paint->setAntiAlias(true);

    if (paintProps.strokeWidth.has_value()) {
        paint->setStyle(SkPaint::kStroke_Style);
        paint->setStrokeWidth(paintProps.strokeWidth.value());
        paint->setStrokeCap(SkPaint::kRound_Cap);
        paint->setStrokeJoin(SkPaint::kRound_Join);
    }

    if (paintProps.maskBlur.has_value()) {
        paint->setMaskFilter(SkMaskFilter::MakeBlur(kNormal_SkBlurStyle, paintProps.maskBlur.value(), false));
    }

    SkPaint* paintPtr = paint.get();
    paintCache[paintProps] = std::move(paint);
    return paintPtr;
}

sk_sp<SkShader> SkiaRenderer::makeLinearGradientShader(
    const SkPoint pts[2],
    const std::vector<std::pair<std::string, double>>& stops)
{
    if (stops.size() < 2) return nullptr;

    std::vector<SkColor4f> colors;
    std::vector<float> positions;
    colors.reserve(stops.size());
    positions.reserve(stops.size());
    for (const auto& stop : stops) {
        // parseColorString already applies this renderer's colour convention; keep the stop
        // opaque unless the colour itself carries alpha (opacity arg = 1.0).
        colors.push_back(SkColor4f::FromColor(parseColorString(stop.first, 1.0f)));
        positions.push_back(static_cast<float>(std::clamp(stop.second, 0.0, 1.0)));
    }

    const SkGradient::Colors gradColors(
        SkSpan<const SkColor4f>(colors.data(), colors.size()),
        SkSpan<const float>(positions.data(), positions.size()),
        SkTileMode::kClamp);
    const SkGradient gradient(gradColors, SkGradient::Interpolation{});
    return SkShaders::LinearGradient(pts, gradient, nullptr);
}

SkColor SkiaRenderer::parseColorString(const std::string& colorStr, const float opacity) {
    int r = 255, g = 255, b = 255;
    double a = 1.0;            // colour's own alpha (0..1)
    bool parsed = false;

    if (!colorStr.empty() && colorStr[0] == '#') {
        // #rrggbb or #rrggbbaa
        try {
            if (colorStr.size() >= 7) {
                r = std::stoi(colorStr.substr(1, 2), nullptr, 16);
                g = std::stoi(colorStr.substr(3, 2), nullptr, 16);
                b = std::stoi(colorStr.substr(5, 2), nullptr, 16);
                if (colorStr.size() >= 9) {
                    a = std::stoi(colorStr.substr(7, 2), nullptr, 16) / 255.0;
                }
                parsed = true;
            }
        } catch (...) {}
    } else if (colorStr.rfind("rgba", 0) == 0 || colorStr.rfind("RGBA", 0) == 0 ||
               colorStr.rfind("rgb", 0) == 0  || colorStr.rfind("RGB", 0) == 0) {
        // rgba(r, g, b, a) or rgb(r, g, b). %d skips leading whitespace, so the
        // spaces after commas are handled.
        int rr = 0, gg = 0, bb = 0;
        double aa = 1.0;
        if (std::sscanf(colorStr.c_str(), "rgba(%d,%d,%d,%lf)", &rr, &gg, &bb, &aa) >= 3 ||
            std::sscanf(colorStr.c_str(), "RGBA(%d,%d,%d,%lf)", &rr, &gg, &bb, &aa) >= 3 ||
            std::sscanf(colorStr.c_str(), "rgb(%d,%d,%d)",      &rr, &gg, &bb)      == 3 ||
            std::sscanf(colorStr.c_str(), "RGB(%d,%d,%d)",      &rr, &gg, &bb)      == 3) {
            r = rr; g = gg; b = bb; a = aa;
            parsed = true;
        }
    }

    if (!parsed) {
        // Unknown format → opaque white fallback (legacy behaviour).
        return SkColorSetARGB(static_cast<U8CPU>(std::lround(255 * opacity)), 255, 255, 255);
    }

    const int alpha = std::clamp(static_cast<int>(std::lround(a * opacity * 255.0)), 0, 255);
    r = std::clamp(r, 0, 255);
    g = std::clamp(g, 0, 255);
    b = std::clamp(b, 0, 255);

    // The swap cancels the one reinterpretation every CPU path ends at: N32-declared
    // bytes handed to a QImage that declares them Format_RGBA8888. A renderer whose
    // result is never reinterpreted asks for Logical and gets the colour as written.
    // See ColorConvention in the header for why this is not a canvas property.
    if (convention == ColorConvention::Logical)
        return SkColorSetARGB(static_cast<U8CPU>(alpha), r, g, b);
    return SkColorSetARGB(static_cast<U8CPU>(alpha), b, g, r);
}

} // namespace subtitle
} // namespace openshot