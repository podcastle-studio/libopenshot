#include "TextAnimationEngine.h"

#include <algorithm>
#include <cmath>

namespace openshot {
namespace text {

const ResolvedAnimProps& identityProps() {
    static const ResolvedAnimProps identity = [] {
        ResolvedAnimProps p;
        p[AnimProp::opacity] = 1.0;
        p[AnimProp::sx] = 1.0;
        p[AnimProp::sy] = 1.0;
        p[AnimProp::brightness] = 1.0;
        p[AnimProp::contrast] = 1.0;
        p[AnimProp::clipGT] = 1.0;
        p[AnimProp::clipGB] = 1.0;
        // All remaining properties default to 0 (tx, ty, tz, rotate*, blur, skew*, clip*, perspective).
        return p;
    }();
    return identity;
}

// ── Cubic-bezier easing ──────────────────────────────────────────────────────

namespace {

double cubicBezierCoord(double p1, double p2, double u) {
    const double inv = 1.0 - u;
    return 3.0 * p1 * u * inv * inv + 3.0 * p2 * u * u * inv + u * u * u;
}

double cubicBezierDeriv(double p1, double p2, double u) {
    const double inv = 1.0 - u;
    return 3.0 * p1 * inv * inv + 6.0 * (p2 - p1) * u * inv + 3.0 * (1.0 - p2) * u * u;
}

} // namespace

double applyCubicBezier(double x1, double y1, double x2, double y2, double t) {
    if (t <= 0.0) return 0.0;
    if (t >= 1.0) return 1.0;

    double lo = 0.0, hi = 1.0, u = t;
    for (int i = 0; i < 8; ++i) {
        const double x = cubicBezierCoord(x1, x2, u);
        if (std::abs(x - t) < 1e-6) break;
        if (x < t) lo = u; else hi = u;
        u = (lo + hi) / 2.0;
    }
    for (int i = 0; i < 4; ++i) {
        const double dx = cubicBezierDeriv(x1, x2, u);
        if (std::abs(dx) < 1e-8) break;
        u -= (cubicBezierCoord(x1, x2, u) - t) / dx;
    }
    return cubicBezierCoord(y1, y2, u);
}

// ── Keyframe evaluation ──────────────────────────────────────────────────────

namespace {

double sampleTrack(const std::vector<PresetKeyframe>& track, double progress,
                   const std::optional<CubicBezier>& easing) {
    const double pct = progress * 100.0;
    if (pct <= track.front().pct) return track.front().value;
    if (pct >= track.back().pct) return track.back().value;

    for (size_t i = 1; i < track.size(); ++i) {
        if (pct <= track[i].pct) {
            const PresetKeyframe& prev = track[i - 1];
            const PresetKeyframe& curr = track[i];
            const double range = curr.pct - prev.pct;
            double t = range > 0.0 ? (pct - prev.pct) / range : 0.0;

            if (curr.interpolation.has_value()) {
                if (curr.interpolation->type == InterpolationType::CONSTANT) return prev.value;
                if (curr.interpolation->type == InterpolationType::BEZIER) {
                    const CubicBezier& p = curr.interpolation->params;
                    t = applyCubicBezier(p.x1, p.y1, p.x2, p.y2, t);
                } else if (easing.has_value()) {
                    t = applyCubicBezier(easing->x1, easing->y1, easing->x2, easing->y2, t);
                }
            } else if (easing.has_value()) {
                t = applyCubicBezier(easing->x1, easing->y1, easing->x2, easing->y2, t);
            }
            return prev.value + (curr.value - prev.value) * t;
        }
    }
    return track.back().value;
}

} // namespace

ResolvedAnimProps evalKeyframes(const PresetKeyframeSet& preset, double progress) {
    ResolvedAnimProps out;
    const ResolvedAnimProps& identity = identityProps();
    const double pct = progress * 100.0;
    for (size_t k = 0; k < kAnimPropCount; ++k) {
        const auto& track = preset.tracks[k];
        const AnimProp key = static_cast<AnimProp>(k);
        if (track.empty() || pct < track.front().pct) {
            out[key] = identity[key];
        } else {
            out[key] = sampleTrack(track, progress, preset.easing);
        }
    }
    return out;
}

std::optional<std::vector<std::pair<double, double>>> evalPolygon(
    const std::vector<PresetPolygonKeyframe>& polyTrack, double progress) {
    if (polyTrack.empty()) return std::nullopt;
    const double pct = progress * 100.0;
    if (pct < polyTrack.front().pct) return std::nullopt;
    if (pct >= polyTrack.back().pct) return polyTrack.back().points;

    for (size_t i = 1; i < polyTrack.size(); ++i) {
        if (pct <= polyTrack[i].pct) {
            const auto& prev = polyTrack[i - 1];
            const auto& curr = polyTrack[i];
            const double range = curr.pct - prev.pct;
            const double t = range > 0.0 ? (pct - prev.pct) / range : 0.0;
            if (prev.points.size() != curr.points.size()) return curr.points;
            std::vector<std::pair<double, double>> result;
            result.reserve(prev.points.size());
            for (size_t k = 0; k < prev.points.size(); ++k) {
                result.emplace_back(
                    prev.points[k].first + (curr.points[k].first - prev.points[k].first) * t,
                    prev.points[k].second + (curr.points[k].second - prev.points[k].second) * t);
            }
            return result;
        }
    }
    return polyTrack.back().points;
}

ActiveRange presetActiveRange(const PresetKeyframeSet& preset) {
    double leadEnd = 100.0;
    double settle = 0.0;
    bool any = false;
    for (const auto& tr : preset.tracks) {
        if (tr.empty()) continue;
        any = true;
        const double first = tr.front().value;
        const double last = tr.back().value;
        double le = tr.front().pct;
        for (size_t i = 1; i < tr.size(); ++i) {
            if (tr[i].value == first) le = tr[i].pct; else break;
        }
        double se = tr.back().pct;
        for (size_t i = tr.size() - 1; i-- > 0;) {
            if (tr[i].value == last) se = tr[i].pct; else break;
        }
        if (le < leadEnd) leadEnd = le;
        if (se > settle) settle = se;
    }
    if (!any) return {0.0, 1.0};
    double start = leadEnd / 100.0;
    double end = settle / 100.0;
    if (end <= start) { start = 0.0; end = 1.0; }
    return {start, end};
}

CharStagger computeCharStagger(double duration, int n, bool isLoop) {
    if (n <= 1) return {duration, 0.0};
    if (isLoop) {
        const double stagger = std::min(duration * 0.15, duration / n);
        return {duration, stagger};
    }
    constexpr double CHAR_ANIM_FRACTION = 0.55;
    const double perCharDuration = duration * CHAR_ANIM_FRACTION;
    const double stagger = (duration - perCharDuration) / (n - 1);
    return {perCharDuration, stagger};
}

std::optional<AnimationTimeline> buildAnimationTimeline(const TextAnimations& anims, double textDurationSec) {
    if (!anims.hasActive()) return std::nullopt;
    AnimationTimeline t;
    t.inPresetId = anims.inAnimationId;
    t.inDuration = anims.inAnimationDuration.value_or(DEFAULT_TEXT_ANIMATION_DURATION);
    t.loopPresetId = anims.loopAnimationId;
    t.loopDuration = anims.loopAnimationDuration.value_or(DEFAULT_TEXT_ANIMATION_DURATION);
    t.outPresetId = anims.outAnimationId;
    t.outDuration = anims.outAnimationDuration.value_or(DEFAULT_TEXT_ANIMATION_DURATION);
    t.textDuration = textDurationSec;
    return t;
}

namespace {

AnimationMode presetMode(const std::optional<std::string>& presetId, const AnimationPresetMap& presets) {
    if (!presetId.has_value()) return AnimationMode::NONE;
    const auto it = presets.find(*presetId);
    if (it == presets.end()) return AnimationMode::NONE;
    return it->second.level == AnimationPresetLevel::CHAR ? AnimationMode::CHAR : AnimationMode::WORD;
}

} // namespace

FramePlan planFrame(double elapsedSec, const AnimationTimeline& timeline, int charCount,
                    const AnimationPresetMap& presets) {
    const double elapsed = std::max(0.0, std::min(elapsedSec, timeline.textDuration));

    const double inDur = timeline.inPresetId.has_value() ? timeline.inDuration : 0.0;
    const double outDur = timeline.outPresetId.has_value() ? timeline.outDuration : 0.0;
    const double outStart = std::max(inDur, timeline.textDuration - outDur);

    FramePlan plan;
    double phaseStartSec = 0.0;
    double phaseDur = 0.0;
    if (elapsed < inDur) {
        plan.phase = AnimationPhase::IN;
        plan.presetId = timeline.inPresetId;
        phaseStartSec = 0.0;
        phaseDur = inDur;
    } else if (outDur > 0.0 && elapsed >= outStart) {
        plan.phase = AnimationPhase::OUT;
        plan.presetId = timeline.outPresetId;
        phaseStartSec = outStart;
        phaseDur = outDur;
    } else {
        plan.phase = AnimationPhase::LOOP;
        plan.presetId = timeline.loopPresetId;
        phaseStartSec = inDur;
        phaseDur = timeline.loopDuration;
    }

    const AnimationMode mode = presetMode(plan.presetId, presets);
    if (mode == AnimationMode::NONE) {
        plan.presetId.reset();
        plan.mode = AnimationMode::NONE;
        return plan;
    }

    const bool isLoop = plan.phase == AnimationPhase::LOOP;
    if (mode == AnimationMode::WORD) {
        const double d = phaseDur > 0.0 ? phaseDur : 1.0;
        double wp;
        if (isLoop) {
            wp = std::fmod(elapsed - phaseStartSec, d) / d;
            if (wp < 0.0) wp += 1.0;
        } else {
            wp = std::max(0.0, std::min(1.0, (elapsed - phaseStartSec) / d));
        }
        plan.mode = AnimationMode::WORD;
        plan.wordProgress = wp;
        return plan;
    }

    const CharStagger cs = computeCharStagger(phaseDur, charCount, isLoop);
    plan.mode = AnimationMode::CHAR;
    plan.charTiming = CharTiming{phaseStartSec, cs.perCharDuration, cs.stagger, isLoop};
    return plan;
}

double charProgressAt(double elapsedSec, const CharTiming& timing, int charIndex) {
    const double e = elapsedSec - timing.phaseStartSec - charIndex * timing.stagger;
    const double d = timing.perCharDuration > 0.0 ? timing.perCharDuration : 1.0;
    if (timing.isLoop) {
        double m = std::fmod(e, d);
        m = std::fmod(m + d, d);
        return m / d;
    }
    if (e <= 0.0) return 0.0;
    if (e >= d) return 1.0;
    return e / d;
}

std::optional<TextClipAnimationFrame> buildAnimationFrame(
    const FramePlan& plan, const AnimationPreset& preset, double elapsedSec) {
    const PresetKeyframeSet& ks = preset.keyframes;

    AnimationTransformFlags flags;
    flags.pivotX = ks.pivotX;
    flags.pivotY = ks.pivotY;
    flags.txInRotatedFrame = ks.txInRotatedFrame;
    flags.rotXFirst = ks.rotXFirst;
    flags.txScaled = ks.txScaled;

    const ActiveRange range = plan.phase != AnimationPhase::LOOP
        ? presetActiveRange(ks)
        : ActiveRange{0.0, 1.0};

    if (plan.mode == AnimationMode::CHAR) {
        const CharTiming timing = plan.charTiming;
        TextClipAnimationFrame frame;
        frame.mode = AnimationMode::CHAR;
        frame.flags = flags;
        frame.getCharProps = [ks, range, timing, elapsedSec](int charIndex) {
            const double p = range.start + charProgressAt(elapsedSec, timing, charIndex) * (range.end - range.start);
            return evalKeyframes(ks, p);
        };
        return frame;
    }

    if (plan.mode == AnimationMode::WORD) {
        const double progress = range.start + plan.wordProgress * (range.end - range.start);
        TextClipAnimationFrame frame;
        frame.mode = AnimationMode::WORD;
        frame.flags = flags;
        frame.props = evalKeyframes(ks, progress);
        frame.clipPolygon = evalPolygon(ks.polyTrack, progress);
        frame.scaleAnimated = !ks.tracks[(size_t)AnimProp::sx].empty() || !ks.tracks[(size_t)AnimProp::sy].empty();
        return frame;
    }

    return std::nullopt;
}

} // namespace text
} // namespace openshot
