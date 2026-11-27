// Port of styles.ts, the counter parts of layer.ts (compileLayer / evaluateLayer /
// poseDistance) and the sampling arithmetic of render/draw.ts (async-counters-handoff-v4).
//
// Differences from the TS, all on purpose and none visible in the glyph list of a plain layer:
//  - Lines can be given as ranges of the text (the reader's wrapped layout); without them the
//    text is split on '\n' exactly as the TS does.
//  - Animation 2 (motion.ts), nativeAnimations and LayerAnim enter/exit are not ported: the text
//    clip's own textAnimations compose on top instead (see CounterRender).
//  - Glyphs carry the index of the number they belong to (Glyph::track), so the renderer can treat
//    a whole number as one unit of a unit-mode text animation.

#include "CounterEngine.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>

namespace openshot {
namespace text {
namespace counter {

namespace {
constexpr double PI = 3.14159265358979323846;
constexpr double INF = std::numeric_limits<double>::infinity();
}

// ── styles.ts ────────────────────────────────────────────────────────────────

namespace {
struct PartPatch {
    double dy = 0, sx = 1, sy = 1, pivot = 0, opacity = 1, blur = 0;
    bool hasClip = false;
    double clip0 = 0, clip1 = 0, feather = 0;
};
Glyph partOf(const std::string& text, const PartPatch& p) {
    Glyph g;
    g.text = text;
    g.dy = p.dy;
    g.sx = p.sx;
    g.sy = p.sy;
    g.pivot = p.pivot;
    g.opacity = p.opacity;
    g.blur = p.blur;
    g.hasClip = p.hasClip;
    g.clip0 = p.clip0;
    g.clip1 = p.clip1;
    g.feather = p.feather;
    return g;
}
} // namespace

std::vector<Glyph> digitParts(const CounterFx& fx, const SlotState& s, const CellMetrics& m) {
    const double p = s.p;
    const int dir = s.dir;
    std::vector<Glyph> out;
    if (p <= 0 || fx.style == DigitStyle::None) {
        if (!s.out.empty()) out.push_back(partOf(s.out, PartPatch{}));
        return out;
    }
    const double q = 1 - p;
    const double travel = fx.distance * m.size;
    const double centre = -m.digit / 2;
    auto add = [&](const std::string& text, const PartPatch& patch) {
        if (!text.empty() && patch.opacity > 0.004) out.push_back(partOf(text, patch));
    };
    auto P = [] { return PartPatch{}; };

    switch (fx.style) {
    case DigitStyle::Roll:
    case DigitStyle::Drum: {
        const double c0 = fx.clip ? centre - travel / 2 : -m.digit - m.size * 0.02;
        const double c1 = fx.clip ? centre + travel / 2 : m.size * 0.02;
        const double feather = fx.clip ? 0 : m.size * 0.17;
        if (fx.style == DigitStyle::Roll) {
            PartPatch a = P(); a.dy = -dir * p * travel; a.hasClip = true; a.clip0 = c0; a.clip1 = c1; a.feather = feather;
            add(s.out, a);
            PartPatch b = P(); b.dy = dir * q * travel; b.hasClip = true; b.clip0 = c0; b.clip1 = c1; b.feather = feather;
            add(s.in, b);
            break;
        }
        const double radius = travel * 0.62;
        auto turn = [&](double angle, double sign) {
            PartPatch t = P();
            t.dy = sign * radius * std::sin(angle);
            t.sy = std::max(std::cos(angle), 0.02);
            t.pivot = centre;
            t.opacity = std::pow(std::cos(angle), 0.7);
            t.hasClip = true;
            t.clip0 = c0;
            t.clip1 = c1;
            t.feather = feather;
            return t;
        };
        add(s.out, turn((p * PI) / 2, -dir));
        add(s.in, turn((q * PI) / 2, dir));
        break;
    }
    case DigitStyle::Slide: {
        const double d = travel * 0.5;
        const double over = fx.bounce * 0.55 * std::sin(PI * p) * q;
        PartPatch a = P(); a.dy = -dir * p * d; a.opacity = 1 - smoothstep(p / 0.7);
        add(s.out, a);
        PartPatch b = P(); b.dy = dir * (q - over) * d; b.opacity = smoothstep((p - 0.15) / 0.6);
        add(s.in, b);
        break;
    }
    case DigitStyle::Fade: {
        PartPatch a = P(); a.opacity = q;
        add(s.out, a);
        PartPatch b = P(); b.opacity = p;
        add(s.in, b);
        break;
    }
    case DigitStyle::Blur: {
        const double amount = m.size * 0.16;
        const double shift = m.size * 0.07;
        PartPatch a = P(); a.blur = p * amount; a.dy = -dir * p * shift; a.opacity = 1 - smoothstep((p - 0.1) / 0.7);
        add(s.out, a);
        PartPatch b = P(); b.blur = q * amount; b.dy = dir * q * shift; b.opacity = smoothstep((p - 0.2) / 0.7);
        add(s.in, b);
        break;
    }
    case DigitStyle::Pop: {
        const double over = fx.bounce * 0.5 * std::sin(PI * p) * q;
        const double a = 1 - 0.55 * p;
        const double b = lerp(0.45, 1, p) + over;
        PartPatch pa = P(); pa.sx = a; pa.sy = a; pa.pivot = centre; pa.opacity = 1 - smoothstep(p / 0.55);
        add(s.out, pa);
        PartPatch pb = P(); pb.sx = b; pb.sy = b; pb.pivot = centre; pb.opacity = smoothstep(p / 0.5);
        add(s.in, pb);
        break;
    }
    case DigitStyle::Zoom: {
        const double a = 1 + 0.9 * p;
        const double b = lerp(0.5, 1, p);
        PartPatch pa = P(); pa.sx = a; pa.sy = a; pa.pivot = centre; pa.blur = p * m.size * 0.05; pa.opacity = 1 - smoothstep(p / 0.75);
        add(s.out, pa);
        PartPatch pb = P(); pb.sx = b; pb.sy = b; pb.pivot = centre; pb.opacity = smoothstep((p - 0.2) / 0.7);
        add(s.in, pb);
        break;
    }
    case DigitStyle::Flip: {
        const double pad = m.size * 0.22;
        const double top = -m.digit - pad;
        const double bottom = pad;
        const double fold = std::abs(std::cos(p * PI));
        const double shade = 0.5 + 0.5 * fold;
        auto clipped = [&](double a, double b) { PartPatch t = P(); t.hasClip = true; t.clip0 = a; t.clip1 = b; return t; };
        if (p < 0.5) {
            const double edge = centre - (centre - top) * fold;
            add(s.in, clipped(top, edge));
            PartPatch f = clipped(edge, centre); f.sy = std::max(fold, 0.02); f.pivot = centre; f.opacity = shade;
            add(s.out, f);
            add(s.out, clipped(centre, bottom));
        } else {
            const double edge = centre + (bottom - centre) * fold;
            add(s.in, clipped(top, centre));
            PartPatch f = clipped(centre, edge); f.sy = std::max(fold, 0.02); f.pivot = centre; f.opacity = shade;
            add(s.in, f);
            add(s.out, clipped(edge, bottom));
        }
        break;
    }
    case DigitStyle::None:
        break;
    }
    return out;
}

// ── layer.ts ─────────────────────────────────────────────────────────────────

namespace {

struct Seg {
    bool isText = true;
    std::string text;      // text segs
    double width = 0;
    std::shared_ptr<TrackSlot> slot;   // slot segs
    int track = -1;
};

} // namespace

struct CompiledLayer {
    LayerInput layer;
    std::vector<NumberToken> tokens;
    std::vector<Track> tracks;
    std::vector<std::vector<Seg>> lines;
    double countStart = 0;
    double countEnd = 0;
    double ascent = 0, descent = 0, digit = 0;
    double digitWidth = 0;
    mutable std::unordered_map<std::string, double> widths;
    const Measurer* measure = nullptr;   // only used inside compileLayer
    std::unordered_map<std::string, double> charWidths;   // frozen after compile

