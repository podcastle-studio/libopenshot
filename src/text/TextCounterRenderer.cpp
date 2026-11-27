#include "TextCounterRenderer.h"

#include "../gpu/GpuOffscreen.h"
#include "../subtitle/SkiaRenderer.h"
#include "TextAnimationRenderer.h"
#include "TextClipRenderer.h"
#include "TextCurvedText.h"
#include "TextDrawShared.h"
#include "TextGlowRenderer.h"
#include "TextGlowShader.h"

#include <skia/include/core/SkCanvas.h>
#include <skia/include/core/SkColorType.h>
#include <skia/include/core/SkFontMetrics.h>
#include <skia/include/core/SkImage.h>
#include <skia/include/core/SkMatrix.h>
#include <skia/include/core/SkPaint.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>

namespace openshot {
namespace text {

namespace C = counter;

namespace {

constexpr int MAX_SHUTTER_SAMPLES = 48;
// Accumulation buffers above this many pixels fall back to the frame-time sample alone.
constexpr long long MAX_ACCUMULATION_PIXELS = 4096LL * 4096LL;

// ── Measurement ──────────────────────────────────────────────────────────────

std::vector<std::string> clusters(const std::string& s) {
    std::vector<std::string> out;
    forEachCluster(s, [&](const std::string& letter, SkUnichar) { out.push_back(letter); });
    return out;
}

double letterWidth(const CounterRenderPlan& plan, subtitle::SkiaRenderer* renderer, const std::string& letter) {
    auto it = plan.letterWidths.find(letter);
    if (it != plan.letterWidths.end()) return it->second;
    const double w = measureLetter(renderer, plan.layoutPaint, letter) * plan.sizeScale;
    plan.letterWidths.emplace(letter, w);
    return w;
}

double textWidth(const CounterRenderPlan& plan, subtitle::SkiaRenderer* renderer, const std::string& text) {
    double w = 0.0;
    for (const std::string& letter : clusters(text)) w += letterWidth(plan, renderer, letter);
    return w;
}

// Measurer on the Skia text engine. Advances are measured at the layout's reference paint and
// scaled, exactly as the layout's own letterAdvances are, so a number's cells sit where the layout
// would have put its digits.
class SkiaCounterMeasurer : public C::Measurer {
public:
    SkiaCounterMeasurer(const CounterRenderPlan& plan, subtitle::SkiaRenderer* renderer, double ascent, double descent, double digit)
        : plan(plan), renderer(renderer), asc(ascent), desc(descent), dig(digit) {}
    double width(const std::string& text) const override { return textWidth(plan, renderer, text); }
    double ascent() const override { return asc; }
    double descent() const override { return desc; }
    double digit() const override { return dig; }
private:
    const CounterRenderPlan& plan;
    subtitle::SkiaRenderer* renderer;
    double asc, desc, dig;
};

// Layout lines → codepoint ranges of the text they came from. The layout keeps every line's text
// as a substring and drops only whitespace (hard breaks, the spaces a wrap ate), so each line is
// found at the cursor after skipping whitespace. nullopt if a line cannot be found.
std::optional<std::vector<C::LineRange>> mapLines(const std::string& text, const TextClipLayout& layout) {
    const std::u32string t = C::toCodepoints(text);
    std::vector<C::LineRange> out;
    size_t cursor = 0;
    auto isWs = [](char32_t c) { return c == U' ' || c == U'\n' || c == U'\t' || c == U'\r' || c == 0xA0 || c == 0x202F || c == 0x2009; };
    for (const TextClipLine& line : layout.lines) {
        const std::u32string l = C::toCodepoints(line.text);
        if (l.empty()) {
            // An empty paragraph sits on the next hard break.
            size_t c = cursor;
            while (c < t.size() && t[c] != U'\n' && isWs(t[c])) ++c;
            out.push_back({static_cast<int>(c), static_cast<int>(c)});
            if (c < t.size() && t[c] == U'\n') cursor = c + 1;
            continue;
        }
        size_t c = cursor;
        while (true) {
            if (t.compare(c, l.size(), l) == 0) break;
            if (c >= t.size() || !isWs(t[c])) return std::nullopt;
            ++c;
        }
        out.push_back({static_cast<int>(c), static_cast<int>(c + l.size())});
        cursor = c + l.size();
    }
    return out;
}

// ── Frame → items ────────────────────────────────────────────────────────────

struct Piece {
    const C::Glyph* glyph = nullptr;
    std::string letter;
    bool counter = false;       // a digit cell / number glyph (centred), else a text letter (left edge)
    double anchorX = 0.0;       // canvas x: letter left edge, or cell centre
    double baseline = 0.0;      // canvas y
    double left = 0.0, right = 0.0;
    double textWidth = 0.0;
    int row = 0;
    int element = -1;
};

std::vector<Piece> piecesOf(const CounterRenderPlan& plan, const C::LayerFrame& frame, double ox, double oy,
                            subtitle::SkiaRenderer* renderer, const std::function<bool(const C::Glyph&)>& skip) {
    std::vector<Piece> out;
    // Text letters map to the row's letter elements in order; numbers by their track.
    std::vector<size_t> letterCursor(plan.rows.size(), 0);
    auto nextLetterElement = [&](int row) {
        if (row < 0 || row >= static_cast<int>(plan.rows.size())) return -1;
        const auto& elems = plan.rows[row];
        size_t& k = letterCursor[row];
        while (k < elems.size() && elems[k].number) ++k;
        return k < elems.size() ? static_cast<int>(k++) : -1;
    };
    auto numberElement = [&](int row, int track) {
        if (row < 0 || row >= static_cast<int>(plan.rows.size())) return -1;
        const auto& elems = plan.rows[row];
        for (size_t k = 0; k < elems.size(); ++k) if (elems[k].number && elems[k].track == track) return static_cast<int>(k);
        return -1;
    };
    for (const C::Glyph& g : frame.glyphs) {
        const double baseline = oy + plan.firstBaseline + g.y;
        const double x0 = ox + plan.anchorX + g.x;
        if (g.run) {
            const std::vector<std::string> letters = clusters(g.text);
            double cursor = x0;
            for (const std::string& letter : letters) {
                const double w = letterWidth(plan, renderer, letter);
                Piece p;
                p.glyph = &g;
                p.letter = letter;
                p.anchorX = cursor;
                p.baseline = baseline;
                p.left = cursor;
                p.right = cursor + w;
                p.textWidth = w;
                p.row = g.line;
                p.element = nextLetterElement(g.line);
                // A run of one letter carries the letter spacing in its width; a longer run never does.
                cursor += letters.size() == 1 ? g.w : w;
                if (!skip || !skip(g)) out.push_back(p);
            }
            continue;
        }
        Piece p;
        p.glyph = &g;
        p.letter = g.text;
        p.counter = true;
        p.anchorX = x0;
        p.baseline = baseline;
        p.left = x0 - g.w / 2;
        p.right = x0 + g.w / 2;
        p.textWidth = textWidth(plan, renderer, g.text);
        p.row = g.line;
        p.element = numberElement(g.line, g.track);
        if (!skip || !skip(g)) out.push_back(p);
    }
    return out;
}

std::optional<SkMatrix> layerTransform(const CounterRenderPlan& plan, const C::LayerFrame& frame, double ox, double oy) {
    const C::LayerXf& xf = frame.xf;
    if (xf.scale == 1.0 && xf.dx == 0.0 && xf.dy == 0.0) return std::nullopt;
    const float px = static_cast<float>(ox + plan.anchorX + xf.px);
    const float py = static_cast<float>(oy + plan.firstBaseline + xf.py);
    SkMatrix m = SkMatrix::Translate(static_cast<float>(xf.dx) + px, static_cast<float>(xf.dy) + py);
    m.preScale(static_cast<float>(xf.scale), static_cast<float>(xf.scale));
    m.preTranslate(-px, -py);
    return m;
}

AnimatedUnitGlyph unitGlyph(const Piece& p, double centerX, double centerY) {
    AnimatedUnitGlyph ug;
    ug.letter = p.letter;
    ug.offsetX = p.anchorX - centerX;
    ug.offsetY = p.baseline - centerY;
    if (p.counter) {
        const C::Glyph& g = *p.glyph;
        CounterGlyphPlacement c;
        c.textOffsetX = -p.textWidth / 2.0;
        c.dy = g.dy;
        c.sx = g.sx;
        c.sy = g.sy;
        c.pivot = g.pivot;
        c.hasClip = g.hasClip;
        c.clip0 = g.clip0;
        c.clip1 = g.clip1;
        c.feather = g.feather;
        c.halfWidth = g.w;
        ug.counter = c;
    }
    return ug;
}

// Items for one evaluated frame. Without a unit animation every glyph is its own item with
// identity props. With one, glyphs are grouped into the animation's units (a whole number is one
// unit) and every glyph of a unit gets the unit's props and pivot box, so the unit moves rigidly.
std::vector<AnimatedUnitItem> buildItems(const CounterRenderPlan& plan, const C::LayerFrame& frame,
                                         double ox, double oy, const TextClipLayout& layout,
                                         const TextClipAnimationFrame* unitAnim, double opacityMul,
                                         subtitle::SkiaRenderer* renderer,
                                         const std::function<bool(const C::Glyph&)>& skip = {}) {
    std::vector<AnimatedUnitItem> items;
    const std::vector<Piece> pieces = piecesOf(plan, frame, ox, oy, renderer, skip);
    const std::optional<SkMatrix> pre = layerTransform(plan, frame, ox, oy);
    const double layerOpacity = frame.xf.opacity * opacityMul;

    auto emit = [&](const Piece& p, double cx, double cy, double bw, double bh, const ResolvedAnimProps& props, double unitOpacity) {
        const double op = p.glyph->opacity * unitOpacity * layerOpacity;
        if (op <= 0.002 || isSpaceGlyph(p.letter)) return;
        AnimatedUnitItem item;
        item.centerX = cx;
        item.centerY = cy;
        item.boxWidth = bw;
        item.boxHeight = bh;
        item.props = props;
        item.opacity = op;
        item.extraBlur = p.glyph->blur;
        item.preTransform = pre;
        item.glyphs.push_back(unitGlyph(p, cx, cy));
        items.push_back(std::move(item));
    };

    if (!unitAnim) {
        for (const Piece& p : pieces) {
            emit(p, p.anchorX, p.baseline, std::max(p.right - p.left, 1.0), plan.fontAscent + plan.fontDescent, identityProps(), 1.0);
        }
        return items;
    }

    // Unit key per granularity, from the element table.
    struct Unit { int index = -1, ordinal = -1; double left = 1e300, right = -1e300; int row = 0; std::vector<const Piece*> pieces; };
    std::map<long long, Unit> units;
    for (const Piece& p : pieces) {
        if (p.element < 0) continue;
        const CounterRenderPlan::Element& e = plan.rows[p.row][p.element];
        if (e.space) continue;
        long long key;
        int index, ordinal;
        switch (unitAnim->granularity) {
        case UnitGranularity::WORD: key = e.word; index = ordinal = e.word; break;
        case UnitGranularity::LINE: key = e.line; index = ordinal = e.line; break;
        default: key = (static_cast<long long>(p.row) << 32) | static_cast<unsigned>(p.element); index = e.charIndex; ordinal = e.charOrdinal; break;
        }
        Unit& u = units[key];
        u.index = index;
        u.ordinal = ordinal;
        u.row = p.row;
        u.left = std::min(u.left, p.left);
        u.right = std::max(u.right, p.right);
        u.pieces.push_back(&p);
    }
    for (auto& [key, u] : units) {
        (void)key;
        const ResolvedAnimProps props = unitAnim->getUnitProps(u.index, u.ordinal);
        const double opacity = clamp01(props.opacity());
        if (opacity <= 0.0) continue;
        const TextClipLine* line = u.row < static_cast<int>(layout.lines.size()) ? &layout.lines[u.row] : nullptr;
        const double asc = line ? line->ascent : plan.fontAscent;
        const double desc = line ? line->descent : plan.fontDescent;
        const double baseline = u.pieces.front()->baseline;
        const double cx = (u.left + u.right) / 2.0;
        const double cy = baseline - (asc - desc) / 2.0;
        for (const Piece* p : u.pieces) emit(*p, cx, cy, u.right - u.left, asc + desc, props, opacity);
    }
    return items;
}

// ── Motion blur ──────────────────────────────────────────────────────────────

std::string signature(const C::Glyph& g) {
    char buf[256];
    std::snprintf(buf, sizeof buf, "|%.2f|%.1f|%.2f|%.3f|%.3f|%.3f|%.1f|%d|%.1f,%.1f", g.x, g.y, g.dy, g.sx, g.sy, g.opacity, g.blur,
                  g.hasClip ? 1 : 0, g.clip0, g.clip1);
    return g.text + buf;
}

bool sameTransform(const C::LayerXf& a, const C::LayerXf& b) {
    return std::abs(a.dx - b.dx) < 0.01 && std::abs(a.dy - b.dy) < 0.01 && std::abs(a.scale - b.scale) < 1e-4 &&
           std::abs(a.opacity - b.opacity) < 1e-3 && std::abs(a.px - b.px) < 0.01 && std::abs(a.blur - b.blur) < 0.01;
}

struct Exposure {
    C::LayerFrame frame;                  // at the frame time
    std::vector<C::LayerFrame> samples;   // empty = no motion blur this frame
    std::set<std::string> still;          // glyphs identical through the exposure
};

Exposure expose(const CounterRenderPlan& plan, double t, double deviceScale) {
    Exposure ex;
    const double d = plan.duration;
    const double ft = std::min(std::max(t, 0.0), d);
    ex.frame = C::evaluateLayer(*plan.compiled, ft, ft);
    if (plan.fx.shutter <= 0.0 || plan.fps <= 0.0) return ex;
    const double shutter = plan.fx.shutter / 360.0 / plan.fps;
    auto at = [&](double s) {
        const double c = std::min(std::max(s, 0.0), d);
        return c == ft ? ex.frame : C::evaluateLayer(*plan.compiled, c, ft);
    };
    const C::LayerFrame open = at(ft - shutter / 2), close = at(ft + shutter / 2);
    const double travel = std::max({C::poseDistance(open, ex.frame), C::poseDistance(ex.frame, close), C::poseDistance(open, close)}) * deviceScale;
    const int n = C::motionBlurSamples(travel, MAX_SHUTTER_SAMPLES);
    if (n <= 1) return ex;
    if (sameTransform(open.xf, close.xf) && sameTransform(open.xf, ex.frame.xf)) {
        std::set<std::string> a, b;
        for (const auto& g : open.glyphs) a.insert(signature(g));
        for (const auto& g : close.glyphs) b.insert(signature(g));
        for (const auto& g : ex.frame.glyphs) {
            const std::string s = signature(g);
            if (a.count(s) && b.count(s)) ex.still.insert(s);
        }
    }
    for (double s : C::shutterSampleTimes(ft, plan.fx.shutter, plan.fps, n, d)) ex.samples.push_back(at(s));
    return ex;
}

// ── Drawing ──────────────────────────────────────────────────────────────────

struct CounterDraw {
    const CounterRenderPlan& plan;
    const TextClipLayout& layout;
    const TextClipPaintStyle& paint;
    const std::optional<TextClipBackgroundStyle>& background;
    subtitle::SkiaRenderer* renderer;
    double t;

