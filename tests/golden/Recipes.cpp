#include "Recipes.h"

#include "FFmpegReader.h"
#include "QtImageReader.h"
#include "Timeline.h"
#include "effects/Alpha.h"
#include "effects/Bars.h"
#include "effects/Blur.h"
#include "effects/BorderReflectedMove.h"
#include "effects/BorderReflectedRotation.h"
#include "effects/Brightness.h"
#include "effects/CameraMovement.h"
#include "effects/CircleMask.h"
#include "effects/ColorShift.h"
#include "effects/Exposure.h"
#include "effects/SplitShift.h"
#include "effects/Wipe.h"
#include "effects/Zoom.h"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <stdexcept>

namespace golden::recipes {

using namespace openshot;

// ── keyframes ────────────────────────────────────────────────────────────────────────────────
Point makePoint(int frame, float value, const Interp& interp) {
    Point p(static_cast<float>(frame), value, interp.type);
    if (interp.type == BEZIER) {
        p.handle_type = MANUAL;
        p.Initialize_LeftHandle(interp.v3, interp.v4);
    }
    return p;
}

namespace {
void addOrReplacePoint(Keyframe& curve, const Point& p) {
    for (int64_t i = 0; i < curve.GetCount(); ++i) {
        if (std::abs(curve.GetPoint(i).co.X - p.co.X) < 0.5f) { curve.UpdatePoint(i, p); return; }
    }
    curve.AddPoint(p);
}
int64_t findPrevPointIndex(const Keyframe& curve, float frame) {
    int64_t best = -1;
    for (int64_t i = 0; i < curve.GetCount(); ++i)
        if (curve.GetPoint(i).co.X < frame - 0.5f) best = i;
    return best;
}
void patchPrevRightHandle(Keyframe& curve, int frame, const Interp& interp) {
    if (interp.type != BEZIER) return;
    const int64_t prevIdx = findPrevPointIndex(curve, static_cast<float>(frame));
    if (prevIdx < 0) return;
    Point prev = curve.GetPoint(prevIdx);
    prev.handle_type = MANUAL;
    prev.Initialize_RightHandle(interp.v1, interp.v2);
    curve.UpdatePoint(prevIdx, prev);
}
} // namespace

void appendKeyframePoint(Keyframe& curve, int frame, float value, const Interp& interp) {
    patchPrevRightHandle(curve, frame, interp);
    addOrReplacePoint(curve, makePoint(frame, value, interp));
}

int64_t RampWindow::frameAt(double t01) const {
    const auto span = static_cast<double>(endFrame - startFrame);
    return std::max<int64_t>(1, startFrame + std::llround(t01 * span));
}

Keyframe createRampKeyframe(const std::vector<RampKf>& kfs, const RampWindow& ramp) {
    std::vector<Point> points;
    for (const auto& kf : kfs) {
        Point p(static_cast<float>(ramp.frameAt(kf.time)), static_cast<float>(kf.value), kf.interp.type);
        if (kf.interp.type == BEZIER) {
            p.handle_type = MANUAL;
            p.Initialize_LeftHandle(kf.interp.v1, kf.interp.v2);
            p.Initialize_RightHandle(kf.interp.v3, kf.interp.v4);
        }
        points.push_back(p);
    }
    return Keyframe(points);
}

Keyframe mergeKeyframes(const Keyframe& a, const Keyframe& b) {
    Keyframe out = a;
    for (int64_t i = 0; i < b.GetCount(); ++i) addOrReplacePoint(out, b.GetPoint(i));
    return out;
}

// ── geometry ─────────────────────────────────────────────────────────────────────────────────
Size predictPrescaledSize_ScaleNone(const ReaderInfo& info, int previewW, int previewH) {
    int output_width = info.width, output_height = info.height;
    if (previewW > 0 && previewH > 0 && previewW < output_width && previewH < output_height) {
        const float ratio = static_cast<float>(output_width) / static_cast<float>(output_height);
        const int possible_w = std::round(previewH * ratio);
        const int possible_h = std::round(previewW / ratio);
        if (possible_w <= previewW) { output_width = possible_w; output_height = previewH; }
        else { output_width = previewW; output_height = possible_h; }
    }
    if (info.pixel_ratio.num != info.pixel_ratio.den && info.pixel_ratio.den != 0)
        output_width = std::max(1, static_cast<int>(std::round(output_width * double(info.pixel_ratio.num) / info.pixel_ratio.den)));
    return {output_width, output_height};
}

float calculateFitScale(Size source, Size video, float hScale, float vScale, const BBox& bbox) {
    const Size target{static_cast<int>(video.width * std::abs(hScale) * bbox.w),
                      static_cast<int>(video.height * std::abs(vScale) * bbox.h)};
    const float wr = static_cast<float>(target.width) / source.width;
    const float hr = static_cast<float>(target.height) / source.height;
    return std::min(wr, hr);
}

void applyTransform(Clip& clip, const Transform& t, Size source, Size video, double fps) {
    const int baselineFrame = timeToClipFrame(0.0, fps);
    float hS = t.hScale, vS = t.vScale, rot = t.rotation;
    BBox bbox = t.bbox;

    auto emitScale = [&](int frame, const Interp& in) {
        const float s = calculateFitScale(source, video, hS, vS, bbox);
        appendKeyframePoint(clip.scale_x, frame, s, in);
        appendKeyframePoint(clip.scale_y, frame, s, in);
    };
    auto emitLocation = [&](int frame, const Interp& in) {
        appendKeyframePoint(clip.location_x, frame, bbox.cx - 0.5f, in);
        appendKeyframePoint(clip.location_y, frame, bbox.cy - 0.5f, in);
    };
    auto emitRotation = [&](int frame, const Interp& in) { appendKeyframePoint(clip.rotation, frame, rot, in); };

    clip.scale_x = Keyframe(); clip.scale_y = Keyframe();
    clip.location_x = Keyframe(); clip.location_y = Keyframe();
    clip.rotation = Keyframe();
    clip.origin_x = Keyframe(0.5f); clip.origin_y = Keyframe(0.5f);

    emitScale(baselineFrame, kLinear);
    emitLocation(baselineFrame, kLinear);
    emitRotation(baselineFrame, kLinear);

    // The service walks all keyframes sorted by time; here the three tracks are applied in order.
    struct Ev { double time; int kind; size_t idx; };
    std::vector<Ev> evs;
    for (size_t i = 0; i < t.scaleKfs.size(); ++i) evs.push_back({t.scaleKfs[i].time, 0, i});
    for (size_t i = 0; i < t.positionKfs.size(); ++i) evs.push_back({t.positionKfs[i].time, 1, i});
    for (size_t i = 0; i < t.rotationKfs.size(); ++i) evs.push_back({t.rotationKfs[i].time, 2, i});
    std::stable_sort(evs.begin(), evs.end(), [](const Ev& a, const Ev& b) { return a.time < b.time; });
    for (const auto& e : evs) {
        const int frame = timeToClipFrame(e.time, fps);
        if (e.kind == 0) { hS = t.scaleKfs[e.idx].hScale; vS = t.scaleKfs[e.idx].vScale; emitScale(frame, t.scaleKfs[e.idx].interp); }
        else if (e.kind == 1) { bbox.cx = t.positionKfs[e.idx].cx; bbox.cy = t.positionKfs[e.idx].cy; emitLocation(frame, t.positionKfs[e.idx].interp); }
        else { rot = t.rotationKfs[e.idx].degrees; emitRotation(frame, t.rotationKfs[e.idx].interp); }
    }
}

namespace {
float getExpectedFadeAlpha(float timeSec, float mainOpacity, float fadeInDur, float fadeOutDur, float clipDuration) {
    float alpha = mainOpacity;
    if (fadeInDur > 0 && timeSec <= fadeInDur) alpha = mainOpacity * (timeSec / fadeInDur);
    else if (fadeOutDur > 0 && timeSec >= clipDuration - fadeOutDur) alpha = mainOpacity * ((clipDuration - timeSec) / fadeOutDur);
    return std::clamp(alpha, 0.f, mainOpacity);
}
void addKeyframePointWithHold(Keyframe& keyframe, int frame, float value, int animationFramesCount, InterpolationType type) {
    const auto pointsCount = keyframe.GetCount();
    const int prevFrame = frame - animationFramesCount;
    if (pointsCount > 0 && prevFrame > 0 && prevFrame > keyframe.GetPoint(pointsCount - 1).co.X)
        keyframe.AddPoint(frame - animationFramesCount, keyframe.GetPoint(pointsCount - 1).co.Y, type);
    keyframe.AddPoint(frame, value, type);
}
} // namespace

Keyframe buildAlphaKeyframe(float mainOpacity, double clipDuration, double fps, double trimStart,
                            const std::optional<Fade>& fade, const std::vector<std::pair<double, double>>& ghostIntervals) {
    Keyframe keyframe;
    const float fadeInDur = fade ? static_cast<float>(fade->fadeIn) : 0.f;
    const float fadeOutDur = fade ? static_cast<float>(fade->fadeOut) : 0.f;
    const int trimStartFrame = static_cast<int>(std::lround(fps * trimStart));
    const Interp easeOut = fade ? fade->easeOut : kLinear;

    if (fadeInDur > 0) {
        const float eiV1 = 1 - easeOut.v3, eiV2 = 1 - easeOut.v4, eiV3 = 1 - easeOut.v1, eiV4 = 1 - easeOut.v2;
        Point fadeInStart(static_cast<float>(trimStartFrame + 1), 0, easeOut.type);
        if (easeOut.type == BEZIER) { fadeInStart.handle_type = MANUAL; fadeInStart.Initialize_LeftHandle(0, 0); fadeInStart.Initialize_RightHandle(eiV1, eiV2); }
        keyframe.AddPoint(fadeInStart);
        const int fadeInEndFrame = trimStartFrame + timeToFrame(fadeInDur, fps);
        Point fadeInEnd(static_cast<float>(fadeInEndFrame), mainOpacity, easeOut.type);
        if (easeOut.type == BEZIER) { fadeInEnd.handle_type = MANUAL; fadeInEnd.Initialize_LeftHandle(eiV3, eiV4); fadeInEnd.Initialize_RightHandle(0, 0); }
        keyframe.AddPoint(fadeInEnd);
    } else {
        addKeyframePointWithHold(keyframe, trimStartFrame + 1, mainOpacity, 1, CONSTANT);
    }

    if (fadeOutDur > 0) {
        const int fadeOutStartFrame = trimStartFrame + timeToFrame(clipDuration - fadeOutDur, fps);
        const int lastFrame = trimStartFrame + timeToFrame(clipDuration, fps);
        Point fadeOutStart(static_cast<float>(fadeOutStartFrame), mainOpacity, easeOut.type);
        if (easeOut.type == BEZIER) { fadeOutStart.handle_type = MANUAL; fadeOutStart.Initialize_LeftHandle(0, 0); fadeOutStart.Initialize_RightHandle(easeOut.v1, easeOut.v2); }
        keyframe.AddPoint(fadeOutStart);
        Point fadeOutEnd(static_cast<float>(lastFrame), 0, easeOut.type);
        if (easeOut.type == BEZIER) { fadeOutEnd.handle_type = MANUAL; fadeOutEnd.Initialize_LeftHandle(easeOut.v3, easeOut.v4); fadeOutEnd.Initialize_RightHandle(0, 0); }
        keyframe.AddPoint(fadeOutEnd);
    }

    for (const auto& [gStart, gEnd] : ghostIntervals) {
        const int gStartFrame = timeToFrame(gStart, fps);
        const int gEndFrame = timeToFrame(gEnd, fps);
        const float holdAlpha = getExpectedFadeAlpha(static_cast<float>(gStart - trimStart), mainOpacity, fadeInDur, fadeOutDur, static_cast<float>(clipDuration));
        const float restoreAlpha = getExpectedFadeAlpha(static_cast<float>(gEnd - trimStart), mainOpacity, fadeInDur, fadeOutDur, static_cast<float>(clipDuration));
        for (int64_t i = keyframe.GetCount() - 1; i >= 0; --i) {
            const auto x = keyframe.GetPoint(i).co.X;
            if (x >= gStartFrame && x <= gEndFrame) keyframe.RemovePoint(i);
        }
        if (gStartFrame > 1) keyframe.AddPoint(gStartFrame - 1, holdAlpha, LINEAR);
        keyframe.AddPoint(gStartFrame, 0, CONSTANT);
        keyframe.AddPoint(gEndFrame + 1, restoreAlpha, CONSTANT);
    }
    return keyframe;
}

void mergeOpacityKeyframes(Keyframe& alpha, const std::vector<OpacityKf>& kfs, double fps) {
    for (const auto& kf : kfs) appendKeyframePoint(alpha, timeToClipFrame(kf.time, fps), kf.value, kf.interp);
}

// ── clips ────────────────────────────────────────────────────────────────────────────────────
ReaderBase* imageReader(Scene& scene, const std::string& path) { return scene.own(new QtImageReader(path)); }
ReaderBase* videoReader(Scene& scene, const std::string& path) { return scene.own(new FFmpegReader(path)); }

Clip* backgroundClip(Scene& scene, const std::string& path) {
    auto* clip = scene.own(new Clip(imageReader(scene, path)));
    clip->Position(0);
    clip->Start(0);
    clip->End(360000);
    clip->Layer(1);
    clip->scale = SCALE_CROP;
    clip->gravity = GRAVITY_CENTER;
    return clip;
}

Clip* mediaClip(Scene& scene, const MediaSpec& spec) {
    const double fps = scene.fps.ToDouble();
    ReaderBase* reader = spec.isImage ? imageReader(scene, spec.path) : videoReader(scene, spec.path);
    reader->Open();
    const double clipOrigDuration = spec.end - spec.trimEnd - spec.start - spec.trimStart;
    const double durOut = clipOrigDuration / spec.speed;

    auto* clip = scene.own(new Clip(reader));
    clip->Position(spec.start + spec.trimStart / spec.speed);
    clip->Start(spec.trimStart);
    clip->End(spec.trimStart + durOut);
    clip->Layer(layerOf(spec.track, spec.priority));
    if (spec.speed != 1) {
        const int64_t outFrames = static_cast<int64_t>(std::llround(durOut * fps)) + 1;
        const int64_t x0 = static_cast<int64_t>(std::llround(clip->Start() * fps)) + 1;
        const int64_t y0 = x0;
        const int64_t x1 = x0 + outFrames - 1;
        const int64_t y1 = y0 + static_cast<int64_t>(std::llround(spec.speed * (outFrames - 1)));
        Keyframe timeKf;
        timeKf.AddPoint(Point(static_cast<float>(x0), static_cast<float>(y0), LINEAR));
        timeKf.AddPoint(Point(static_cast<float>(x1), static_cast<float>(y1), LINEAR));
        clip->time = timeKf;
    }
    clip->volume = 0;
    clip->scale = SCALE_NONE;
    clip->gravity = GRAVITY_CENTER;
    if (spec.transform) {
        const Size src = predictPrescaledSize_ScaleNone(reader->info, scene.width, scene.height);
        applyTransform(*clip, *spec.transform, src, Size{scene.width, scene.height}, fps);
    }
    clip->alpha = buildAlphaKeyframe(spec.opacity, durOut, fps, spec.trimStart, spec.fade, spec.ghostIntervals);
    mergeOpacityKeyframes(clip->alpha, spec.opacityKfs, fps);
    return clip;
}

Crop* cropEffect(Keyframe left, Keyframe top, Keyframe right, Keyframe bottom, Keyframe radius) {
    auto* crop = new Crop(left, top, right, bottom, radius);
    crop->info.apply_before_clip = true;
    crop->resize = false;
    crop->Layer(999999);
    return crop;
}

Mask* imageMask(Scene& scene, const std::string& path) {
    ReaderBase* reader = imageReader(scene, path);
    reader->Open();
    auto* mask = new Mask(reader, Keyframe(0.0), Keyframe(3.0));
    mask->invert = true;
    return mask;
}

Mask* videoMask(Scene& scene, const std::string& path, const Clip& parent, double fps) {
    ReaderBase* reader = videoReader(scene, path);
    reader->Open();
    auto* mapper = scene.own(new ClipSyncedFrameMapper(reader, Fraction(static_cast<int>(fps), 1), PULLDOWN_NONE,
                                                       44100, 2, LAYOUT_STEREO, &parent));
    mapper->Open();
    auto* mask = new Mask(mapper, Keyframe(0.0), Keyframe(3.0));
    mask->invert = true;
    return mask;
}

// ── text ─────────────────────────────────────────────────────────────────────────────────────
text::TextClipStyle baseStyle(const std::string& fontPath) {
    text::TextClipStyle s;
    s.fontFamily = fontPath;
    s.fontWeight = 400;
    s.color = "#FFFFFF";
    s.textAlign = text::TextAlignment::CENTER;
    return s;
}

const text::AnimationPresetMap& presets() {
    static const text::AnimationPresetMap map = [] {
        using namespace text;
        AnimationPresetMap m;
        auto track = [](std::vector<std::pair<double, double>> pv) {
            std::vector<PresetKeyframe> out;
            for (auto [pct, v] : pv) out.push_back(PresetKeyframe{pct, v, std::nullopt});
            return out;
        };
        {   // whole box fades in
            AnimationPreset p; p.level = AnimationPresetLevel::BOX;
            p.keyframes.tracks[(size_t)AnimProp::opacity] = track({{0, 0}, {100, 1}});
            m["fade-in"] = p;
        }
        {   // characters rise and fade in, staggered from the first character.
            // tx/ty are in fontSize units, as the service passes them through (TextClipData.cpp):
            // production payloads use fractions (ty in [-0.27, 0.5], tx in [-2, 0]), so keep these
            // in that range — a value of 40 would translate each glyph 40 font sizes off screen.
            AnimationPreset p; p.level = AnimationPresetLevel::CHAR;
            p.keyframes.tracks[(size_t)AnimProp::opacity] = track({{0, 0}, {100, 1}});
            p.keyframes.tracks[(size_t)AnimProp::ty] = track({{0, 0.4}, {100, 0}});
            p.keyframes.staggerFrom = StaggerFrom::FIRST;
            p.keyframes.easing = CubicBezier{0.22, 1.0, 0.36, 1.0};
            m["rise-chars"] = p;
        }
        {   // loop: gentle scale pulse of the box
            AnimationPreset p; p.level = AnimationPresetLevel::BOX;
            p.keyframes.tracks[(size_t)AnimProp::sx] = track({{0, 1}, {50, 1.15}, {100, 1}});
            p.keyframes.tracks[(size_t)AnimProp::sy] = track({{0, 1}, {50, 1.15}, {100, 1}});
            m["pulse"] = p;
        }
        {   // words drop out with rotation
            AnimationPreset p; p.level = AnimationPresetLevel::WORD;
            p.keyframes.tracks[(size_t)AnimProp::opacity] = track({{0, 1}, {100, 0}});
            p.keyframes.tracks[(size_t)AnimProp::ty] = track({{0, 0}, {100, 0.6}});
            p.keyframes.tracks[(size_t)AnimProp::rotate] = track({{0, 0}, {100, 25}});
            p.keyframes.staggerFrom = StaggerFrom::LAST;
            m["drop-words"] = p;
        }
        return m;
    }();
    return map;
}

Clip* textClip(Scene& scene, const TextSpec& spec, double fps) {
    text::TextClipData data;
    data.value = spec.value;
    data.style = spec.style;
    data.transformation = spec.transformation;
    data.transformation.positionX = 0.0;
    data.transformation.positionY = 0.0;
    data.transformation.rotation = 0.0;
    const double renderSize = data.transformation.size;

    auto* reader = new TextClipReader(scene.width, scene.height, data);
    scene.own(reader);
    const double durationSec = spec.end - spec.start;
    if (spec.animations) reader->SetAnimations(*spec.animations, presets(), fps, durationSec);
    if (spec.styleKeyframes) reader->SetStyleKeyframes(*spec.styleKeyframes, fps);
    reader->Open();

    auto* clip = scene.own(new Clip(reader));
    clip->Position(spec.start);
    clip->Start(0);
    clip->End(durationSec);
    clip->Layer(layerOf(spec.track, spec.priority));
    clip->scale = SCALE_NONE;
    clip->gravity = GRAVITY_CENTER;

    float alignSignX = 0.f;
    switch (data.style.textAlign) {
        case text::TextAlignment::LEFT:  alignSignX = 1.f; break;
        case text::TextAlignment::RIGHT: alignSignX = -1.f; break;
        default: break;
    }
    // applyTextTransformKeyframes baseline (no TEXT_SIZE / TEXT_POSITION keyframes here).
    const int baselineFrame = timeToClipFrame(0.0, fps);
    const double bboxW = reader->BoundingWidth();
    const double frameCenterCanvasX = spec.posX * scene.width + alignSignX * (bboxW / 2.0);
    const double frameCenterCanvasY = spec.posY * scene.height;
    clip->scale_x = Keyframe(); clip->scale_y = Keyframe();
    clip->location_x = Keyframe(); clip->location_y = Keyframe();
    clip->rotation = Keyframe();
    clip->origin_x = Keyframe(0.5f); clip->origin_y = Keyframe(0.5f);
    const float s = renderSize > 0.0 ? 1.f : 1.f;
    appendKeyframePoint(clip->scale_x, baselineFrame, s, kLinear);
    appendKeyframePoint(clip->scale_y, baselineFrame, s, kLinear);
    appendKeyframePoint(clip->location_x, baselineFrame, static_cast<float>(frameCenterCanvasX / scene.width - 0.5), kLinear);
    appendKeyframePoint(clip->location_y, baselineFrame, static_cast<float>(frameCenterCanvasY / scene.height - 0.5), kLinear);
    appendKeyframePoint(clip->rotation, baselineFrame, spec.rotation, kLinear);

    clip->alpha = buildAlphaKeyframe(spec.opacity, durationSec, fps, 0.0, spec.fade);
    return clip;
}

// ── transitions ──────────────────────────────────────────────────────────────────────────────
const char* name(TransitionEffect e) {
    switch (e) {
        case TransitionEffect::Alpha: return "alpha";
        case TransitionEffect::Blur: return "blur";
        case TransitionEffect::DiagonalBlur: return "diagonal_blur";
        case TransitionEffect::RotationalBlur: return "rotational_blur";
        case TransitionEffect::ZoomBlur: return "zoom_blur";
        case TransitionEffect::Zoom: return "zoom";
        case TransitionEffect::BorderReflectedMove: return "border_reflected_move";
        case TransitionEffect::BorderReflectedRotation: return "border_reflected_rotation";
        case TransitionEffect::Bars: return "bars";
        case TransitionEffect::ThresholdWipeMask: return "threshold_wipe_mask";
        case TransitionEffect::SplitShift: return "split_shift";
        case TransitionEffect::CircleMask: return "circle_mask";
        case TransitionEffect::Exposure: return "exposure";
        case TransitionEffect::Brightness: return "brightness";
        case TransitionEffect::ColorShift: return "color_shift";
    }
    return "?";
}

namespace {
// A linear ramp from a to b over the window; `outgoing` flips direction so the incoming clip recovers.
Keyframe ramp(double a, double b, const RampWindow& w, bool outgoing) {
    return outgoing ? createRampKeyframe({{0.0, a}, {1.0, b}}, w) : createRampKeyframe({{0.0, b}, {1.0, a}}, w);
}
Keyframe withLeadingZero(const Keyframe& k) {   // ColorShift: CONSTANT 0 one frame before the first point
    Keyframe out;
    out.AddPoint(Point(k.GetPoint(0).co.X - 1, 0, CONSTANT));
    return mergeKeyframes(out, k);
}
} // namespace

void addTransitionEffect(Clip& clip, TransitionEffect e, const RampWindow& w, bool out) {
    switch (e) {
        case TransitionEffect::Alpha:
            clip.AddEffect(new Alpha(ramp(1, 0, w, out))); break;
        case TransitionEffect::Blur:
            clip.AddEffect(new Blur(ramp(0, 40, w, out), ramp(0, 40, w, out), 0, 0, 0, 0, 0, 0, 1)); break;
        case TransitionEffect::DiagonalBlur:
            clip.AddEffect(new Blur(0, 0, ramp(0, 40, w, out), 0, 0, 0, 0, 0, 1)); break;
        case TransitionEffect::RotationalBlur:
            clip.AddEffect(new Blur(0, 0, 0, ramp(0, 25, w, out), 0, 0, 0, 0, 1)); break;
        case TransitionEffect::ZoomBlur:
            clip.AddEffect(new Blur(0, 0, 0, 0, ramp(0, 60, w, out), 0.5, 0.5, 0, 1)); break;
        case TransitionEffect::Zoom:
            clip.AddEffect(new Zoom(ramp(100, 220, w, out), 0.5, 0.5)); break;
        case TransitionEffect::BorderReflectedMove:
            clip.AddEffect(new BorderReflectedMove(ramp(0, 0.3, w, out), 0)); break;   // dx is a fraction of the width
        case TransitionEffect::BorderReflectedRotation:
            clip.AddEffect(new BorderReflectedRotation(ramp(0, 45, w, out))); break;
        case TransitionEffect::Bars:
            clip.AddEffect(new Bars(Color("#000000"), ramp(0, 0.5, w, out), ramp(0, 0.5, w, out), ramp(0, 0.5, w, out), ramp(0, 0.5, w, out))); break;
        case TransitionEffect::ThresholdWipeMask: {
            Keyframe low = ramp(0, 80, w, out), high = ramp(20, 100, w, out);
            Keyframe enabled;
            enabled.AddPoint(1, 0, CONSTANT);
            enabled.AddPoint(std::max<double>(low.GetPoint(0).co.X, 1.0), 1, CONSTANT);
            clip.AddEffect(new Wipe(low, high, enabled));
            break;
        }
        case TransitionEffect::SplitShift:
            clip.AddEffect(new SplitShift(ramp(0, 1, w, out), false, 0.5)); break;
        case TransitionEffect::CircleMask:
            clip.AddEffect(new CircleMask(ramp(1, 0, w, out))); break;
        case TransitionEffect::Exposure:
            clip.AddEffect(new Exposure(ramp(1, 3, w, out))); break;
        case TransitionEffect::Brightness:
            clip.AddEffect(new Brightness(ramp(0, 1, w, out))); break;
        case TransitionEffect::ColorShift:
            clip.AddEffect(new ColorShift(withLeadingZero(ramp(0, 0.3, w, out)), 0, 0, withLeadingZero(ramp(0, -0.2, w, out)),
                                          withLeadingZero(ramp(0, -0.3, w, out)), 0, 0, 0));
            break;
    }
}

int64_t clipFirstTimelineFrame(const Clip& clip, double fps) { return std::llround(clip.Position() * fps) + 1; }
int64_t clipLastTimelineFrame(const Clip& clip, double fps) { return std::llround((clip.Position() + clip.Duration()) * fps); }
namespace {
int64_t clipFirstLocalFrame(const Clip& clip, double fps) { return static_cast<int64_t>(clip.Start() * fps) + 1; }
int64_t toLocalFrame(const Clip& clip, int64_t tf, double fps) { return tf - clipFirstTimelineFrame(clip, fps) + clipFirstLocalFrame(clip, fps); }
} // namespace
RampWindow toLocalWindow(const Clip& clip, int64_t first, int64_t last, double fps) {
    return {toLocalFrame(clip, first, fps), toLocalFrame(clip, last, fps)};
}

void applyOverlappingTransition(Clip& outClip, Clip& inClip, double duration, double fps,
                                const std::vector<TransitionEffect>& effects) {
    const float half = static_cast<float>(duration / 2.0);
    // pass 2: holds
    {   // out clip: tail only
        outClip.End(outClip.EndRaw() + half);
        const auto grownFrames = std::llround(half * fps);
        outClip.mFreezeFramesCountAtEnd = std::max<int64_t>(0, grownFrames);
    }
    {   // in clip: head only
        const float head = std::min(half, std::max(0.f, inClip.Position()));
        inClip.End(inClip.EndRaw() + head);
        if (head > 0.f) {
            const auto positionFrameBefore = std::llround(inClip.Position() * fps);
            inClip.Position(inClip.Position() - head);
            inClip.mFreezeFramesCountAtBeginning = std::max<int64_t>(0, positionFrameBefore - std::llround(inClip.Position() * fps));
        }
    }
    // pass 3: layers
    outClip.Layer(inClip.Layer() + 1);
    // pass 4: windows
    const auto rampFrames = std::max<int64_t>(1, std::llround(duration * fps));
    int64_t outWindowEnd = clipLastTimelineFrame(outClip, fps);
    int64_t outWindowStart = outWindowEnd - rampFrames + 1;
    int64_t inWindowStart = clipFirstTimelineFrame(inClip, fps);
    int64_t inWindowEnd = inWindowStart + rampFrames - 1;
    const auto sharedStart = std::max(inWindowStart, clipFirstTimelineFrame(outClip, fps));
    const auto sharedEnd = std::min(outWindowEnd, clipLastTimelineFrame(inClip, fps));
    if (sharedEnd > sharedStart) { outWindowStart = inWindowStart = sharedStart; outWindowEnd = inWindowEnd = sharedEnd; }
    for (auto e : effects) {
        addTransitionEffect(outClip, e, toLocalWindow(outClip, outWindowStart, outWindowEnd, fps), true);
        addTransitionEffect(inClip, e, toLocalWindow(inClip, inWindowStart, inWindowEnd, fps), false);
    }
}

Clip* addOverlayClip(Scene& scene, const std::string& path, double duration, Clip::OverlayType type,
                     Clip* outClip, Clip* inClip, double hDisp, double vDisp) {
    auto* overlay = scene.own(new Clip(path));   // Clip(path) owns its reader
    overlay->Start(overlay->info.duration / 2.f - duration / 2.f);
    overlay->End(overlay->info.duration / 2.f + duration / 2.f);
    overlay->scale = SCALE_NONE;
    overlay->isOverlay = true;
    overlay->Open();
    const double overlayFps = overlay->info.fps.ToDouble();
    const RampWindow overlayRamp{std::llround(overlay->Start() * overlayFps), std::llround(overlay->EndRaw() * overlayFps)};
    if (type == Clip::DISPLACEMENT_MAP) {
        overlay->horizontal_displacement = createRampKeyframe({{0.0, 0.0}, {0.5, hDisp}, {1.0, 0.0}}, overlayRamp);
        overlay->vertical_displacement = createRampKeyframe({{0.0, 0.0}, {0.5, vDisp}, {1.0, 0.0}}, overlayRamp);
    }
    if (outClip) outClip->AddOverlayClip(overlay, type, true);
    if (inClip) inClip->AddOverlayClip(overlay, type, false);
    return overlay;
}

// ── camera movement ──────────────────────────────────────────────────────────────────────────
namespace {
Keyframe animKeyframe(const std::vector<RampKf>& kfs, double duration, double start, double fps) {
    std::vector<Point> points;
    for (const auto& kf : kfs) {
        Point p(static_cast<float>(timeToFrame(start + kf.time * duration, fps)), static_cast<float>(kf.value), kf.interp.type);
        if (kf.interp.type == BEZIER) {
            p.handle_type = MANUAL;
            p.Initialize_LeftHandle(kf.interp.v1, kf.interp.v2);
            p.Initialize_RightHandle(kf.interp.v3, kf.interp.v4);
        }
        points.push_back(p);
    }
    return Keyframe(points);
}
} // namespace

void addCameraMovement(Clip& clip, double startSec, double endSec, double fps, const std::vector<RampKf>& zoom,
                       const std::vector<RampKf>& rotate, const std::vector<RampKf>& moveX, const std::vector<RampKf>& moveY) {
    const double duration = endSec - startSec;
    clip.AddEffect(new CameraMovement(animKeyframe(zoom, duration, startSec, fps), animKeyframe(rotate, duration, startSec, fps),
                                      animKeyframe(moveX, duration, startSec, fps), animKeyframe(moveY, duration, startSec, fps)));
}

// ── subtitles ────────────────────────────────────────────────────────────────────────────────
std::string subtitlesJson(const std::string& fontPath, int exportWidth, SubtitleVariant v) {
    // Values are already in pixels (the service's preprocessSizeFields has run); fontSize scales with width.
    const int fontSize = exportWidth / 16;
    std::ostringstream o;
    o << "{\"settings\":{"
      << "\"defaultStyle\":{\"fontFamily\":\"" << fontPath << "\",\"fontSize\":" << fontSize
      << ",\"fontWeight\":700,\"color\":\"#FFFFFF\",\"strokeColor\":\"#000000\",\"strokeWidth\":" << fontSize / 12
      << ",\"shadowColor\":\"#000000\",\"shadowOpacity\":0.6,\"shadowBlur\":" << fontSize / 8 << ",\"shadowDistance\":" << fontSize / 16 << "},"
      << "\"transformation\":{\"maxWidth\":" << exportWidth * 3 / 4 << ",\"center\":{\"x\":0.5,\"y\":0.85}}";
    if (v == SubtitleVariant::OneWordContainer) {
        o << ",\"containerStyle\":{\"appearance\":\"ONE_WORD\",\"textAlign\":\"CENTER\",\"color\":\"#FFD040\",\"opacity\":0.9,"
             "\"paddingX\":" << fontSize / 3 << ",\"paddingY\":" << fontSize / 6 << ",\"radius\":" << fontSize / 4 << "}";
    }
    if (v == SubtitleVariant::AnimatedInOut) {
        o << ",\"animationSettings\":{\"level\":\"WORD\",\"inDuration\":250,\"inInterpolation\":{\"type\":\"BEZIER\"},"
             "\"inStyles\":{\"opacity\":0,\"translateY\":" << fontSize / 2 << ",\"color\":\"#FF4040\"},"
             "\"outDuration\":150,\"outInterpolation\":{\"type\":\"LINEAR\"},\"outStyles\":{\"opacity\":0.3}}";
    }
    o << "},\"segments\":["
      << "{\"id\":\"s1\",\"startTime\":0,\"endTime\":1500,\"visible\":true,\"attached\":true,\"wordDetails\":["
         "{\"word\":\"Golden\",\"startTime\":0,\"endTime\":500},{\"word\":\"frames\",\"startTime\":500,\"endTime\":1000},"
         "{\"word\":\"stay\",\"startTime\":1000,\"endTime\":1500}]},"
      << "{\"id\":\"s2\",\"startTime\":1500,\"endTime\":3000,\"visible\":true,\"attached\":true,\"wordDetails\":["
         "{\"word\":\"on\",\"startTime\":1500,\"endTime\":1900},{\"word\":\"the\",\"startTime\":1900,\"endTime\":2200},"
         "{\"word\":\"GPU!\",\"startTime\":2200,\"endTime\":3000}]}"
      << "]}";
    return o.str();
}

// ── writer ───────────────────────────────────────────────────────────────────────────────────
void configureWriter(FFmpegWriter& w, int width, int height, Fraction fps, int bitrate, const std::string& codec) {
    w.SetSilentAudioMode(true);
    w.SetSkipClipAudioProcessing(true);
    w.SetPipelineMode(true);
    w.SetAudioOptions(true, "aac", 48000, 2, LAYOUT_STEREO, 128000);
    w.SetVideoOptions(codec, width, height, fps, bitrate);
    w.PrepareStreams();
    if (codec.find("nvenc") != std::string::npos) {
        // Hardware encoder: the x264-only options (x264-params) do not exist here, but "crf"
        // does now -- FFmpegWriter maps it onto NVENC's constant-quality control, so both
        // encoders are driven by the same quality number. These are exactly what
        // VideoRenderingImpl.cpp asks for when RenderBackend::usingHardwareEncoder() is true,
        // including the BT.709 tagging that x264 carries inside x264-params. Measuring anything
        // else here measures a configuration production never runs.
        w.SetOption(VIDEO_STREAM, "preset", "p4");   // as the service (W25: p5 quality, 2x the speed)
        w.SetOption(VIDEO_STREAM, "tune", "hq");
        w.SetOption(VIDEO_STREAM, "crf", "18");      // same knob as x264; the writer maps it to cq
        w.SetOption(VIDEO_STREAM, "color_primaries", "bt709");
        w.SetOption(VIDEO_STREAM, "color_trc", "bt709");
        w.SetOption(VIDEO_STREAM, "colorspace", "bt709");
    } else {
        w.SetOption(VIDEO_STREAM, "crf", "18");
        w.SetOption(VIDEO_STREAM, "preset", "medium");
        w.SetOption(VIDEO_STREAM, "x264-params", "colorprim=bt709:transfer=bt709:colormatrix=bt709");
    }
    w.SetOption(VIDEO_STREAM, "g", "30");
    w.SetOption(VIDEO_STREAM, "use_editlist", "0");
}

} // namespace golden::recipes
