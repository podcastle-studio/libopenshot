#pragma once

// Animated number counters ("odometer" count-ups) for text clips.
//
// A C++ port of the design team's TypeScript counter engine (async-counters-handoff-v4,
// src/engine: types.ts, parse.ts, easing.ts, track.ts, styles.ts, the counter parts of
// layer.ts, presets.ts). The engine is PURE: no Skia, no Qt, no clock. It turns
// (text, CounterFx, clip-local time) into a list of glyphs to draw, and receives font
// measurements through `Measurer`. TextClipReader implements Measurer on the Skia text
// engine and paints the glyphs through the normal text paint passes (see CounterRender.h).
//
// Portability is the point: the TS engine is deterministic (hash-based "random", no
// Math.random), and every formula here follows the TS line for line so the same payload
// gives the same digits on the web preview and in the export. tests/golden/scenarios/
// Counter*.cpp holds fixtures generated from the TS engine and checks this port against
// them to 1e-6. When the TS engine changes, port the change here and regenerate them
// (tools/counter-fixtures/).
//
// Out of scope (not ported): motion.ts ("Animation 2"), LayerAnim enter/exit,
// nativeAnimations. A text clip's own in/loop/out textAnimations compose on top instead.
//
// Strings are UTF-8. Text indices inside the engine are CODEPOINT indices (the TS uses
// UTF-16 units; the two agree on everything the parser looks at, which is all BMP).

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace openshot {
namespace text {
namespace counter {

// ── Data model (types.ts) ────────────────────────────────────────────────────

/// cubic-bezier(x1, y1, x2, y2), y inside 0..1 so the value never runs backwards.
struct Bezier {
    double x1 = 0, y1 = 0, x2 = 1, y2 = 1;
};

enum class DigitStyle { None, Roll, Drum, Slide, Fade, Blur, Pop, Zoom, Flip };
enum class CounterMode { Count, Slot, Scramble };
enum class Direction { Auto, Up, Down, Alternate };
enum class Order { Ltr, Rtl, Random };

struct CounterFx {
    std::string preset = "smooth-roll";
    std::vector<int> targets{0};
    std::map<int, double> from;
    double range[2] = {0.05, 0.75};
    Bezier easing{0.16, 1, 0.3, 1};
    CounterMode mode = CounterMode::Count;
    DigitStyle style = DigitStyle::Roll;
    Direction direction = Direction::Auto;
    double swap = 0.36;
    bool soft = false;
    double bounce = 0;
    double maxSpeed = 12;
    double cascade = 0.02;
    double distance = 1;
    bool clip = false;
    double stepFps = 0;
    double shutter = 180;
    double landPop = 0;
    double grow = 0;
    double spins = 2;
    Order order = Order::Ltr;
    double rate = 18;
    bool compactScale = true;
    bool tabular = true;
};

/// DEFAULT_FX of presets.ts.
CounterFx defaultFx();

struct Preset {
    std::string id;
    std::string group;
    std::function<void(CounterFx&)> apply;   ///< lays the preset's partial fx over a CounterFx
};

const std::vector<Preset>& presets();
const Preset* presetById(const std::string& id);

/// applyPreset(current, preset): the LOOK keys reset to DEFAULT_FX, then the preset's own fx,
/// then preset = id. Start value, range and targets are kept from `current` (or the defaults).
CounterFx applyPreset(const CounterFx* current, const Preset& preset);

/// The 20 ready-made templates of presets.ts (text, start value, preset, fx/style patches).
struct Template {
    std::string id;
    std::string text;
    std::optional<double> from;
    std::string preset;
    std::function<void(CounterFx&)> fx;      ///< may be empty
    std::optional<std::string> currency;
    std::optional<std::string> fontFamily;   ///< the only style patch the templates use
};
const std::vector<Template>& templates();
/// layerFromTemplate's counter: applyPreset(null, preset) + template fx + from.
CounterFx fxFromTemplate(const Template& tpl);

/// Easing curves of easing.ts, by name ("linear", "easeOut", "smoothOut", ...).
std::optional<Bezier> easingByName(const std::string& name);

// ── Easing (easing.ts) ───────────────────────────────────────────────────────

double ease(const Bezier& b, double x);
double easeInverse(const Bezier& b, double y);
inline double clamp(double v, double lo = 0.0, double hi = 1.0) { return v < lo ? lo : v > hi ? hi : v; }
inline double lerp(double a, double b, double t) { return a + (b - a) * t; }
double smoothstep(double x);

// ── Numbers in text (parse.ts) ───────────────────────────────────────────────

struct CompactInfo {
    int tier = 0;
    std::vector<std::string> ladder;   ///< ladder[i] = suffix shown for 1000^i
};

struct TimeInfo {
    int fields = 2;          ///< 2 = m:ss, 3 = h:mm:ss
    int lead = 1;            ///< digits of the first field
    int frac = 0;            ///< fraction digits
    std::string fracSep = ".";
};

struct NumberToken {
    int index = 0;
    bool isTime = false;
    /// [start, end) codepoint indices of the number, K/M and ordinal suffix included, sign excluded.
    int start = 0;
    int end = 0;
    int signAt = -1;
    char sign = '\0';        ///< '\0', '+' or '-'
    std::string minusChar = "\xE2\x88\x92";   // U+2212
    double value = 0;
    std::optional<std::string> source;
    bool precisionLost = false;
    int decimals = 0;
    std::string decimalSep = ".";
    std::string groupSep;
    int minInt = 1;
    std::optional<CompactInfo> compact;
    bool ordinal = false;
    std::optional<TimeInfo> time;
};

/// UTF-8 → codepoints, and back.
std::u32string toCodepoints(const std::string& utf8);
std::string fromCodepoints(const std::u32string& cps, size_t from = 0, size_t to = std::u32string::npos);

std::vector<NumberToken> parseText(const std::string& text);
std::string ordinalSuffix(double n);
int compactTier(const NumberToken& tok, double magnitude);
double compactBoundary(const NumberToken& tok, int tier);
std::string formatBody(const NumberToken& tok, double value, bool scaled = true);
std::string rewriteToken(const std::string& text, const NumberToken& tok, double value);

// JS number semantics used by the formatters (exposed for tests).
double jsRound(double x);
std::string jsIntegerString(double integral);

// ── Tracks (track.ts) ────────────────────────────────────────────────────────

struct SlotState {
    std::string out;
    std::string in;
    double p = 0;
    double presence = 0;
    double pos = 0;
    int dir = 1;
};

struct TrackSlot {
    std::string id;
    bool wheel = false;   ///< 'wheel' or 'char'
    std::function<SlotState(double)> sample;
};

struct Track {
    NumberToken token;
    bool isStatic = false;
    std::vector<std::shared_ptr<TrackSlot>> cells;
    std::vector<std::shared_ptr<TrackSlot>> signs;
    std::function<double(double)> valueAt;
    double settlesAt = 0;
};

Track buildTrack(const NumberToken& tok, double fromValue, const CounterFx& fx, double windowSeconds);

// ── Glyphs (styles.ts, layer.ts) ─────────────────────────────────────────────

/// One piece of text to draw. Coordinates are local to the layer anchor, y grows downwards.
struct Glyph {
    std::string text;
    double x = 0;        ///< centre of the cell (digits) or left edge (runs)
    double y = 0;        ///< baseline of the line
    double w = 0;        ///< cell width, used for the clip rectangle
    bool run = false;
    double dy = 0;
    double sx = 1;
    double sy = 1;
    double pivot = 0;    ///< pivot of the scale relative to the baseline (negative = above)
    double opacity = 1;
    double blur = 0;     ///< gaussian blur radius, px
    bool hasClip = false;
    double clip0 = 0;    ///< vertical window relative to the baseline
    double clip1 = 0;
    double feather = 0;  ///< soft edge of that window, px; 0 = hard cut
    /// Index of the counter number this glyph belongs to, or -1 for plain text.
    int track = -1;
    /// Row (line) of the compiled layer the glyph is on.
    int line = 0;
};

struct CellMetrics {
    double size = 0;
    double digit = 0;
};

std::vector<Glyph> digitParts(const CounterFx& fx, const SlotState& s, const CellMetrics& m);

struct Bounds { double x = 0, y = 0, w = 0, h = 0; };
struct LayerXf { double dx = 0, dy = 0, scale = 1, px = 0, py = 0, opacity = 1, blur = 0; };

struct LayerFrame {
    std::vector<Glyph> glyphs;
    Bounds bounds;
    LayerXf xf;
    std::vector<double> pose;
    /// Width of every line at this moment (engine units), same order as the compiled lines.
    std::vector<double> lineWidths;
};

double poseDistance(const LayerFrame& a, const LayerFrame& b);

// ── Layer (layer.ts, counter parts) ──────────────────────────────────────────

/// Font measurement for one fixed text style. Implemented on Skia by the reader, by a stub in tests.
class Measurer {
public:
    virtual ~Measurer() = default;
    /// Advance width of a text run (no letter spacing).
    virtual double width(const std::string& text) const = 0;
    virtual double ascent() const = 0;
    virtual double descent() const = 0;
    /// Height of a digit above the baseline.
    virtual double digit() const = 0;
};

enum class Align { Left, Center, Right };

struct LayerStyle {
    double fontSize = 180;
    double letterSpacing = 0;   ///< em
    double lineHeight = 1.1;    ///< multiple of the font size
    Align align = Align::Center;
};

/// A line of the laid-out text as a [start, end) codepoint range of the layer text.
struct LineRange { int start = 0; int end = 0; };

struct LayerInput {
    std::string text;
    LayerStyle style;
    double duration = 4;           ///< clip length, seconds
    double opacity = 1;
    std::optional<CounterFx> counter;
    /// Lines of the layout. Empty = split the text on '\n' (what the TS engine does). The reader
    /// passes the wrapped lines of its own layout, so wrapping follows the static text.
    std::vector<LineRange> lines;
    /// Splits a run into the letters placed one by one under letter spacing. Empty = codepoints
    /// (the TS `for (const c of piece)`); the reader passes its emoji-aware cluster splitter.
    std::function<std::vector<std::string>(const std::string&)> letters;
};

struct CompiledLayer;

/// compileLayer(): everything that does not depend on time. Never throws on odd input; a number
/// whose digit steps cannot be represented is drawn as ordinary text (Track::isStatic).
std::shared_ptr<const CompiledLayer> compileLayer(const LayerInput& input, const Measurer& measure);

/// evaluateLayer(): the frame at clip-local time t (seconds since the clip start). frameTime is
/// the moment of the video frame a motion-blur sample belongs to (digits that swap instantly
/// are taken there).
LayerFrame evaluateLayer(const CompiledLayer& c, double t, std::optional<double> frameTime = std::nullopt);

/// Accessors on a compiled layer.
size_t trackCount(const CompiledLayer& c);
double countStart(const CompiledLayer& c);
double countEnd(const CompiledLayer& c);
size_t lineCount(const CompiledLayer& c);
/// Last moment (clip-local seconds) at which anything still moves: the slowest track settling,
/// the grow and the land pop. +inf when it never settles (scramble with rate 0).
double settleTime(const CompiledLayer& c);
const LayerInput& layerInput(const CompiledLayer& c);

// ── Motion blur sampling (render/draw.ts) ────────────────────────────────────

/// Number of shutter samples draw.ts takes for a travel (device px) within the exposure:
/// 1 when nothing moves more than 0.4 px, else one per 2 px, at least 4, at most maxSamples,
/// rounded up to the next count its 4x4x4 accumulation cascade can split.
int motionBlurSamples(double travelPx, int maxSamples = 48);

/// Sample times (clip-local seconds) of one exposure centred on `time`, clamped to the clip.
std::vector<double> shutterSampleTimes(double time, double shutterDeg, double fps, int samples, double duration);

} // namespace counter
} // namespace text
} // namespace openshot
