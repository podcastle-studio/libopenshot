#include "TextClipReader.h"

#include "../Exceptions.h"
#include "../Frame.h"
#include "../Json.h"
#include "../subtitle/SkiaRenderer.h"
#include "TextAnimationRenderer.h"
#include "TextClipRenderer.h"
#include "TextDrawShared.h"
#include "TextGlowShader.h"

#include <skia/include/core/SkBitmap.h>
#include <skia/include/core/SkCanvas.h>
#include <skia/include/core/SkImageInfo.h>

#include <QColor>
#include <QImage>

#include <algorithm>
#include <cmath>
#include <regex>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace openshot {

namespace {

// Blur reach: cover ~3 σ of the gaussian (the rest is visually negligible).
constexpr double SHADOW_BLUR_SIGMA_MULTIPLIER = 3.0;

// Curved text is a single unbroken line bent along the path — collapse hard breaks (and the
// whitespace around them) to single spaces so it lays out as one line. Mirrors the frontend
// `value.replace(/\s*\n+\s*/g, ' ')`.
std::string collapseNewlines(const std::string& value) {
    static const std::regex re(R"(\s*\n+\s*)");
    return std::regex_replace(value, re, " ");
}

// Outward margin (px) the text effects (shadow, stroke, gaussian blur, glow beams) extend
// beyond the bounding box, so the frame buffer reserves room for them and nothing is clipped.
double effectsMargin(const openshot::text::TextClipPaintStyle& paint,
                     double contentWidth, double contentHeight) {
    const double strokeMargin = paint.stroke.has_value() ? paint.stroke->width : 0.0;
    double shadowMargin = 0.0;
    if (paint.dropShadow.has_value()) {
        shadowMargin = paint.dropShadow->distance + SHADOW_BLUR_SIGMA_MULTIPLIER * paint.dropShadow->blur;
    }
    double glowMargin = 0.0;
    if (paint.glow.has_value()) {
        const double offX = paint.glow->sourceOffX * paint.fontSize;
        const double offY = paint.glow->sourceOffY * paint.fontSize;
        const double offMax = std::max(std::abs(offX), std::abs(offY));
        const double halfExtent = std::max(contentWidth, contentHeight) / 2.0;
        glowMargin = paint.glow->rayLen * (halfExtent + offMax) + offMax
                     + openshot::text::GLOW_BEAM_BLUR_RATIO * paint.fontSize * 3.0;
    }
    const double blurMargin = SHADOW_BLUR_SIGMA_MULTIPLIER * paint.blur;
    return std::max({strokeMargin, shadowMargin, glowMargin}) + blurMargin;
}

text::TextAlignment parseAlignment(const std::string& s) {
    if (s == "LEFT") return text::TextAlignment::LEFT;
    if (s == "RIGHT") return text::TextAlignment::RIGHT;
    return text::TextAlignment::CENTER;
}

const char* alignmentToString(text::TextAlignment a) {
    switch (a) {
    case text::TextAlignment::LEFT:  return "LEFT";
    case text::TextAlignment::RIGHT: return "RIGHT";
    default:                          return "CENTER";
    }
}

text::TextTransform parseTransform(const std::string& s) {
    if (s == "UPPERCASE")  return text::TextTransform::UPPERCASE;
    if (s == "LOWERCASE")  return text::TextTransform::LOWERCASE;
    if (s == "CAPITALIZE") return text::TextTransform::CAPITALIZE;
    return text::TextTransform::NONE;
}

const char* transformToString(text::TextTransform t) {
    switch (t) {
    case text::TextTransform::UPPERCASE:  return "UPPERCASE";
    case text::TextTransform::LOWERCASE:  return "LOWERCASE";
    case text::TextTransform::CAPITALIZE: return "CAPITALIZE";
    default:                                return "NONE";
    }
}

void styleFromJson(const Json::Value& j, text::TextClipStyle& style) {
    if (!j["fontFamily"].isNull())            style.fontFamily = j["fontFamily"].asString();
    if (!j["italic"].isNull())                style.italic = j["italic"].asBool();
    if (!j["textAlign"].isNull())             style.textAlign = parseAlignment(j["textAlign"].asString());
    if (!j["textTransform"].isNull())         style.textTransform = parseTransform(j["textTransform"].asString());
    if (!j["color"].isNull())                 style.color = j["color"].asString();
    if (!j["lineHeight"].isNull())            style.lineHeight = j["lineHeight"].asDouble();
    if (!j["letterSpacing"].isNull())         style.letterSpacing = j["letterSpacing"].asDouble();
    if (!j["fontWeight"].isNull())            style.fontWeight = j["fontWeight"].asInt();
    if (!j["strokeColor"].isNull())           style.strokeColor = j["strokeColor"].asString();
    if (!j["strokeWidthRatio"].isNull())      style.strokeWidthRatio = j["strokeWidthRatio"].asDouble();
    if (!j["shadowColor"].isNull())           style.shadowColor = j["shadowColor"].asString();
    if (!j["shadowBlurRatio"].isNull())       style.shadowBlurRatio = j["shadowBlurRatio"].asDouble();
    if (!j["shadowDistanceRatio"].isNull())   style.shadowDistanceRatio = j["shadowDistanceRatio"].asDouble();
    if (!j["shadowAngle"].isNull())           style.shadowAngle = j["shadowAngle"].asDouble();
    if (!j["backgroundColor"].isNull())       style.backgroundColor = j["backgroundColor"].asString();
    if (!j["backgroundRadiusRatio"].isNull()) style.backgroundRadiusRatio = j["backgroundRadiusRatio"].asDouble();
    if (!j["backgroundPaddingXRatio"].isNull())style.backgroundPaddingXRatio = j["backgroundPaddingXRatio"].asDouble();
    if (!j["backgroundPaddingYRatio"].isNull())style.backgroundPaddingYRatio = j["backgroundPaddingYRatio"].asDouble();
    if (!j["blurRatio"].isNull())             style.blurRatio = j["blurRatio"].asDouble();
    if (!j["glowColor"].isNull())             style.glowColor = j["glowColor"].asString();
    if (!j["glowIntensityRatio"].isNull())    style.glowIntensityRatio = j["glowIntensityRatio"].asDouble();
    if (!j["glowRangeRatio"].isNull())        style.glowRangeRatio = j["glowRangeRatio"].asDouble();
    if (!j["glowDirectionX"].isNull())        style.glowDirectionX = j["glowDirectionX"].asDouble();
    if (!j["glowDirectionY"].isNull())        style.glowDirectionY = j["glowDirectionY"].asDouble();
    if (!j["curveAngle"].isNull())            style.curveAngle = j["curveAngle"].asDouble();
}

Json::Value styleToJson(const text::TextClipStyle& style) {
    Json::Value j(Json::objectValue);
    j["fontFamily"] = style.fontFamily;
    j["italic"] = style.italic;
    j["textAlign"] = alignmentToString(style.textAlign);
    j["textTransform"] = transformToString(style.textTransform);
    j["color"] = style.color;
    j["lineHeight"] = style.lineHeight;
    j["letterSpacing"] = style.letterSpacing;
    j["fontWeight"] = style.fontWeight;
    j["strokeWidthRatio"] = style.strokeWidthRatio;
    j["shadowBlurRatio"] = style.shadowBlurRatio;
    j["shadowDistanceRatio"] = style.shadowDistanceRatio;
    j["shadowAngle"] = style.shadowAngle;
    j["backgroundRadiusRatio"] = style.backgroundRadiusRatio;
    j["backgroundPaddingXRatio"] = style.backgroundPaddingXRatio;
    j["backgroundPaddingYRatio"] = style.backgroundPaddingYRatio;
    j["glowIntensityRatio"] = style.glowIntensityRatio;
    j["glowRangeRatio"] = style.glowRangeRatio;
    j["glowDirectionX"] = style.glowDirectionX;
    j["glowDirectionY"] = style.glowDirectionY;
    if (style.strokeColor.has_value())     j["strokeColor"]     = *style.strokeColor;
    if (style.shadowColor.has_value())     j["shadowColor"]     = *style.shadowColor;
    if (style.backgroundColor.has_value()) j["backgroundColor"] = *style.backgroundColor;
    if (style.blurRatio.has_value())       j["blurRatio"]       = *style.blurRatio;
    if (style.glowColor.has_value())       j["glowColor"]       = *style.glowColor;
    if (style.curveAngle.has_value())      j["curveAngle"]      = *style.curveAngle;
    return j;
}

void transformationFromJson(const Json::Value& j, text::TextTransformation& t) {
    if (!j["size"].isNull())     t.size = j["size"].asDouble();
    if (!j["rotation"].isNull()) t.rotation = j["rotation"].asDouble();
    if (!j["maxWidth"].isNull()) t.maxWidth = j["maxWidth"].asDouble();
    if (!j["tiltX"].isNull())    t.tiltX = j["tiltX"].asDouble();
    if (!j["tiltY"].isNull())    t.tiltY = j["tiltY"].asDouble();
    if (j["position"].isObject()) {
        if (!j["position"]["x"].isNull()) t.positionX = j["position"]["x"].asDouble();
        if (!j["position"]["y"].isNull()) t.positionY = j["position"]["y"].asDouble();
    }
}

Json::Value transformationToJson(const text::TextTransformation& t) {
    Json::Value j(Json::objectValue);
    j["size"] = t.size;
    j["rotation"] = t.rotation;
    j["maxWidth"] = t.maxWidth;
    j["tiltX"] = t.tiltX;
    j["tiltY"] = t.tiltY;
    Json::Value pos(Json::objectValue);
    pos["x"] = t.positionX;
    pos["y"] = t.positionY;
    j["position"] = pos;
    return j;
}

} // namespace

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