    // Margin around the content box that the glyphs, their effects and their travel can reach.
    double effectsMargin() const {
        double m = paint.fontSize * 0.5 + 4.0;
        if (paint.dropShadow) m += paint.dropShadow->distance + 3.0 * paint.dropShadow->blur;
        if (paint.stroke) m += paint.stroke->width;
        m += 3.0 * (paint.blur + paint.fontSize * 0.16);
        return m;
    }

    double blockMargin() const {
        const double shadowMargin = paint.dropShadow ? paint.dropShadow->distance + paint.dropShadow->blur * 2.0 : 0.0;
        const double strokeMargin = paint.stroke ? paint.stroke->width * 2.0 : 0.0;
        double glowMargin = 0.0;
        if (paint.glow) {
            const double offMax = std::max(std::abs(paint.glow->sourceOffX), std::abs(paint.glow->sourceOffY)) * paint.fontSize;
            const double halfExtent = std::max(layout.layoutWidth, layout.textHeight) / 2.0;
            glowMargin = paint.glow->rayLen * (halfExtent + offMax) + offMax + GLOW_BEAM_BLUR_RATIO * paint.fontSize * 3.0;
        }
        const double over = std::max({plan.overLeft, plan.overRight, plan.overTop, plan.overBottom});
        return std::ceil(std::max({shadowMargin, strokeMargin, glowMargin}) + paint.blur * 3.0 + paint.fontSize * 0.16 * 3.0 + over + 4.0);
    }

