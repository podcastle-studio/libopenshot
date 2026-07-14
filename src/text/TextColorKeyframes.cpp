#include "TextStyleKeyframes.h"

#include "TextClipRenderer.h"   // parseTextGradient

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>

namespace openshot {
namespace text {

namespace {

// Straight (non-premultiplied) RGBA; r/g/b in 0..255, a in 0..1. Interpolation happens in this
// CSS colour space, then we re-emit "rgba(...)" which the existing parse path re-consumes.
struct RGBA { double r = 255.0, g = 255.0, b = 255.0, a = 1.0; };

std::string trimCopy(const std::string& s) {
    const auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    const auto e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

int hex2(const std::string& s, size_t i) { return std::stoi(s.substr(i, 2), nullptr, 16); }

// Parse #rgb / #rrggbb / #rrggbbaa / rgb(...) / rgba(...) into straight RGBA. Unknown → opaque white.
RGBA parseRGBA(const std::string& raw) {
    const std::string s = trimCopy(raw);
    RGBA c;
    if (s.empty()) return c;
    try {
        if (s[0] == '#') {
            if (s.size() == 4) {  // #rgb
                c.r = std::stoi(std::string(2, s[1]), nullptr, 16);
                c.g = std::stoi(std::string(2, s[2]), nullptr, 16);
                c.b = std::stoi(std::string(2, s[3]), nullptr, 16);
            } else if (s.size() >= 7) {  // #rrggbb[aa]
                c.r = hex2(s, 1); c.g = hex2(s, 3); c.b = hex2(s, 5);
                if (s.size() >= 9) c.a = hex2(s, 7) / 255.0;
            }
            return c;
        }
        if (s.rfind("rgba", 0) == 0 || s.rfind("RGBA", 0) == 0 ||
            s.rfind("rgb", 0) == 0  || s.rfind("RGB", 0) == 0) {
            const auto open = s.find('(');
            const auto close = s.find(')');
            if (open != std::string::npos && close != std::string::npos && close > open) {
                std::stringstream ss(s.substr(open + 1, close - open - 1));
                std::string part; int idx = 0;
                while (std::getline(ss, part, ',')) {
                    const std::string v = trimCopy(part);
                    // A percentage component (e.g. "100%") is 0..100 of full scale, not a raw 0..255.
                    const bool pct = !v.empty() && v.back() == '%';
                    const double raw = std::stod(v);   // stod stops at '%', giving the numeric part
                    if (idx == 0) c.r = pct ? raw * 2.55 : raw;
                    else if (idx == 1) c.g = pct ? raw * 2.55 : raw;
                    else if (idx == 2) c.b = pct ? raw * 2.55 : raw;
                    else if (idx == 3) c.a = pct ? raw / 100.0 : raw;
                    ++idx;
                }
            }
        }
    } catch (...) {}
    return c;
}

std::string toCss(const RGBA& c) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "rgba(%d, %d, %d, %g)",
                  std::clamp(static_cast<int>(std::lround(c.r)), 0, 255),
                  std::clamp(static_cast<int>(std::lround(c.g)), 0, 255),
                  std::clamp(static_cast<int>(std::lround(c.b)), 0, 255),
                  std::clamp(c.a, 0.0, 1.0));
    return buf;
}

RGBA lerp(const RGBA& a, const RGBA& b, double f) {
    return {a.r + (b.r - a.r) * f, a.g + (b.g - a.g) * f,
            a.b + (b.b - a.b) * f, a.a + (b.a - a.a) * f};
}

// Sample a parsed gradient at normalised position `pos` (0..1) → RGBA (stop colours lerped).
RGBA gradientColorAt(const TextClipGradient& g, double pos) {
    if (g.stops.empty()) return RGBA{};
    std::vector<TextClipGradient::Stop> stops = g.stops;
    std::sort(stops.begin(), stops.end(),
              [](const auto& a, const auto& b) { return a.position < b.position; });
    if (pos <= stops.front().position) return parseRGBA(stops.front().color);
    if (pos >= stops.back().position)  return parseRGBA(stops.back().color);
    for (size_t i = 1; i < stops.size(); ++i) {
        if (pos <= stops[i].position) {
            const double span = stops[i].position - stops[i - 1].position;
            const double f = span > 0.0 ? (pos - stops[i - 1].position) / span : 0.0;
            return lerp(parseRGBA(stops[i - 1].color), parseRGBA(stops[i].color), f);
        }
    }
    return parseRGBA(stops.back().color);
}

// Is this raw CSS string a parseable linear-gradient?
bool isGradientStr(const std::string& css) { return parseTextGradient(css).has_value(); }

// A raw CSS colour as a gradient: a linear-gradient parses through; a solid becomes a single stop.
TextClipGradient asGradient(const std::string& css) {
    if (auto g = parseTextGradient(css)) return *g;
    TextClipGradient g;
    g.stops.push_back({css, 0.0});
    return g;
}

// CSS cubic-bezier easing: given progress x in [0,1] and handles (x1,y1,x2,y2) with implicit
// endpoints (0,0)/(1,1), solve for the bezier parameter at x then return the eased y.
double bezierComp(double t, double p1, double p2) {
    const double mt = 1.0 - t;
    return 3.0 * mt * mt * t * p1 + 3.0 * mt * t * t * p2 + t * t * t;
}
double cubicBezierEase(double x, const std::array<double, 4>& h) {
    if (x <= 0.0) return 0.0;
    if (x >= 1.0) return 1.0;
    double lo = 0.0, hi = 1.0, t = x;
    for (int i = 0; i < 32; ++i) {          // bisection on x (monotonic for valid easings)
        const double xt = bezierComp(t, h[0], h[2]);
        if (std::abs(xt - x) < 1e-6) break;
        if (xt < x) lo = t; else hi = t;
        t = 0.5 * (lo + hi);
    }
    return bezierComp(t, h[1], h[3]);
}

// Eased fraction for the segment ending at `right`, given raw progress u in [0,1].
double easedFraction(double u, const ColorKeyPoint& right) {
    switch (right.interp) {
        case openshot::CONSTANT: return 0.0;                 // hold the left value until `right`
        case openshot::LINEAR:   return std::clamp(u, 0.0, 1.0);
        case openshot::BEZIER:   return cubicBezierEase(std::clamp(u, 0.0, 1.0), right.bezier);
        default:                 return std::clamp(u, 0.0, 1.0);
    }
}

} // namespace

std::string sampleColorChannel(const ColorKeyframeChannel& channel, double t,
                               const std::string& fallback) {
    if (channel.empty()) return fallback;

    std::vector<ColorKeyPoint> pts = channel.points;
    std::sort(pts.begin(), pts.end(),
              [](const ColorKeyPoint& a, const ColorKeyPoint& b) { return a.timeSec < b.timeSec; });

    // Locate the segment [L, R] bracketing t; clamp (hold) outside the range.
    size_t li = 0;
    double f = 0.0;
    bool single = false;
    if (t <= pts.front().timeSec)      { li = 0; single = true; }
    else if (t >= pts.back().timeSec)  { li = pts.size() - 1; single = true; }
    else {
        for (size_t i = 0; i + 1 < pts.size(); ++i) {
            if (t >= pts[i].timeSec && t < pts[i + 1].timeSec) { li = i; break; }
        }
        const double span = pts[li + 1].timeSec - pts[li].timeSec;
        const double u = span > 0.0 ? (t - pts[li].timeSec) / span : 0.0;
        f = easedFraction(u, pts[li + 1]);
    }
    const ColorKeyPoint& L = pts[li];
    const ColorKeyPoint& R = single ? pts[li] : pts[li + 1];

    const bool anyGradient =
        std::any_of(pts.begin(), pts.end(), [](const ColorKeyPoint& p) { return isGradientStr(p.value); });

    if (!anyGradient) {
        if (single) return toCss(parseRGBA(L.value));
        return toCss(lerp(parseRGBA(L.value), parseRGBA(R.value), f));
    }

    // Gradient mode: resample both endpoints onto the union of all stop positions in the channel.
    std::vector<double> positions;
    for (const auto& p : pts) {
        for (const auto& s : asGradient(p.value).stops) positions.push_back(s.position);
    }
    std::sort(positions.begin(), positions.end());
    positions.erase(std::unique(positions.begin(), positions.end(),
                    [](double a, double b) { return std::abs(a - b) < 1e-6; }),
                    positions.end());
    if (positions.empty()) positions.push_back(0.0);

    const TextClipGradient gL = asGradient(L.value);
    const TextClipGradient gR = asGradient(R.value);
    // Solids borrow the neighbouring gradient's angle.
    const double angleL = isGradientStr(L.value) ? gL.angle : gR.angle;
    const double angleR = isGradientStr(R.value) ? gR.angle : gL.angle;
    const double angle = single ? angleL : (angleL + (angleR - angleL) * f);

    if (positions.size() == 1) {
        const RGBA c = single ? gradientColorAt(gL, positions[0])
                              : lerp(gradientColorAt(gL, positions[0]),
                                     gradientColorAt(gR, positions[0]), f);
        return toCss(c);
    }

    std::ostringstream out;
    out << "linear-gradient(" << angle << "deg";
    for (double pos : positions) {
        const RGBA c = single ? gradientColorAt(gL, pos)
                              : lerp(gradientColorAt(gL, pos), gradientColorAt(gR, pos), f);
        out << ", " << toCss(c) << ' ' << (pos * 100.0) << '%';
    }
    out << ')';
    return out.str();
}

} // namespace text
} // namespace openshot