    double charWidth(const std::string& c) const {
        auto it = charWidths.find(c);
        if (it != charWidths.end()) return it->second;
        auto w = widths.find(c);
        return w != widths.end() ? w->second : 0.0;
    }
};

namespace {
std::vector<int> activeTargets(const std::vector<NumberToken>& tokens, const std::optional<CounterFx>& fx) {
    if (!fx || tokens.empty()) return {};
    std::vector<int> valid;
    for (int i : fx->targets) if (i >= 0 && i < static_cast<int>(tokens.size())) valid.push_back(i);
    return valid.empty() ? std::vector<int>{0} : valid;
}

std::vector<std::string> codepointLetters(const std::string& s) {
    std::vector<std::string> out;
    const std::u32string cps = toCodepoints(s);
    for (size_t i = 0; i < cps.size(); ++i) out.push_back(fromCodepoints(cps, i, i + 1));
    return out;
}
} // namespace

std::shared_ptr<const CompiledLayer> compileLayer(const LayerInput& input, const Measurer& measure) {
    auto out = std::make_shared<CompiledLayer>();
    CompiledLayer& c = *out;
    c.layer = input;
    const LayerStyle& style = input.style;
    const std::optional<CounterFx>& fx = input.counter;
    const std::u32string text = toCodepoints(input.text);
    c.tokens = parseText(input.text);
    const double spacing = style.letterSpacing * style.fontSize;
    auto charWidth = [&](const std::string& ch) {
        auto it = c.charWidths.find(ch);
        if (it != c.charWidths.end()) return it->second;
        const double w = measure.width(ch);
        c.charWidths.emplace(ch, w);
        return w;
    };
    auto letters = input.letters ? input.letters : codepointLetters;

    c.countStart = fx ? clamp(fx->range[0]) * input.duration : 0;
    c.countEnd = fx ? std::max(clamp(fx->range[1]) * input.duration, c.countStart) : 0;

    struct Cut { int from, to; std::vector<std::shared_ptr<TrackSlot>> cells; int track; };
    std::vector<Cut> cuts;
    if (fx) {
        for (int index : activeTargets(c.tokens, fx)) {
            const NumberToken& tok = c.tokens[index];
            auto f = fx->from.find(index);
            Track track = buildTrack(tok, f != fx->from.end() ? f->second : 0.0, *fx, c.countEnd - c.countStart);
            if (track.isStatic) continue;
            c.tracks.push_back(track);
            const int ti = static_cast<int>(c.tracks.size()) - 1;
            if (!track.signs.empty()) {
                if (tok.signAt >= 0) cuts.push_back({tok.signAt, tok.signAt + 1, track.signs, ti});
                else cuts.push_back({tok.start, tok.start, track.signs, ti});
            }
            cuts.push_back({tok.start, tok.end, track.cells, ti});
        }
        std::stable_sort(cuts.begin(), cuts.end(), [](const Cut& a, const Cut& b) {
            return a.from != b.from ? a.from < b.from : a.to < b.to;
        });
    }

    // Line ranges: given by the caller (its wrapped layout), or the TS split on '\n'.
    std::vector<LineRange> ranges = input.lines;
    if (ranges.empty()) {
        int start = 0;
        for (int i = 0; i <= static_cast<int>(text.size()); ++i) {
            if (i == static_cast<int>(text.size()) || text[i] == U'\n') {
                ranges.push_back({start, i});
                start = i + 1;
            }
        }
    }
    c.lines.assign(ranges.size(), {});
    // The line a text position belongs to: the last range starting at or before it.
    auto lineOf = [&](int pos) {
        int li = 0;
        for (size_t k = 0; k < ranges.size(); ++k) if (ranges[k].start <= pos) li = static_cast<int>(k);
        return li;
    };
    const bool perGlyph = spacing != 0;
    auto pushText = [&](int a, int b) {
        // chunk = text[a, b): split along the line ranges; text between ranges is not drawn.
        for (size_t li = 0; li < ranges.size(); ++li) {
            const int s = std::max(a, ranges[li].start);
            const int e = std::min(b, ranges[li].end);
            if (e <= s) continue;
            const std::string piece = fromCodepoints(text, s, e);
            std::vector<Seg>& line = c.lines[li];
            if (perGlyph) {
                for (const std::string& ch : letters(piece)) {
                    Seg seg;
                    seg.text = ch;
                    seg.width = charWidth(ch) + spacing;
                    line.push_back(seg);
                }
                continue;
            }
            Seg seg;
            seg.text = piece;
            seg.width = measure.width(piece);
            line.push_back(seg);
        }
    };
    int at = 0;
    for (const Cut& cut : cuts) {
        if (cut.from > at) pushText(at, cut.from);
        std::vector<Seg>& line = c.lines[lineOf(cut.from)];
        for (const auto& slot : cut.cells) {
            Seg seg;
            seg.isText = false;
            seg.slot = slot;
            seg.track = cut.track;
            line.push_back(seg);
        }
        at = std::max(at, cut.to);
    }
    pushText(at, static_cast<int>(text.size()));

    c.ascent = measure.ascent();
    c.descent = measure.descent();
    c.digit = measure.digit();
    c.digitWidth = 0;
    for (char d = '0'; d <= '9'; ++d) c.digitWidth = std::max(c.digitWidth, charWidth(std::string(1, d)));
    // Every glyph a slot can show, measured now so evaluateLayer never needs the measurer.
    for (const char* extra : {"0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "st", "nd", "rd", "th"}) charWidth(extra);
    for (const auto& line : c.lines) {
        for (const Seg& seg : line) {
            if (seg.isText) continue;
            if (!seg.slot->wheel) charWidth(seg.slot->sample(0).out);
        }
    }
    return out;
}

LayerFrame evaluateLayer(const CompiledLayer& c, double t, std::optional<double> frameTimeOpt) {
    const double frameTime = frameTimeOpt.value_or(t);
    const LayerInput& layer = c.layer;
    const LayerStyle& style = layer.style;
    const std::optional<CounterFx>& fxo = layer.counter;
    const CounterFx* fx = fxo ? &*fxo : nullptr;
    const double size = style.fontSize;
    const double spacing = style.letterSpacing * size;
    const double lineStep = style.lineHeight * size;
    const CellMetrics cell{size, c.digit};
    LayerFrame frame;
    std::vector<Glyph>& glyphs = frame.glyphs;
    std::vector<double>& pose = frame.pose;

    double clock = (fx && fx->style == DigitStyle::None ? frameTime : t) - c.countStart;
    if (fx && fx->stepFps > 0) clock = std::floor(clock * fx->stepFps + 1e-6) / fx->stepFps;

    auto measureOf = [&](const std::string& s) {
        auto it = c.charWidths.find(s);
        return it != c.charWidths.end() ? it->second : 0.0;
    };

    double minX = 0;
    double maxX = 0;
    for (size_t row = 0; row < c.lines.size(); ++row) {
        const std::vector<Seg>& line = c.lines[row];
        const double y = row * lineStep;
        const size_t first = glyphs.size();
        double x = 0;
        for (const Seg& seg : line) {
            if (seg.isText) {
                Glyph g;
                g.text = seg.text;
                g.x = x;
                g.y = y;
                g.w = seg.width;
                g.run = true;
                glyphs.push_back(g);
                x += seg.width;
                continue;
            }
            const SlotState s = seg.slot->sample(clock);
            if (!seg.slot->wheel) {
                const double w = measureOf(s.out) + spacing;
                if (s.presence > 0.002) {
                    Glyph g;
                    g.text = s.out;
                    g.x = x + (w * s.presence) / 2;
                    g.y = y;
                    g.w = w;
                    g.run = false;
                    g.opacity = smoothstep(s.presence * 1.3);
                    g.track = seg.track;
                    glyphs.push_back(g);
                }
                x += w * s.presence;
                pose.push_back(w * s.presence);
                pose.push_back(0);
                continue;
            }
            const double wOut = !s.out.empty() ? (fx->tabular ? c.digitWidth : measureOf(s.out)) : 0;
            const double wIn = !s.in.empty() ? (fx->tabular ? c.digitWidth : measureOf(s.in)) : 0;
            const double natural = s.p <= 0 ? wOut : s.out.empty() ? wIn : s.in.empty() ? wOut : lerp(wOut, wIn, s.p);
            const double w = (natural + (natural > 0 ? spacing : 0)) * s.presence;
            if (s.presence > 0.002) {
                const double visible = smoothstep(s.presence * 1.3);
                const double center = x + w / 2;
                const double width = std::max(w, c.digitWidth);
                if (s.p <= 0 || fx->style == DigitStyle::None) {
                    if (!s.out.empty()) {
                        Glyph g;
                        g.text = s.out;
                        g.opacity = visible;
                        g.x = center;
                        g.y = y;
                        g.w = width;
                        g.run = false;
                        g.track = seg.track;
                        glyphs.push_back(g);
                    }
                } else {
                    for (Glyph g : digitParts(*fx, s, cell)) {
                        g.opacity *= visible;
                        g.x = center;
                        g.y = y;
                        g.w = width;
                        g.run = false;
                        g.track = seg.track;
                        glyphs.push_back(g);
                    }
                }
            }
            x += w;
            pose.push_back(s.pos * fx->distance * size);
            pose.push_back(10 * fx->distance * size);
            pose.push_back(w);
            pose.push_back(0);
        }
        const double shift = style.align == Align::Left ? 0 : style.align == Align::Center ? -x / 2 : -x;
        for (size_t i = first; i < glyphs.size(); i++) {
            glyphs[i].x += shift;
            glyphs[i].line = static_cast<int>(row);
        }
        minX = std::min(minX, shift);
        maxX = std::max(maxX, shift + x);
        frame.lineWidths.push_back(x);
    }

    frame.bounds = Bounds{minX, -c.ascent, maxX - minX, c.ascent + c.descent + (static_cast<double>(c.lines.size()) - 1) * lineStep};
    const Bounds& b = frame.bounds;
    LayerXf& xf = frame.xf;
    xf.dx = 0;
    xf.dy = 0;
    xf.scale = 1;
    xf.px = b.x + b.w / 2;
    xf.py = b.y + b.h / 2;
    xf.opacity = layer.opacity;
    xf.blur = 0;

    if (fx && !c.tracks.empty()) {
        const double window = std::max(c.countEnd - c.countStart, 1e-4);
        if (fx->grow > 0) xf.scale *= 1 - 0.5 * fx->grow * (1 - ease(fx->easing, clamp(clock / window)));
        if (fx->landPop > 0) {
            const double u = (t - (c.countEnd - 0.12)) / 0.5;
            if (u > 0 && u < 1) xf.scale *= 1 + 0.13 * fx->landPop * std::sin(PI * std::pow(u, 0.55)) * std::pow(1 - u, 1.3);
        }
    }
    // (LayerAnim enter / exit and Animation 2 are not ported; see the file comment.)

    const double radius = std::max(b.w, b.h) * 0.5;
    for (double v : {xf.dx, 0.0, xf.dy, 0.0, xf.scale * radius, 0.0, xf.blur, 0.0}) pose.push_back(v);
    return frame;
}

double poseDistance(const LayerFrame& a, const LayerFrame& b) {
    if (a.pose.size() != b.pose.size()) return INF;
    double max = 0;
    for (size_t i = 0; i + 1 < a.pose.size(); i += 2) {
        double d = std::abs(a.pose[i] - b.pose[i]);
        const double wrap = a.pose[i + 1];
        if (wrap > 0) {
            d = std::fmod(d, wrap);
            d = std::min(d, wrap - d);
        }
        if (d > max) max = d;
    }
    return max;
}

size_t trackCount(const CompiledLayer& c) { return c.tracks.size(); }
double countStart(const CompiledLayer& c) { return c.countStart; }
double countEnd(const CompiledLayer& c) { return c.countEnd; }
size_t lineCount(const CompiledLayer& c) { return c.lines.size(); }
const LayerInput& layerInput(const CompiledLayer& c) { return c.layer; }

double settleTime(const CompiledLayer& c) {
    // layerStillTime() of draw.ts, without enter / exit.
    const std::optional<CounterFx>& fx = c.layer.counter;
    if (!fx || c.tracks.empty()) return 0;
    double settle = fx->grow > 0 ? std::max(c.countEnd - c.countStart, 1e-4) : 0;
    for (const Track& t : c.tracks) settle = std::max(settle, t.settlesAt);
    if (fx->stepFps > 0) settle = std::ceil(settle * fx->stepFps) / fx->stepFps;
    const double countHold = c.countStart + settle;
    const double popHold = fx->landPop > 0 ? c.countEnd + 0.38 : 0;
    return std::max(countHold, popHold) + 1e-7;
}

// ── draw.ts sampling ─────────────────────────────────────────────────────────

int motionBlurSamples(double travelPx, int maxSamples) {
    static const int CASCADES[][3] = {
        {1, 1, 1}, {2, 1, 1}, {3, 1, 1}, {4, 1, 1}, {3, 2, 1}, {4, 2, 1}, {3, 3, 1}, {4, 3, 1}, {4, 4, 1},
        {3, 3, 2}, {4, 3, 2}, {3, 3, 3}, {4, 4, 2}, {4, 3, 3}, {4, 4, 3}, {4, 4, 4},
    };
    int wanted = 1;
    if (travelPx > 0.4) wanted = std::min(maxSamples, std::max(4, static_cast<int>(std::ceil(travelPx / 2))));
    for (const auto& cs : CASCADES) {
        if (cs[0] * cs[1] * cs[2] >= wanted) return cs[0] * cs[1] * cs[2];
    }
    return 64;
}

std::vector<double> shutterSampleTimes(double time, double shutterDeg, double fps, int samples, double duration) {
    std::vector<double> out;
    const double shutter = std::max(0.0, shutterDeg) / 360 / fps;
    for (int i = 0; i < samples; ++i) {
        const double t = samples == 1 ? time : time + ((i + 0.5) / samples - 0.5) * shutter;
        out.push_back(std::min(std::max(t, 0.0), duration));
    }
    return out;
}

} // namespace counter
} // namespace text
} // namespace openshot
