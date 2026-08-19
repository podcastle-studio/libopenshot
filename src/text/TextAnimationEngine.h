#pragma once

// Text-animation engine + preset types. Evaluates CSS-derived keyframe presets at
// an arbitrary progress (linear interpolation between stops with per-segment easing)
// and resolves a clip's in -> loop -> out timeline to a single per-frame plan, driven
// by deterministic clip-relative time (seconds since clip start). Ported from
// text-animation-engine.ts + text-animation-preset.types.ts.

#include <array>
#include <cstdint>
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

// Order the stagger units start in. Absent on a preset = FIRST *and* the legacy (slot) index
// space — see the two-index-space note on UnitCounts below.
enum class StaggerFrom { FIRST, LAST, CENTER, EDGES, RANDOM };

// Default share of a phase slot that one unit animates for (the rest is spent staggering).
constexpr double DEFAULT_UNIT_FRACTION = 0.55;

struct PresetKeyframeSet {
    double pivotX = 0.0;
    double pivotY = 0.0;
    bool txInRotatedFrame = false;
    bool rotXFirst = false;
    bool txScaled = false;
    std::optional<CubicBezier> easing;
    // Stagger order. nullopt = FIRST order AND the legacy slot index space (spaces counted).
    // Naming an order switches char granularity to the drawn index space (spaces excluded).
    std::optional<StaggerFrom> staggerFrom;
    std::uint32_t staggerSeed = 1;              // only consumed by StaggerFrom::RANDOM
    std::optional<double> unitFraction;         // nullopt = DEFAULT_UNIT_FRACTION; ignored by LOOP
    std::vector<PresetPolygonKeyframe> polyTrack;
    std::array<std::vector<PresetKeyframe>, kAnimPropCount> tracks;
};

// `box` animates the whole text box under one transform (block mode); char/word/line group the
// laid-out glyphs into staggered units (unit mode).
enum class AnimationPresetLevel { CHAR, WORD, LINE, BOX };

// Which glyphs share one stagger index and one pivot box in unit mode.
enum class UnitGranularity { CHAR, WORD, LINE };

// Unit tallies over the LAID-OUT lines (post-wrap), the input the timing planner sizes its
// stagger from.
//
// Two index spaces (the easiest thing to get wrong):
//  * slot space  (`chars`, spaces counted)  — used when the preset names NO staggerFrom, so the
//    legacy wave crosses word gaps at the same rate it crosses letters.
//  * drawn space (`charsDrawn`, spaces excluded) — used when a staggerFrom IS named, because
//    `center` must mean the centre of the VISIBLE glyphs.
// For word / line granularity the two spaces coincide (blanks are skipped by construction).
struct UnitCounts {
    int chars = 0;        // laid-out characters, spaces INCLUDED
    int charsDrawn = 0;   // characters that actually draw (spaces excluded)
    int words = 0;        // runs of non-space characters, per line (never crossing a line)
    int lines = 0;        // laid-out lines that contain ink

    int forGranularity(UnitGranularity g) const {
        switch (g) {
            case UnitGranularity::CHAR: return chars;
            case UnitGranularity::WORD: return words;
            case UnitGranularity::LINE: return lines;
        }
        return chars;
    }
};

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

// Stagger order as STEP-PER-UNIT: entry `i` is how many stagger steps after the first unit that
// unit begins. NOT a permutation in general — `center` and `edges` repeat indices on purpose
// (units start in pairs), so their highest step is about n/2, not n-1. Size the stagger step from
// max(indices), never from n-1, or those two orders play roughly twice as fast as they should.
//
//   n=5  first  -> [0,1,2,3,4]     last   -> [4,3,2,1,0]
//        center -> [2,1,0,1,2]     edges  -> [0,1,2,1,0]
std::vector<int> staggerOrderIndices(int n, StaggerFrom from, std::uint32_t seed);

// splitmix32, inlined bit-for-bit from the web preview so the same seed yields the same shuffle in
// every renderer (a platform RNG cannot guarantee that). Returns a double in [0, 1).
class SplitMix32 {
public:
    explicit SplitMix32(std::uint32_t seed) : state_(seed) {}
    double next();
private:
    std::uint32_t state_;
};

struct UnitStagger { double perUnitDuration; double stagger; };

// `steps` = how many stagger steps separate the first unit from the last one to START:
// unitCount-1 for the default order, max(orderIndices) for a named one.
UnitStagger computeUnitStagger(double duration, int unitCount, bool isLoop,
                               double fraction, int steps);

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

struct UnitTiming {
    double phaseStartSec = 0.0;
    double perUnitDuration = 0.0;
    double stagger = 0.0;
    bool isLoop = false;
    // Step-per-unit order, indexed by ORDINAL. Empty = the default order, where the step is the
    // unit's own index (slot position for glyph units).
    std::vector<int> order;
};

// BLOCK = the whole text box animates under one transform (preset level `box`).
// UNIT  = the block's glyphs are grouped and animate with a stagger (level char / word / line).
enum class AnimationMode { NONE, BLOCK, UNIT };

struct FramePlan {
    AnimationPhase phase = AnimationPhase::LOOP;
    std::optional<std::string> presetId;   // nullopt -> draw resting (static) text
    AnimationMode mode = AnimationMode::NONE;
    double blockProgress = 0.0;            // valid when mode == BLOCK
    UnitTiming unitTiming;                 // valid when mode == UNIT
    UnitGranularity granularity = UnitGranularity::CHAR;   // valid when mode == UNIT
};

FramePlan planFrame(double elapsedSec, const AnimationTimeline& timeline, const UnitCounts& counts,
                    const AnimationPresetMap& presets);

// `unitIndex` drives the default order (slot position for glyph units); `ordinal` is the unit's
// position among the units that actually draw and is the index a NAMED order is looked up by.
double unitProgressAt(double elapsedSec, const UnitTiming& timing, int unitIndex, int ordinal);

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
    UnitGranularity granularity = UnitGranularity::CHAR;   // valid when mode == UNIT

    // BLOCK mode:
    ResolvedAnimProps props;
    std::optional<std::vector<std::pair<double, double>>> clipPolygon;
    bool scaleAnimated = false;
    // Force the composited block texture path even when the tilt currently evaluates to 0.
    // Set for clips with KEYFRAMED tilt so the frame where tilt == 0 renders the shadow the same
    // (confined, baked-into-texture) way as neighbouring nonzero-tilt frames — otherwise the render
    // flips to the flat path at exactly-0 tilt and the drop shadow visibly pops larger for a frame.
    bool forceBlockTexture = false;

    // UNIT mode: resolve evaluated keyframes for one stagger unit, addressed by BOTH of its
    // indices — (unitIndex, ordinal). See UnitCounts for why two are needed.
    std::function<ResolvedAnimProps(int unitIndex, int ordinal)> getUnitProps;

    // Static 3D tilt {rotateX, rotateY} (degrees) applied to the whole block. In BLOCK mode the
    // tilt is folded into `props`; in UNIT mode there is no block-level transform, so it is
    // carried here and the unit-animated glyphs are baked flat then tilted as one unit.
    std::optional<std::pair<double, double>> static3D;
};

std::optional<TextClipAnimationFrame> buildAnimationFrame(
    const FramePlan& plan, const AnimationPreset& preset, double elapsedSec);

} // namespace text
} // namespace openshot