    void drawBackground(const C::LayerFrame& frame, double ox, double oy) const {
        if (!background) return;
        // The box of the final text, grown to whatever the counting text needs: it never shrinks
        // under the resting layout, so a count-up's pill does not wobble with every digit.
        double left = 0.0, right = layout.layoutWidth;
        const C::Align align = C::layerInput(*plan.compiled).style.align;
        for (double w : frame.lineWidths) {
            const double shift = align == C::Align::Left ? 0.0 : align == C::Align::Center ? -w / 2.0 : -w;
            left = std::min(left, plan.anchorX + shift);
            right = std::max(right, plan.anchorX + shift + w);
        }
        drawBackgroundRect(renderer, *background, ox + left, oy, right - left, layout.textHeight);
    }

    void pass(std::vector<AnimatedUnitItem>& items, double ox, double oy, BlockDrawLayer layer, const AnimationTransformFlags& flags) const {
        if (items.empty()) return;
        TextGlowRenderer glow(renderer);
        UnitAnimationRenderer(renderer, &glow).drawAnimatedUnitItems(
            items, paint, flags, layout.layoutWidth, layout.textHeight, ox, oy, /*skipGlow*/ true, layer);
    }

    // One pass (shadow, or stroke + fill) of the whole exposure: the moving glyphs averaged over the
    // shutter samples in an F16 buffer aligned to device pixels, then the still glyphs on top.
    void exposedPass(const Exposure& ex, double ox, double oy, BlockDrawLayer layer,
                     const TextClipAnimationFrame* unitAnim, double opacityMul) const {
        const AnimationTransformFlags flags = unitAnim ? unitAnim->flags : AnimationTransformFlags{};
        SkCanvas* canvas = renderer->getCanvas();
        auto drawFrameOnly = [&] {
            std::vector<AnimatedUnitItem> items = buildItems(plan, ex.frame, ox, oy, layout, unitAnim, opacityMul, renderer);
            pass(items, ox, oy, layer, flags);
        };
        if (ex.samples.empty() || !canvas) { drawFrameOnly(); return; }

        const SkMatrix ctm = canvas->getLocalToDeviceAs3x3();
        if (ctm.hasPerspective()) { drawFrameOnly(); return; }
        SkIRect region = canvas->getDeviceClipBounds();
        if (!unitAnim) {
            // Without a unit animation the glyphs stay near the content box: bound the buffer to it.
            const double m = effectsMargin();
            const SkRect local = SkRect::MakeLTRB(
                static_cast<float>(ox - plan.overLeft - m), static_cast<float>(oy - plan.overTop - m),
                static_cast<float>(ox + layout.layoutWidth + plan.overRight + m),
                static_cast<float>(oy + layout.textHeight + plan.overBottom + m));
            const SkIRect dev = ctm.mapRect(local).roundOut();
            if (!region.intersect(dev)) return;
        }
        if (region.isEmpty()) return;
        if (static_cast<long long>(region.width()) * region.height() > MAX_ACCUMULATION_PIXELS) { drawFrameOnly(); return; }

        GpuOffscreen accum = GpuOffscreen::Match(canvas, region.width(), region.height(), kRGBA_F16_SkColorType);
        GpuOffscreen scratch = GpuOffscreen::Match(canvas, region.width(), region.height());
        if (!accum || !scratch) { drawFrameOnly(); return; }
        SkCanvas* acc = accum.canvas();
        SkCanvas* scr = scratch.canvas();
        acc->clear(SK_ColorTRANSPARENT);
        const std::set<std::string>& still = ex.still;
        const auto moving = [&still](const C::Glyph& g) { return still.count(signature(g)) > 0; };
        const float share = 1.0f / static_cast<float>(ex.samples.size());
        SkPaint add;
        add.setBlendMode(SkBlendMode::kPlus);
        add.setAlphaf(share);
        for (const C::LayerFrame& f : ex.samples) {
            scr->clear(SK_ColorTRANSPARENT);
            scr->save();
            scr->translate(static_cast<float>(-region.left()), static_cast<float>(-region.top()));
            scr->concat(ctm);
            renderer->renderToCanvas(scr, [&] {
                std::vector<AnimatedUnitItem> items = buildItems(plan, f, ox, oy, layout, unitAnim, opacityMul, renderer, moving);
                pass(items, ox, oy, layer, flags);
            });
            scr->restore();
            if (sk_sp<SkImage> img = scratch.snapshot()) acc->drawImage(img.get(), 0.0f, 0.0f, SkSamplingOptions(), &add);
        }
        if (sk_sp<SkImage> img = accum.snapshot()) {
            canvas->save();
            canvas->resetMatrix();
            canvas->drawImage(img.get(), static_cast<float>(region.left()), static_cast<float>(region.top()));
            canvas->restore();
        }
        if (!still.empty()) {
            std::vector<AnimatedUnitItem> items = buildItems(plan, ex.frame, ox, oy, layout, unitAnim, opacityMul, renderer,
                                                             [&still](const C::Glyph& g) { return still.count(signature(g)) == 0; });
            pass(items, ox, oy, layer, flags);
        }
    }

