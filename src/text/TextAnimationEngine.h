#pragma once

// Text-animation engine + preset types. Evaluates CSS-derived keyframe presets at
// an arbitrary progress (linear interpolation between stops with per-segment easing)
// and resolves a clip's in -> loop -> out timeline to a single per-frame plan, driven
// by deterministic clip-relative time (seconds since clip start). Ported from
// text-animation-engine.ts + text-animation-preset.types.ts.

#include <array>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace openshot {
namespace text {

// Default duration (seconds) for a text animation when none is specified.
constexpr double DEFAULT_TEXT_ANIMATION_DURATION = 1.5;

// Reference font size the keyframe ratios are authored against (documented; the stored
// track values are already css_px / REF_FONT_SIZE and are multiplied by the actual fontSize
// at draw time by the transforms).
constexpr double REF_FONT_SIZE = 15.0;

// ── Animatable properties ────────────────────────────────────────────────────

enum class AnimProp {
    opacity, tx, ty, tz, sx, sy, rotate, rotateX, rotateY, blur, brightness, contrast,
    skewX, skewY, letterSpacing, clipT, clipR, clipB, clipL, clipGT, clipGB, perspective,
    COUNT
};
constexpr size_t kAnimPropCount = static_cast<size_t>(AnimProp::COUNT);

// Fully-resolved animatable properties for one frame (identity-filled).
struct ResolvedAnimProps {
    std::array<double, kAnimPropCount> v{};
    double  operator[](AnimProp p) const { return v[static_cast<size_t>(p)]; }
    double& operator[](AnimProp p)       { return v[static_cast<size_t>(p)]; }

    // Named accessors for readability in the transform code.
    double opacity()   const { return v[(size_t)AnimProp::opacity]; }
    double tx()        const { return v[(size_t)AnimProp::tx]; }
    double ty()        const { return v[(size_t)AnimProp::ty]; }
    double tz()        const { return v[(size_t)AnimProp::tz]; }
    double sx()        const { return v[(size_t)AnimProp::sx]; }
    double sy()        const { return v[(size_t)AnimProp::sy]; }
    double rotate()    const { return v[(size_t)AnimProp::rotate]; }
    double rotateX()   const { return v[(size_t)AnimProp::rotateX]; }
    double rotateY()   const { return v[(size_t)AnimProp::rotateY]; }
    double blur()      const { return v[(size_t)AnimProp::blur]; }
    double skewX()     const { return v[(size_t)AnimProp::skewX]; }
    double skewY()     const { return v[(size_t)AnimProp::skewY]; }
    double letterSpacing() const { return v[(size_t)AnimProp::letterSpacing]; }
    double clipT()     const { return v[(size_t)AnimProp::clipT]; }
    double clipR()     const { return v[(size_t)AnimProp::clipR]; }
    double clipB()     const { return v[(size_t)AnimProp::clipB]; }
    double clipL()     const { return v[(size_t)AnimProp::clipL]; }
    double clipGT()    const { return v[(size_t)AnimProp::clipGT]; }
    double clipGB()    const { return v[(size_t)AnimProp::clipGB]; }
    double perspective() const { return v[(size_t)AnimProp::perspective]; }
};

// Identity values for all animatable properties (no visual change).
const ResolvedAnimProps& identityProps();

// ── Preset keyframe definition ───────────────────────────────────────────────

enum class InterpolationType { LINEAR, CONSTANT, BEZIER };

struct CubicBezier { double x1 = 0, y1 = 0, x2 = 0, y2 = 0; };

struct PresetInterpolation {
    InterpolationType type = InterpolationType::LINEAR;
    CubicBezier params;          // valid when type == BEZIER
};

struct PresetKeyframe {
    double pct = 0.0;            // timeline position 0..100
    double value = 0.0;
    std::optional<PresetInterpolation> interpolation;  // to the next keyframe; omitted = LINEAR
};

struct PresetPolygonKeyframe {
    double pct = 0.0;
    std::vector<std::pair<double, double>> points;     // fractions of the text box
};

struct PresetKeyframeSet {
    double pivotX = 0.0;
    double pivotY = 0.0;
    bool txInRotatedFrame = false;
    bool rotXFirst = false;
    bool txScaled = false;
    std::optional<CubicBezier> easing;
    std::vector<PresetPolygonKeyframe> polyTrack;
    std::array<std::vector<PresetKeyframe>, kAnimPropCount> tracks;
};

enum class AnimationPresetLevel { CHAR, BOX };

struct AnimationPreset {
    AnimationPresetLevel level = AnimationPresetLevel::BOX;
    PresetKeyframeSet keyframes;
};

// The set of presets resolvable by id (provided by the host / payload).
using AnimationPresetMap = std::map<std::string, AnimationPreset>;

// ── Stored animation slots on a clip ─────────────────────────────────────────

struct TextAnimations {
    std::optional<std::string> inAnimationId;
    std::optional<double>      inAnimationDuration;
    std::optional<std::string> outAnimationId;
    std::optional<double>      outAnimationDuration;
    std::optional<std::string> loopAnimationId;
    std::optional<double>      loopAnimationDuration;

