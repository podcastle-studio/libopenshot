#include "TextAnimationEngine.h"

#include <algorithm>
#include <cmath>
#include <numeric>

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

// ── Stagger order ────────────────────────────────────────────────────────────

double SplitMix32::next() {
    // All operations are unsigned 32-bit; the multiplies WRAP (JS Math.imul).
    auto mul32 = [](std::uint32_t a, std::uint32_t b) -> std::uint32_t {
        return static_cast<std::uint32_t>(a * b);
    };
    state_ = static_cast<std::uint32_t>(state_ + 0x6d2b79f5u);
    std::uint32_t e = state_;
    e = mul32(e ^ (e >> 15), e | 1u);
    e ^= static_cast<std::uint32_t>(e + mul32(e ^ (e >> 7), e | 61u));
    return static_cast<double>(e ^ (e >> 14)) / 4294967296.0;
}

std::vector<int> staggerOrderIndices(int n, StaggerFrom from, std::uint32_t seed) {
    if (n <= 0) return {};
    std::vector<int> out(static_cast<size_t>(n), 0);
    switch (from) {
        case StaggerFrom::FIRST:
            for (int i = 0; i < n; ++i) out[i] = i;
            break;
        case StaggerFrom::LAST:
            for (int i = 0; i < n; ++i) out[i] = n - i - 1;
            break;
        case StaggerFrom::CENTER:
            // Middle unit first, spreading outwards. Repeats indices on purpose (units start in
            // pairs), so the highest step is about n/2. At n == 2 both collapse to 0.
            for (int i = 0; i < n; ++i)
                out[i] = static_cast<int>(std::floor(std::abs(i - (n - 1) / 2.0)));
            break;
        case StaggerFrom::EDGES:
            // Both ends first, converging on the middle. Also repeats indices on purpose.
            for (int i = 0; i < n; ++i) out[i] = std::min(i, n - i - 1);
            break;
        case StaggerFrom::RANDOM: {
            // Fisher-Yates yields unit-per-step; what's needed is step-per-unit, so invert it.
            std::vector<int> permutation(static_cast<size_t>(n));
            std::iota(permutation.begin(), permutation.end(), 0);
            SplitMix32 rng(seed);
            for (int i = n - 1; i > 0; --i) {
                const int j = static_cast<int>(std::floor(rng.next() * (i + 1)));
                std::swap(permutation[i], permutation[std::min(std::max(j, 0), i)]);
            }
            for (int step = 0; step < n; ++step) out[permutation[step]] = step;
            break;
        }
    }
    return out;
}

UnitStagger computeUnitStagger(double duration, int unitCount, bool isLoop,
                               double fraction, int steps) {
    // Checked FIRST: a single unit has no stagger to make room for, so it gets the whole slot
    // whatever the fraction says.
    if (unitCount <= 1) return {duration, 0.0};
    // A loop already gives every unit the full cycle and only offsets them, so the fraction has
    // nothing to divide up.
    if (isLoop) return {duration, std::min(duration * 0.15, duration / unitCount)};

    const double perUnitDuration = duration * std::max(0.0, std::min(1.0, fraction));
    const double stagger = steps > 0 ? (duration - perUnitDuration) / steps : 0.0;
    return {perUnitDuration, stagger};
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

const AnimationPreset* findPreset(const std::optional<std::string>& presetId,
                                  const AnimationPresetMap& presets) {
    if (!presetId.has_value()) return nullptr;
    const auto it = presets.find(*presetId);
    return it == presets.end() ? nullptr : &it->second;
}

// level `box` -> block mode; char / word / line -> unit mode with that granularity.
bool unitGranularityForLevel(AnimationPresetLevel level, UnitGranularity& out) {
    switch (level) {
        case AnimationPresetLevel::CHAR: out = UnitGranularity::CHAR; return true;
        case AnimationPresetLevel::WORD: out = UnitGranularity::WORD; return true;
        case AnimationPresetLevel::LINE: out = UnitGranularity::LINE; return true;
        case AnimationPresetLevel::BOX:  return false;
    }
    return false;
}

} // namespace

FramePlan planFrame(double elapsedSec, const AnimationTimeline& timeline, const UnitCounts& counts,
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

    const AnimationPreset* preset = findPreset(plan.presetId, presets);
    UnitGranularity granularity = UnitGranularity::CHAR;
    if (!preset) {
        plan.presetId.reset();
        plan.mode = AnimationMode::NONE;
        return plan;
    }

    const bool isLoop = plan.phase == AnimationPhase::LOOP;
    if (!unitGranularityForLevel(preset->level, granularity)) {
        const double d = phaseDur > 0.0 ? phaseDur : 1.0;
        double bp;
        if (isLoop) {
            bp = std::fmod(elapsed - phaseStartSec, d) / d;
            if (bp < 0.0) bp += 1.0;
        } else {
            bp = std::max(0.0, std::min(1.0, (elapsed - phaseStartSec) / d));
        }
        plan.mode = AnimationMode::BLOCK;
        plan.blockProgress = bp;
        return plan;
    }

    const PresetKeyframeSet& ks = preset->keyframes;
    int unitCount = counts.forGranularity(granularity);
    std::vector<int> order;
    int steps = unitCount - 1;

    if (ks.staggerFrom.has_value()) {
        // A NAMED order is looked up by ordinal in the DRAWN index space (see UnitCounts).
        unitCount = granularity == UnitGranularity::CHAR ? counts.charsDrawn
                                                         : counts.forGranularity(granularity);
        order = staggerOrderIndices(unitCount, *ks.staggerFrom, ks.staggerSeed);
        steps = order.empty() ? 0 : *std::max_element(order.begin(), order.end());
    }

    const UnitStagger us = computeUnitStagger(phaseDur, unitCount, isLoop,
                                              ks.unitFraction.value_or(DEFAULT_UNIT_FRACTION), steps);
    plan.mode = AnimationMode::UNIT;
    plan.granularity = granularity;
    plan.unitTiming = UnitTiming{phaseStartSec, us.perUnitDuration, us.stagger, isLoop, std::move(order)};
    return plan;
}

double unitProgressAt(double elapsedSec, const UnitTiming& timing, int unitIndex, int ordinal) {
    int step = unitIndex;
    if (!timing.order.empty()) {
        step = (ordinal >= 0 && ordinal < static_cast<int>(timing.order.size()))
            ? timing.order[static_cast<size_t>(ordinal)] : 0;
    }
    const double e = elapsedSec - timing.phaseStartSec - step * timing.stagger;
    const double d = timing.perUnitDuration > 0.0 ? timing.perUnitDuration : 1.0;
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

    if (plan.mode == AnimationMode::UNIT) {
        const UnitTiming timing = plan.unitTiming;
        TextClipAnimationFrame frame;
        frame.mode = AnimationMode::UNIT;
        frame.flags = flags;
        frame.granularity = plan.granularity;
        frame.getUnitProps = [ks, range, timing, elapsedSec](int unitIndex, int ordinal) {
            const double p = range.start
                + unitProgressAt(elapsedSec, timing, unitIndex, ordinal) * (range.end - range.start);
            return evalKeyframes(ks, p);
        };
        return frame;
    }

    if (plan.mode == AnimationMode::BLOCK) {
        const double progress = range.start + plan.blockProgress * (range.end - range.start);
        TextClipAnimationFrame frame;
        frame.mode = AnimationMode::BLOCK;
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