    Exposure exposure() const {
        SkCanvas* canvas = renderer->getCanvas();
        double scale = 1.0;
        if (canvas) {
            const SkMatrix m = canvas->getLocalToDeviceAs3x3();
            scale = std::sqrt(std::abs(m.getScaleX() * m.getScaleY() - m.getSkewX() * m.getSkewY()));
            if (!(scale > 0.0)) scale = 1.0;
        }
        return expose(plan, t, scale);
    }

    void glowLayer(const Exposure& ex, double ox, double oy, const TextClipAnimationFrame* unitAnim, double opacityMul) const {
        if (!paint.glow) return;
        std::vector<AnimatedUnitItem> items = buildItems(plan, ex.frame, ox, oy, layout, unitAnim, opacityMul, renderer);
        if (items.empty()) return;
        TextGlowRenderer glow(renderer);
        glow.drawAnimatedGlowLayer(items, paint, *paint.glow, layout.layoutWidth, layout.textHeight, ox, oy,
                                   unitAnim ? unitAnim->flags : AnimationTransformFlags{});
    }

    // The whole counter (or one z-band of it) with the content box's top-left at (ox, oy).
    void content(double ox, double oy, BlockDrawLayer layer, const TextClipAnimationFrame* unitAnim) const {
        const Exposure ex = exposure();
        if (layer != BlockDrawLayer::AboveGlow) drawBackground(ex.frame, ox, oy);
        if (layer != BlockDrawLayer::AboveGlow && paint.dropShadow) exposedPass(ex, ox, oy, BlockDrawLayer::BelowGlow, unitAnim, 1.0);
        if (layer == BlockDrawLayer::All) glowLayer(ex, ox, oy, unitAnim, 1.0);
        if (layer != BlockDrawLayer::BelowGlow) exposedPass(ex, ox, oy, BlockDrawLayer::AboveGlow, unitAnim, 1.0);
    }