    bool hasActive() const {
        return inAnimationId.has_value() || loopAnimationId.has_value() || outAnimationId.has_value();
    }
};

// ── Engine functions ─────────────────────────────────────────────────────────

double applyCubicBezier(double x1, double y1, double x2, double y2, double t);

ResolvedAnimProps evalKeyframes(const PresetKeyframeSet& preset, double progress);

// Returns the interpolated polygon, or nullopt when not active at this progress.
std::optional<std::vector<std::pair<double, double>>> evalPolygon(
    const std::vector<PresetPolygonKeyframe>& polyTrack, double progress);

struct ActiveRange { double start; double end; };
ActiveRange presetActiveRange(const PresetKeyframeSet& preset);

struct CharStagger { double perCharDuration; double stagger; };
CharStagger computeCharStagger(double duration, int n, bool isLoop);

// ── Timeline planning ────────────────────────────────────────────────────────

enum class AnimationPhase { IN, LOOP, OUT };

struct AnimationTimeline {
    std::optional<std::string> inPresetId;
    double inDuration = 0.0;
    std::optional<std::string> loopPresetId;
    double loopDuration = 0.0;
    std::optional<std::string> outPresetId;
    double outDuration = 0.0;
    double textDuration = 0.0;   // clip's visible duration in seconds
};

std::optional<AnimationTimeline> buildAnimationTimeline(const TextAnimations& anims, double textDurationSec);

struct CharTiming {
    double phaseStartSec = 0.0;
    double perCharDuration = 0.0;
    double stagger = 0.0;
    bool isLoop = false;
};

enum class AnimationMode { NONE, WORD, CHAR };

struct FramePlan {
    AnimationPhase phase = AnimationPhase::LOOP;
    std::optional<std::string> presetId;   // nullopt -> draw resting (static) text
    AnimationMode mode = AnimationMode::NONE;
    double wordProgress = 0.0;             // valid when mode == WORD
    CharTiming charTiming;                 // valid when mode == CHAR
};

FramePlan planFrame(double elapsedSec, const AnimationTimeline& timeline, int charCount,
                    const AnimationPresetMap& presets);

double charProgressAt(double elapsedSec, const CharTiming& timing, int charIndex);

// ── Per-frame animation input for the renderer ───────────────────────────────

struct AnimationTransformFlags {
    double pivotX = 0.0;
    double pivotY = 0.0;
    bool txInRotatedFrame = false;
    bool rotXFirst = false;
    bool txScaled = false;
};

struct TextClipAnimationFrame {
    AnimationMode mode = AnimationMode::NONE;
    AnimationTransformFlags flags;

    // WORD mode:
    ResolvedAnimProps props;
    std::optional<std::vector<std::pair<double, double>>> clipPolygon;
    bool scaleAnimated = false;

    // CHAR mode: resolve evaluated keyframes for a character by its draw index.
    std::function<ResolvedAnimProps(int)> getCharProps;

    // Static 3D tilt {rotateX, rotateY} (degrees) applied to the whole block. In WORD mode the
    // tilt is folded into `props`; in CHAR mode there is no block-level transform, so it is
    // carried here and the char-animated glyphs are baked flat then tilted as one unit.
    std::optional<std::pair<double, double>> static3D;
};

std::optional<TextClipAnimationFrame> buildAnimationFrame(
    const FramePlan& plan, const AnimationPreset& preset, double elapsedSec);

} // namespace text
} // namespace openshot