TextClipReader::TextClipReader()
    : project_width(1920), project_height(1080), frame_width(0), frame_height(0), is_open(false), dirty(true)
{
    Open();
    Close();
}

TextClipReader::TextClipReader(int project_width_, int project_height_, const text::TextClipData& data_)
    : project_width(project_width_), project_height(project_height_), frame_width(0), frame_height(0),
      data(data_), is_open(false), dirty(true)
{
    Open();
    Close();
}

TextClipReader::~TextClipReader() = default;

// ---------------------------------------------------------------------------
// Open / Close
// ---------------------------------------------------------------------------

void TextClipReader::Open() {
    if (is_open) return;
    buildPlan();       // computes frame_width / frame_height + cached render plan
    initInfo();
    dirty = false;
    is_open = true;
}

void TextClipReader::Close() {
    if (!is_open) return;
    is_open = false;
    rendered_image.reset();
    info.vcodec = "";
    info.acodec = "";
}

void TextClipReader::initInfo() {
    // Animated text is a frame SEQUENCE (each frame differs); static text is a single image.
    const int fpsNum = has_animation ? std::max(1, static_cast<int>(std::lround(anim_fps))) : 30;

    info.has_audio = false;
    info.has_video = true;
    info.has_alpha = true;
    info.has_single_image = !has_animation;
    info.file_size = 0;
    info.vcodec = "QImage";
    info.width = std::max(1, frame_width);
    info.height = std::max(1, frame_height);
    info.pixel_ratio.num = 1;
    info.pixel_ratio.den = 1;
    info.duration = 60 * 60 * 1; // 1 hour
    info.fps.num = fpsNum;
    info.fps.den = 1;
    info.video_timebase.num = 1;
    info.video_timebase.den = fpsNum;
    info.video_length = static_cast<int64_t>(std::round(info.duration * info.fps.ToDouble()));

    Fraction dar(info.width * info.pixel_ratio.num, info.height * info.pixel_ratio.den);
    dar.Reduce();
    info.display_ratio.num = dar.num;
    info.display_ratio.den = dar.den;
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

void TextClipReader::buildPlan() {
    if (project_width <= 0 || data.value.empty()) {
        // Minimal 1×1 transparent placeholder — keeps Clip/Frame happy.
        plan_empty = true;
        frame_width = 1;
        frame_height = 1;
        bounding_width = 0.0;
        bounding_height = 0.0;
        frame_center_project_x = data.transformation.positionX;
        frame_center_project_y = data.transformation.positionY;
        has_animation = false;
        has_tilt = false;
        timeline.reset();
        return;
    }
    plan_empty = false;
    has_tilt = data.transformation.tiltX != 0.0 || data.transformation.tiltY != 0.0;

    // 1. Compute paint + background + layout (using only a measuring SkiaRenderer).
    const text::TextClipPaintStyle paint =
        text::convertTextStyleToPaintStyle(data.style, data.transformation, project_width);

    // `layoutPaint` drives line breaks at LAYOUT_REFERENCE_SIZE so wrap decisions are stable across
    // `transformation.size`, and at the aspect-ratio-correct reference width (Full HD long side) so
    // they are also stable across export resolution. Using the real project_width here would let the
    // measuring font size (and thus Skia's non-linear metrics) reflow the text at 4K. The sizeScale
    // in layoutTextAtReferenceSize divides it back out, keeping geometry correct.
    // See BACKEND_PATCH_LINE_CALCULATION.md.
    text::TextTransformation refTransformation = data.transformation;
    refTransformation.size = text::LAYOUT_REFERENCE_SIZE;
    const text::TextClipPaintStyle layoutPaint = text::convertTextStyleToPaintStyle(
        data.style, refTransformation, text::layoutReferenceProjectWidth(project_width, project_height));

    const auto background = text::convertBackgroundStyle(data.style, paint);

    // Curving is active if the static style curves OR a curveAngle keyframe does — either way the
    // layout must be newline-collapsed to a single line, so the cached plan_layout is valid for the
    // per-frame curved render (resolvePlanAtFrame reuses it, only recomputing the arc geometry).
    const bool curveKeyframed = has_style_keyframes && style_keyframes.curveAngle.has_value();
    const bool isCurved = paint.curveAngle.has_value() || curveKeyframed;

    // When the base is flat but curveAngle is keyframed, borrow frame 1's angle for the base
    // geometry so curvedGeometryForLayout has an angle to work with.
    text::TextClipPaintStyle curvePaint = paint;
    if (curveKeyframed && !curvePaint.curveAngle.has_value())
        curvePaint.curveAngle = style_keyframes.curveAngle->GetValue(1);

    std::string transformed = text::transformTextValue(data.value, data.style.textTransform);
    if (isCurved) transformed = collapseNewlines(transformed);

    // maxWidth is a dimensionless multiplier of the canvas-and-size scale; convert to pixels.
    // See BACKEND_PATCH_MAX_WIDTH_SIZE_RELATIVE.md. Curved text never wraps (single line).
    const double maxWidthPx = data.transformation.maxWidth > 0.0
        ? project_width * text::SIZE_BASE_COEFFICIENT
              * data.transformation.size * data.transformation.maxWidth
        : 0.0;
    const double wrapWidth    = isCurved ? 1e18 : (maxWidthPx > 0.0 ? maxWidthPx : 1e9);
    const double userMaxWidth = isCurved ? 0.0 : maxWidthPx;

    // Layout needs a canvas-less renderer for font measurement only. Skia requires a canvas,
    // so spin up a tiny dummy bitmap just to satisfy the API. The canvas isn't drawn to.
    SkBitmap measureBitmap;
    measureBitmap.allocPixels(SkImageInfo::MakeN32Premul(1, 1));
    SkCanvas measureCanvas(measureBitmap);
    subtitle::SkiaRenderer measureRenderer(&measureCanvas);

    const text::TextClipLayout layout = text::layoutTextAtReferenceSize(
        transformed, paint, layoutPaint, wrapWidth, userMaxWidth, &measureRenderer);

    // 2. Content box: the flat text block, or the curved arc's bounding box when curving.
    double contentWidth = layout.layoutWidth;
    double contentHeight = layout.textHeight;
    if (isCurved) {
        const text::CurvedTextGeometry geometry = text::curvedGeometryForLayout(layout, curvePaint);
        contentWidth = geometry.width;
        contentHeight = geometry.height;
    }

    const double bgPaddingX = background.has_value() ? background->paddingX : 0.0;
    const double bgPaddingY = background.has_value() ? background->paddingY : 0.0;
    const double boundingWidth  = contentWidth  + 2.0 * bgPaddingX;
    const double boundingHeight = contentHeight + 2.0 * bgPaddingY;
    bounding_width  = boundingWidth;
    bounding_height = boundingHeight;

    // 2a. Apply alignment anchoring to derive the bbox CENTRE on the project canvas.
    //     position.x is interpreted as:
    //       LEFT   -> left edge of bbox    (centre = positionX + boundingWidth/2)
    //       CENTER -> centre of bbox       (centre = positionX)
    //       RIGHT  -> right edge of bbox   (centre = positionX - boundingWidth/2)
    //     position.y is always the vertical centre.
    const double alignmentOffset = text::alignmentOffsetX(data.style.textAlign, boundingWidth);
    frame_center_project_x = data.transformation.positionX + alignmentOffset;
    frame_center_project_y = data.transformation.positionY;

    // 3. Resolve the animation timeline. Active only when an animation slot is set, the clip has
    //    a positive duration, and preset keyframe data is available to drive it.
    timeline.reset();
    has_animation = false;
    anim_char_count = 0;
    if (animations.hasActive() && anim_duration_sec > 0.0 && !presets.empty()) {
        timeline = text::buildAnimationTimeline(animations, anim_duration_sec);
        has_animation = timeline.has_value();
        for (const auto& line : layout.lines) anim_char_count += static_cast<int>(text::utf8Length(line.text));
    }

    // Cache the plan for per-frame rendering. Origin = top-left of the content box; the
    // background extends paddingX/Y beyond it, so centring the content box on (0,0) means
    // top-left = (-contentWidth/2, -contentHeight/2). Set BEFORE frame sizing so the worst-case
    // pass (resolvePlanAtFrame) can read plan_layout / plan_content_* for curved geometry.
    plan_paint = paint;
    plan_layout = layout;
    plan_background = background;
    plan_content_w = contentWidth;
    plan_content_h = contentHeight;
    plan_origin_x = -contentWidth / 2.0;
    plan_origin_y = -contentHeight / 2.0;

    // 4/5. Frame buffer sizing. The buffer must bound EVERY frame, so it is sized to the worst case
    // across the whole timeline: the animation reach (computeAnimatedExtent), the static/keyframed
    // 3D-tilt AABB, and the effect margins (shadow/stroke/blur/glow), all wrapped in the 2D-rotation
    // AABB. Background padding is static; only colours/glow/blur/tilt/curve vary per frame.
    const double bgPadX = background.has_value() ? background->paddingX : 0.0;
    const double bgPadY = background.has_value() ? background->paddingY : 0.0;

    double animHalfW = 0.0, animHalfH = 0.0;
    if (has_animation) {
        const text::AnimatedExtent extent = text::computeAnimatedExtent(
            layout, paint, boundingWidth, boundingHeight, *timeline, presets, anim_char_count);
        animHalfW = extent.halfWidth;
        animHalfH = extent.halfHeight;
    }

    // Axis-aligned extent of one frame given its (possibly keyframed) paint / content box / tilt.
    const double rotationRad = data.transformation.rotation * M_PI / 180.0;
    const double rotC = std::abs(std::cos(rotationRad));
    const double rotS = std::abs(std::sin(rotationRad));
    auto extentFor = [&](const text::TextClipPaintStyle& p, double cw, double ch,
                         double padX, double padY, double tiltX, double tiltY) -> std::pair<double, double> {
        double halfW = (cw + 2.0 * padX) / 2.0;
        double halfH = (ch + 2.0 * padY) / 2.0;
        if (has_animation) { halfW = std::max(halfW, animHalfW); halfH = std::max(halfH, animHalfH); }
        if (tiltX != 0.0 || tiltY != 0.0) {
            const text::TextClipAnimationFrame f = text::buildStatic3DFrame(tiltX, tiltY);
            const SkMatrix m = text::animationMatrix(f.props, p.fontSize, 2.0 * halfW, 2.0 * halfH, f.flags);
            const SkRect box = SkRect::MakeLTRB(static_cast<float>(-halfW), static_cast<float>(-halfH),
                                                static_cast<float>(halfW), static_cast<float>(halfH));
            SkRect mapped;
            m.mapRect(&mapped, box);
            halfW = std::max({std::abs(static_cast<double>(mapped.fLeft)), std::abs(static_cast<double>(mapped.fRight)), halfW});
            halfH = std::max({std::abs(static_cast<double>(mapped.fTop)),  std::abs(static_cast<double>(mapped.fBottom)), halfH});
        }
        const double pad = effectsMargin(p, cw, ch);
        const double preW = 2.0 * halfW + 2.0 * pad;
        const double preH = 2.0 * halfH + 2.0 * pad;
        return {preW * rotC + preH * rotS, preW * rotS + preH * rotC};
    };

    std::pair<double, double> aabb = extentFor(paint, contentWidth, contentHeight,
                                               bgPadX, bgPadY,
                                               data.transformation.tiltX, data.transformation.tiltY);
    double aabbW = aabb.first, aabbH = aabb.second;

    // Keyframed style/tilt: expand to the worst case across the whole animated range. The frame
    // extent is NON-MONOTONIC in several channels (e.g. a curveAngle arc bounding box peaks near
    // 180 deg; a BEZIER ease can overshoot past its endpoints), so sampling only the keyframe
    // control points would miss interior peaks and under-size the buffer (→ mid-animation clipping).
    // Instead sweep every frame across [1, lastFrame], capped to a sane sample budget for long clips.
    if (has_style_keyframes && style_keyframes.affectsFrameSize()) {
        int64_t lastFrame = 1;
        auto trackNum = [&](const std::optional<openshot::Keyframe>& k) {
            if (!k) return;
            for (int64_t i = 0; i < k->GetCount(); ++i)
                lastFrame = std::max(lastFrame, static_cast<int64_t>(std::llround(k->GetPoint(i).co.X)));
        };
        trackNum(style_keyframes.tiltX); trackNum(style_keyframes.tiltY);
        trackNum(style_keyframes.glowIntensityRatio); trackNum(style_keyframes.glowRangeRatio);
        trackNum(style_keyframes.glowDirectionX); trackNum(style_keyframes.glowDirectionY);
        trackNum(style_keyframes.blurRatio); trackNum(style_keyframes.curveAngle);
        trackNum(style_keyframes.strokeWidthRatio);
        trackNum(style_keyframes.shadowBlurRatio); trackNum(style_keyframes.shadowDistanceRatio);
        trackNum(style_keyframes.shadowAngle);
        trackNum(style_keyframes.backgroundPaddingXRatio); trackNum(style_keyframes.backgroundPaddingYRatio);
        trackNum(style_keyframes.backgroundRadiusRatio);
        // Colour channels are time-indexed; convert their last point to a frame.
        auto trackCol = [&](const text::ColorKeyframeChannel& ch) {
            for (const auto& p : ch.points)
                lastFrame = std::max(lastFrame, static_cast<int64_t>(std::llround(p.timeSec * anim_fps)) + 1);
        };
        trackCol(style_keyframes.color); trackCol(style_keyframes.strokeColor);
        trackCol(style_keyframes.shadowColor); trackCol(style_keyframes.backgroundColor);
        trackCol(style_keyframes.glowColor);

        constexpr int64_t kMaxSamples = 400;
        const int64_t step = std::max<int64_t>(1, (lastFrame + kMaxSamples - 1) / kMaxSamples);
        for (int64_t fr = 1; fr <= lastFrame; fr += step) {
            const ResolvedPlan rp = resolvePlanAtFrame(fr);
            const double fPadX = rp.background.has_value() ? rp.background->paddingX : 0.0;
            const double fPadY = rp.background.has_value() ? rp.background->paddingY : 0.0;
            const std::pair<double, double> e =
                extentFor(rp.paint, rp.content_w, rp.content_h, fPadX, fPadY, rp.tiltX, rp.tiltY);
            aabbW = std::max(aabbW, e.first);
            aabbH = std::max(aabbH, e.second);
        }
    }

    frame_width  = std::max(1, static_cast<int>(std::ceil(aabbW)));
    frame_height = std::max(1, static_cast<int>(std::ceil(aabbH)));
}

TextClipReader::ResolvedPlan TextClipReader::cachedPlan() const {
    ResolvedPlan p;
    p.paint = plan_paint;
    p.background = plan_background;
    p.content_w = plan_content_w;
    p.content_h = plan_content_h;
    p.origin_x = plan_origin_x;
    p.origin_y = plan_origin_y;
    p.tiltX = data.transformation.tiltX;
    p.tiltY = data.transformation.tiltY;
    return p;
}

TextClipReader::ResolvedPlan TextClipReader::resolvePlanAtFrame(int64_t frame) const {
    // Overwrite the keyframed fields on a copy of the static style/transformation.
    text::TextClipStyle s = data.style;
    text::TextTransformation tf = data.transformation;
    const text::TextStyleKeyframes& kf = style_keyframes;

    if (kf.tiltX) tf.tiltX = kf.tiltX->GetValue(frame);
    if (kf.tiltY) tf.tiltY = kf.tiltY->GetValue(frame);
    if (kf.glowIntensityRatio) s.glowIntensityRatio = kf.glowIntensityRatio->GetValue(frame);
    if (kf.glowRangeRatio)     s.glowRangeRatio     = kf.glowRangeRatio->GetValue(frame);
    if (kf.glowDirectionX)     s.glowDirectionX     = kf.glowDirectionX->GetValue(frame);
    if (kf.glowDirectionY)     s.glowDirectionY     = kf.glowDirectionY->GetValue(frame);
    if (kf.blurRatio)          s.blurRatio          = kf.blurRatio->GetValue(frame);
    if (kf.curveAngle)         s.curveAngle         = kf.curveAngle->GetValue(frame);
    if (kf.strokeWidthRatio)   s.strokeWidthRatio   = kf.strokeWidthRatio->GetValue(frame);
    if (kf.shadowBlurRatio)    s.shadowBlurRatio    = kf.shadowBlurRatio->GetValue(frame);
    if (kf.shadowDistanceRatio)s.shadowDistanceRatio= kf.shadowDistanceRatio->GetValue(frame);
    if (kf.shadowAngle)        s.shadowAngle        = kf.shadowAngle->GetValue(frame);
    if (kf.backgroundPaddingXRatio) s.backgroundPaddingXRatio = kf.backgroundPaddingXRatio->GetValue(frame);
    if (kf.backgroundPaddingYRatio) s.backgroundPaddingYRatio = kf.backgroundPaddingYRatio->GetValue(frame);
    if (kf.backgroundRadiusRatio)   s.backgroundRadiusRatio   = kf.backgroundRadiusRatio->GetValue(frame);

    // Colour channels are time-indexed (seconds) and resolve to a CSS string the paint path parses.
    const double tSec = std::max(0.0, static_cast<double>(frame - 1) / anim_fps);
    if (!kf.color.empty())
        s.color = text::sampleColorChannel(kf.color, tSec, s.color);
    if (!kf.strokeColor.empty())
        s.strokeColor = text::sampleColorChannel(kf.strokeColor, tSec, s.strokeColor.value_or(""));
    if (!kf.shadowColor.empty())
        s.shadowColor = text::sampleColorChannel(kf.shadowColor, tSec, s.shadowColor.value_or(""));
    if (!kf.backgroundColor.empty())
        s.backgroundColor = text::sampleColorChannel(kf.backgroundColor, tSec, s.backgroundColor.value_or(""));
    if (!kf.glowColor.empty())
        s.glowColor = text::sampleColorChannel(kf.glowColor, tSec, s.glowColor.value_or(""));

    ResolvedPlan p;
    p.paint = text::convertTextStyleToPaintStyle(s, tf, project_width);
    p.background = text::convertBackgroundStyle(s, p.paint);
    // Only a keyframed curveAngle changes glyph geometry (wrapping/layout are stable), so recompute
    // the curved content box per frame; otherwise reuse the cached (flat or static-curve) content.
    if (p.paint.curveAngle.has_value()) {
        const text::CurvedTextGeometry g = text::curvedGeometryForLayout(plan_layout, p.paint);
        p.content_w = g.width;
        p.content_h = g.height;
    } else {
        p.content_w = plan_content_w;
        p.content_h = plan_content_h;
    }
    p.origin_x = -p.content_w / 2.0;
    p.origin_y = -p.content_h / 2.0;
    p.tiltX = tf.tiltX;
    p.tiltY = tf.tiltY;
    return p;
}

std::shared_ptr<QImage> TextClipReader::renderToQImage(
    const ResolvedPlan& plan,
    const std::optional<text::TextClipAnimationFrame>& animation) {
    if (plan_empty) {
        auto img = std::make_shared<QImage>(1, 1, QImage::Format_RGBA8888_Premultiplied);
        img->fill(QColor(0, 0, 0, 0));
        return img;
    }

    auto img = std::make_shared<QImage>(frame_width, frame_height, QImage::Format_RGBA8888_Premultiplied);
    img->fill(QColor(0, 0, 0, 0));

    SkBitmap bitmap;
    const SkImageInfo skiaInfo = SkImageInfo::MakeN32Premul(frame_width, frame_height);
    if (!bitmap.installPixels(skiaInfo, img->bits(), img->bytesPerLine())) {
        return img;
    }
    SkCanvas canvas(bitmap);
    subtitle::SkiaRenderer renderer(&canvas);

    // Centre the content box at the frame's centre; rotate around that centre.
    canvas.save();
    canvas.translate(static_cast<float>(frame_width)  / 2.0f,
                     static_cast<float>(frame_height) / 2.0f);
    if (data.transformation.rotation != 0.0) {
        canvas.rotate(static_cast<float>(data.transformation.rotation));
    }
    text::renderTextFrame(plan_layout, plan.paint, plan.background,
                          plan.origin_x, plan.origin_y, 1.0, animation, &renderer);
    canvas.restore();

    return img;
}

void TextClipReader::renderToImage() {
    // A resting frame carrying only the static 3D tilt (if any) — constant across frames, so the
    // single-image cache still holds. Without tilt this is a plain static render (std::nullopt).
    std::optional<text::TextClipAnimationFrame> frame;
    if (has_tilt) {
        frame = text::buildStatic3DFrame(data.transformation.tiltX, data.transformation.tiltY);
    }
    rendered_image = renderToQImage(cachedPlan(), frame);
}

// ---------------------------------------------------------------------------
// GetFrame
// ---------------------------------------------------------------------------

std::shared_ptr<Frame> TextClipReader::GetFrame(int64_t requested_frame) {
    const std::lock_guard<std::recursive_mutex> lock(getFrameMutex);

    if (!is_open) Open();
    if (dirty) {
        buildPlan();
        initInfo();
        rendered_image.reset();
        dirty = false;
    }

    const int sample_count = Frame::GetSamplesPerFrame(requested_frame, info.fps, info.sample_rate, info.channels);

    // Resolve the active animation frame at this frame's clip-relative time (if any).
    std::optional<text::TextClipAnimationFrame> animFrame;
    if (has_animation && timeline.has_value()) {
        const double elapsedSec = std::max(0.0, static_cast<double>(requested_frame - 1) / anim_fps);
        const text::FramePlan plan = text::planFrame(elapsedSec, *timeline, anim_char_count, presets);
        if (plan.presetId.has_value()) {
            const auto it = presets.find(*plan.presetId);
            if (it != presets.end()) {
                animFrame = text::buildAnimationFrame(plan, it->second, elapsedSec);
            }
        }
    }

    std::shared_ptr<QImage> image;
    if (!has_style_keyframes && !animFrame.has_value()) {
        // Static / resting phase: the frame is pixel-identical every frame (incl. any static 3D
        // tilt). Render ONCE and reuse the cache — this skips recomputing the (expensive) glow for
        // every resting frame. For a 5s clip with a 1.5s IN that's ~70% of frames served from cache.
        if (!rendered_image) renderToImage();
        image = rendered_image;
    } else {
        // Per-frame render. Resolve the style keyframe overlay (if any), else reuse the cached plan.
        // No single-image cache here — the content varies frame to frame.
        const ResolvedPlan rp = has_style_keyframes ? resolvePlanAtFrame(requested_frame) : cachedPlan();
        const bool tilt = rp.tiltX != 0.0 || rp.tiltY != 0.0;
        std::optional<text::TextClipAnimationFrame> frame = animFrame;
        if (frame.has_value()) {
            // Fold the (static or keyframed) 3D tilt into the active animation frame: word frames add
            // it to their props; char frames carry it separately (baked flat then tilted as one unit).
            if (tilt) {
                if (frame->mode == text::AnimationMode::WORD) {
                    text::composeStatic3DIntoWordFrame(*frame, rp.tiltX, rp.tiltY);
                } else if (frame->mode == text::AnimationMode::CHAR) {
                    frame->static3D = std::make_pair(rp.tiltX, rp.tiltY);
                }
            }
        } else if (tilt) {
            frame = text::buildStatic3DFrame(rp.tiltX, rp.tiltY);
        }
        image = renderToQImage(rp, frame);
    }

    if (!image) {
        return std::make_shared<Frame>(requested_frame, std::max(1, frame_width),
                                       std::max(1, frame_height), "#00000000",
                                       sample_count, info.channels);
    }

    auto frame = std::make_shared<Frame>(
        requested_frame, image->width(), image->height(),
        "#00000000", sample_count, info.channels);
    frame->AddImage(std::make_shared<QImage>(image->copy()));
    return frame;
}

// ---------------------------------------------------------------------------
// Setters
// ---------------------------------------------------------------------------

void TextClipReader::SetText(const std::string& value) {
    data.value = value;
    dirty = true;
}

void TextClipReader::SetStyle(const text::TextClipStyle& style) {
    data.style = style;
    dirty = true;
}

void TextClipReader::SetTransformation(const text::TextTransformation& transformation) {
    data.transformation = transformation;
    dirty = true;
}

void TextClipReader::SetProjectWidth(int width) {
    project_width = width;
    dirty = true;
}

void TextClipReader::SetStyleKeyframes(const text::TextStyleKeyframes& keyframes, double fps) {
    style_keyframes = keyframes;
    has_style_keyframes = !keyframes.empty();
    if (fps > 0.0) anim_fps = fps;   // shared frame↔time clock (also set by SetAnimations)
    dirty = true;
}

void TextClipReader::SetAnimations(const text::TextAnimations& animations_,
                                   const text::AnimationPresetMap& presets_,
                                   double fps,
                                   double durationSec) {
    animations = animations_;
    presets = presets_;
    anim_fps = fps > 0.0 ? fps : 30.0;
    anim_duration_sec = durationSec;
    dirty = true;
}

// ---------------------------------------------------------------------------
// JSON
// ---------------------------------------------------------------------------

std::string TextClipReader::Json() const {
    return JsonValue().toStyledString();
}

Json::Value TextClipReader::JsonValue() const {
    Json::Value root = ReaderBase::JsonValue();
    root["type"] = "TextClipReader";
    root["project_width"] = project_width;
    root["value"] = data.value;
    root["style"] = styleToJson(data.style);
    root["transformation"] = transformationToJson(data.transformation);
    return root;
}

void TextClipReader::SetJson(const std::string value) {
    try {
        const Json::Value root = openshot::stringToJson(value);
        SetJsonValue(root);
    } catch (const std::exception& e) {
        throw InvalidJSON("JSON is invalid (missing keys or invalid data types)");
    }
}

void TextClipReader::SetJsonValue(const Json::Value root) {
    ReaderBase::SetJsonValue(root);

    if (!root["project_width"].isNull())   project_width = root["project_width"].asInt();
    if (!root["value"].isNull())           data.value = root["value"].asString();
    if (root["style"].isObject())          styleFromJson(root["style"], data.style);
    if (root["transformation"].isObject()) transformationFromJson(root["transformation"], data.transformation);

    dirty = true;

    if (is_open) {
        renderToImage();
        initInfo();
    }
}

} // namespace openshot