    BlockAnimationContent blockContent(const TextClipAnimationFrame* unitAnim) const {
        BlockAnimationContent c;
        c.contentWidth = layout.layoutWidth;
        c.contentHeight = layout.textHeight;
        c.hasGlow = paint.glow.has_value();
        const double margin = blockMargin();
        c.margin = [margin](double blurSigma) { return margin + std::ceil(blurSigma * 3.0); };
        c.draw = [this, unitAnim](double ox, double oy, BlockDrawLayer layer, bool) { content(ox, oy, layer, unitAnim); };
        c.drawGlow = [this, unitAnim](double ox, double oy, double opacityMul) {
            glowLayer(exposure(), ox, oy, unitAnim, opacityMul);
        };
        return c;
    }
};

// Elements of each row and their unit indices, from the frame at the end of the clip. Text letters
// are the same at every moment; a number is one element whatever digits it shows.
void buildElements(CounterRenderPlan& plan, const C::LayerFrame& frame) {
    plan.rows.assign(C::lineCount(*plan.compiled), {});
    for (const C::Glyph& g : frame.glyphs) {
        if (g.line < 0 || g.line >= static_cast<int>(plan.rows.size())) continue;
        auto& row = plan.rows[g.line];
        if (g.run) {
            for (const std::string& letter : clusters(g.text)) {
                CounterRenderPlan::Element e;
                e.space = isSpaceGlyph(letter);
                row.push_back(e);
            }
            continue;
        }
        bool seen = false;
        for (const auto& e : row) seen = seen || (e.number && e.track == g.track);
        if (!seen) {
            CounterRenderPlan::Element e;
            e.number = true;
            e.track = g.track;
            row.push_back(e);
        }
    }
    int slot = 0, drawn = 0, word = 0, line = 0;
    for (auto& row : plan.rows) {
        bool inWord = false, ink = false;
        int current = -1;
        for (auto& e : row) {
            e.charIndex = slot++;
            if (e.space) { inWord = false; continue; }
            e.charOrdinal = drawn++;
            if (!inWord) { current = word++; inWord = true; }
            e.word = current;
            ink = true;
        }
        if (ink) {
            for (auto& e : row) if (!e.space) e.line = line;
            ++line;
        }
    }
    plan.unitCounts.chars = slot;
    plan.unitCounts.charsDrawn = drawn;
    plan.unitCounts.words = word;
    plan.unitCounts.lines = line;
}

// How far the glyphs of a frame reach, content-box coordinates (top-left = 0, 0).
void extendExtents(const CounterRenderPlan& plan, const C::LayerFrame& frame, subtitle::SkiaRenderer* renderer,
                   double& minX, double& maxX, double& minY, double& maxY) {
    double x0 = 1e300, x1 = -1e300, y0 = 1e300, y1 = -1e300;
    for (const Piece& p : piecesOf(plan, frame, 0.0, 0.0, renderer, {})) {
        const C::Glyph& g = *p.glyph;
        if (g.opacity <= 0.002) continue;
        double l = p.left, r = p.right, top = p.baseline - plan.fontAscent, bottom = p.baseline + plan.fontDescent;
        if (p.counter) {
            const double half = p.textWidth / 2.0 * std::abs(g.sx);
            l = p.anchorX - half;
            r = p.anchorX + half;
            auto map = [&](double yl) { return g.dy + g.pivot + (yl - g.pivot) * g.sy; };
            double a = map(-plan.fontAscent), b = map(plan.fontDescent);
            if (a > b) std::swap(a, b);
            if (g.hasClip) {
                a = std::max(a, g.clip0 - g.feather);
                b = std::min(b, g.clip1 + g.feather);
                l = std::max(l, p.anchorX - g.w);
                r = std::min(r, p.anchorX + g.w);
                if (b <= a || r <= l) continue;
            }
            top = p.baseline + a;
            bottom = p.baseline + b;
        }
        x0 = std::min(x0, l); x1 = std::max(x1, r);
        y0 = std::min(y0, top); y1 = std::max(y1, bottom);
    }
    if (x0 > x1) return;
    if (frame.xf.scale != 1.0) {
        const double px = plan.anchorX + frame.xf.px, py = plan.firstBaseline + frame.xf.py, s = frame.xf.scale;
        x0 = px + (x0 - px) * s; x1 = px + (x1 - px) * s;
        y0 = py + (y0 - py) * s; y1 = py + (y1 - py) * s;
    }
    minX = std::min(minX, x0); maxX = std::max(maxX, x1);
    minY = std::min(minY, y0); maxY = std::max(maxY, y1);
}

} // namespace

std::optional<CounterRenderPlan> buildCounterPlan(
    const std::string& transformedText, const C::CounterFx& fxIn, const TextClipLayout& layout,
    const TextClipPaintStyle& paint, const TextClipPaintStyle& layoutPaint, double durationSec, double fps,
    subtitle::SkiaRenderer* renderer, std::vector<std::string>& warnings) {
    if (paint.curveAngle.has_value()) {
        warnings.push_back("counter on curved text is not supported; rendered as static text");
        return std::nullopt;
    }
    if (!(durationSec > 0.0)) {
        warnings.push_back("counter needs the clip duration; rendered as static text");
        return std::nullopt;
    }
    const std::vector<C::NumberToken> tokens = C::parseText(transformedText);
    if (tokens.empty()) {
        warnings.push_back("counter: the text has no number; rendered as static text");
        return std::nullopt;
    }
    const std::optional<std::vector<C::LineRange>> ranges = mapLines(transformedText, layout);
    if (!ranges) {
        warnings.push_back("counter: the layout's lines could not be matched to the text; rendered as static text");
        return std::nullopt;
    }
    // Targets whose number (or sign) is broken across two lines are left static.
    C::CounterFx fx = fxIn;
    std::vector<int> active;
    for (int i : fx.targets) if (i >= 0 && i < static_cast<int>(tokens.size())) active.push_back(i);
    if (active.empty()) active.push_back(0);
    std::vector<int> kept;
    auto lineOf = [&](int pos) {
        for (size_t k = 0; k < ranges->size(); ++k) if (pos >= (*ranges)[k].start && pos < std::max((*ranges)[k].end, (*ranges)[k].start + 1)) return static_cast<int>(k);
        return -1;
    };
    for (int i : active) {
        const C::NumberToken& tok = tokens[i];
        const int a = lineOf(tok.start), b = lineOf(tok.end - 1);
        const bool signOk = tok.signAt < 0 || lineOf(tok.signAt) == a;
        if (a >= 0 && a == b && signOk) kept.push_back(i);
        else warnings.push_back("counter: number " + std::to_string(i) + " is wrapped across lines; left static");
    }
    if (kept.empty()) return std::nullopt;
    fx.targets = kept;

    CounterRenderPlan plan;
    plan.fx = fx;
    plan.duration = durationSec;
    plan.fps = fps > 0.0 ? fps : 30.0;
    plan.layoutPaint = layoutPaint;
    plan.sizeScale = layoutPaint.fontSize > 0.0 ? paint.fontSize / layoutPaint.fontSize : 1.0;
    plan.firstBaseline = layout.firstLineAscent;
    plan.lineStep = layout.lineHeight;
    for (const auto& line : layout.lines) {
        plan.lineAscent.push_back(line.ascent);
        plan.lineDescent.push_back(line.descent);
    }
    plan.anchorX = paint.alignment == TextAlignment::LEFT ? 0.0
                 : paint.alignment == TextAlignment::RIGHT ? layout.layoutWidth : layout.layoutWidth / 2.0;

    const SkFont font = getFontForChar(renderer, paint, U'0');
    SkFontMetrics fm;
    font.getMetrics(&fm);
    plan.fontAscent = -fm.fAscent;
    plan.fontDescent = fm.fDescent;
    const double digit = -measureTextVerticalBounds("0123456789", paint, renderer).top;

    C::LayerInput in;
    in.text = transformedText;
    in.style.fontSize = paint.fontSize;
    in.style.letterSpacing = paint.fontSize > 0.0 ? paint.letterSpacing / paint.fontSize : 0.0;
    in.style.lineHeight = paint.fontSize > 0.0 ? layout.lineHeight / paint.fontSize : 1.0;
    in.style.align = paint.alignment == TextAlignment::LEFT ? C::Align::Left
                   : paint.alignment == TextAlignment::RIGHT ? C::Align::Right : C::Align::Center;
    in.duration = durationSec;
    in.counter = fx;
    in.lines = *ranges;
    in.letters = clusters;
    SkiaCounterMeasurer measurer(plan, renderer, plan.fontAscent, plan.fontDescent, digit);
    plan.compiled = C::compileLayer(in, measurer);
    if (C::trackCount(*plan.compiled) == 0) {
        warnings.push_back("counter: no number in the text can be animated (too many digits?); rendered as static text");
        return std::nullopt;
    }
    plan.settle = C::settleTime(*plan.compiled);

    buildElements(plan, C::evaluateLayer(*plan.compiled, durationSec));

    // Worst-case reach over the clip: every frame, plus the shutter's half-exposure either side.
    double minX = 0.0, maxX = layout.layoutWidth, minY = 0.0, maxY = layout.textHeight;
    const int frames = static_cast<int>(std::ceil(durationSec * plan.fps));
    const int step = std::max(1, frames / 600);
    const double halfShutter = std::max(0.0, fx.shutter) / 720.0 / plan.fps;
    for (int k = 0; k <= frames; k += step) {
        const double t = std::min(durationSec, k / plan.fps);
        for (double s : {t - halfShutter, t, t + halfShutter}) {
            const double c = std::min(std::max(s, 0.0), durationSec);
            extendExtents(plan, C::evaluateLayer(*plan.compiled, c, t), renderer, minX, maxX, minY, maxY);
        }
    }
    const double slack = paint.fontSize * 0.05;
    plan.overLeft = std::max(0.0, -minX) + slack;
    plan.overRight = std::max(0.0, maxX - layout.layoutWidth) + slack;
    plan.overTop = std::max(0.0, -minY) + slack;
    plan.overBottom = std::max(0.0, maxY - layout.textHeight) + slack;
    return plan;
}

void renderCounterFrame(const CounterRenderPlan& plan, double t, const TextClipLayout& layout,
                        const TextClipPaintStyle& paint, const std::optional<TextClipBackgroundStyle>& background,
                        double originX, double originY, double scale,
                        const std::optional<TextClipAnimationFrame>& animation, subtitle::SkiaRenderer* renderer) {
    if (!renderer->getCanvas()) return;
    const CounterDraw draw{plan, layout, paint, background, renderer, t};
    const bool unit = animation && animation->mode == AnimationMode::UNIT;
    const bool block = animation && animation->mode == AnimationMode::BLOCK;
    if (unit && animation->static3D) {
        // Unit animation under a static tilt: stagger flat into the block texture, tilt it as one piece.
        const BlockAnimationContent content = draw.blockContent(&*animation);
        const TextClipAnimationFrame tilt = buildStatic3DFrame(animation->static3D->first, animation->static3D->second);
        BlockAnimationRenderer(renderer).renderBlockAnimated(content, paint, background, originX, originY, scale, tilt);
    } else if (unit) {
        draw.content(originX, originY, BlockDrawLayer::All, &*animation);
    } else if (block) {
        const BlockAnimationContent content = draw.blockContent(nullptr);
        BlockAnimationRenderer(renderer).renderBlockAnimated(content, paint, background, originX, originY, scale, *animation);
    } else {
        draw.content(originX, originY, BlockDrawLayer::All, nullptr);
    }
}

std::vector<std::string> counterFinalLines(const CounterRenderPlan& plan) {
    const C::LayerFrame f = C::evaluateLayer(*plan.compiled, plan.duration);
    std::vector<std::vector<const C::Glyph*>> rows(C::lineCount(*plan.compiled));
    for (const auto& g : f.glyphs) if (g.opacity > 0.5 && !g.hasClip && g.line < static_cast<int>(rows.size())) rows[g.line].push_back(&g);
    std::vector<std::string> out;
    for (auto& r : rows) {
        std::stable_sort(r.begin(), r.end(), [](const C::Glyph* a, const C::Glyph* b) { return a->x < b->x; });
        std::string s;
        for (const auto* g : r) s += g->text;
        out.push_back(s);
    }
    return out;
}

} // namespace text
} // namespace openshot
