#pragma once

// Animated number counters on a text clip: glue between the pure counter engine
// (src/text/counter) and the Skia text renderer.
//
// The clip's `value` is the FINAL text. It is laid out by the ordinary layout (wrapping, align,
// textTransform) and every animated number's span is replaced, line by line, by the engine's
// digit cells, whose width changes over time; the alignment decides which side of each line
// stays put. The glyphs of a frame become AnimatedUnitItems and are painted by the unit-mode
// passes, so a counter gets the same fill / gradient / stroke / shadow / glow / background /
// blur as normal text, and composes with block- and unit-mode textAnimations (a whole number
// is ONE unit) and with the static 3D tilt.
//
// Motion blur (CounterFx::shutter): the frame is evaluated at several moments inside the
// exposure, centred on the frame time, as draw.ts does -- as many samples as the motion needs
// (poseDistance, cap 48) -- each drawn at 8 bits and averaged in a kRGBA_F16 buffer in device
// space. Glyphs identical across the exposure are drawn once, on top, unblurred. The glow is
// computed once, from the frame-time glyphs.

#include "TextAnimationEngine.h"
#include "TextClipTypes.h"
#include "counter/CounterEngine.h"

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace openshot {
namespace subtitle { class SkiaRenderer; }
namespace text {

struct CounterRenderPlan {
    std::shared_ptr<const counter::CompiledLayer> compiled;
    counter::CounterFx fx;
    double duration = 0.0;          ///< clip visible duration, seconds (range is a fraction of it)
    double fps = 30.0;
    double anchorX = 0.0;           ///< alignment anchor of every line, content-box x
    double firstBaseline = 0.0;     ///< first baseline below the content-box top
    double lineStep = 0.0;
    std::vector<double> lineAscent; ///< per row (layout line)
    std::vector<double> lineDescent;
    double fontAscent = 0.0;        ///< font box, for the extents of a moving digit
    double fontDescent = 0.0;
    TextClipPaintStyle layoutPaint; ///< reference-size paint the layout measured with
    double sizeScale = 1.0;         ///< actual / reference size

    /// Stagger units with every animated number counted as ONE unit (for planFrame).
    UnitCounts unitCounts;
    /// Per row, the elements a unit-mode animation groups: text letters (in order) and numbers.
    struct Element {
        bool number = false;
        int track = -1;             ///< number: engine track index
        bool space = false;         ///< letter: whitespace (never a unit of its own)
        int charIndex = -1;         ///< CHAR granularity: slot index (spaces counted)
        int charOrdinal = -1;       ///<                   drawn index
        int word = -1;              ///< WORD granularity ordinal (-1 for spaces)
        int line = -1;              ///< LINE granularity ordinal
    };
    std::vector<std::vector<Element>> rows;

    /// Worst case over the clip of how far the drawn glyphs reach past the content box, px.
    double overLeft = 0.0, overRight = 0.0, overTop = 0.0, overBottom = 0.0;
    /// Clip-local time after which nothing moves (the resting frame is the frame at `duration`).
    double settle = 0.0;

    /// Advance of one letter at the actual size (measured at the layout paint, then scaled, as the
    /// layout's own advances are). Filled while planning; misses are measured on demand.
    mutable std::unordered_map<std::string, double> letterWidths;
};

/// Build the plan for a text clip with a `counter`. nullopt (with a reason in `warnings`) when the
/// counter cannot apply -- curved text, no number in the text, a number broken across lines --
/// and the clip then renders as plain text.
std::optional<CounterRenderPlan> buildCounterPlan(
    const std::string& transformedText,
    const counter::CounterFx& fx,
    const TextClipLayout& layout,
    const TextClipPaintStyle& paint,
    const TextClipPaintStyle& layoutPaint,
    double durationSec, double fps,
    subtitle::SkiaRenderer* measureRenderer,
    std::vector<std::string>& warnings);

/// Draw the counter clip at clip-local time `t` (seconds), the content box's top-left at
/// (originX, originY). `animation` is the clip's text animation frame, as for renderTextFrame:
/// block mode transforms the whole counter, unit mode staggers its units, static3D tilts it.
void renderCounterFrame(
    const CounterRenderPlan& plan, double t,
    const TextClipLayout& layout,
    const TextClipPaintStyle& paint,
    const std::optional<TextClipBackgroundStyle>& background,
    double originX, double originY, double scale,
    const std::optional<TextClipAnimationFrame>& animation,
    subtitle::SkiaRenderer* renderer);

/// The final text of each line as the engine lays it out at the end of the clip (for tests).
std::vector<std::string> counterFinalLines(const CounterRenderPlan& plan);

} // namespace text
} // namespace openshot
