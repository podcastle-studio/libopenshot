// Port of track.ts (async-counters-handoff-v4): digit wheels, speed limits, epochs.
// Keep this line-for-line with the TS: the parity fixtures compare glyph lists to 1e-6.
//
// The idea (from the TS): each wheel counts its own digit changes; that staircase in time is
// blurred by a `swap`-second step kernel, so slow changes become eased single steps and fast
// ones add up to a continuous spin, with no switch between the two. A wheel that would spin
// faster than `maxSpeed` replays the stretch with whole turns removed so it still lands right.

#include "CounterEngine.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <list>
#include <mutex>
#include <set>
#include <sstream>
#include <unordered_map>

namespace openshot {
namespace text {
namespace counter {

namespace {

constexpr int EXACT = 48;
constexpr int SIMPSON = 32;
const char* const DIGITS = "0123456789";
constexpr double MAX_LEVEL = 4503599627370495.0;   // 2 ** 52 - 1
constexpr double INF = std::numeric_limits<double>::infinity();

using Span = std::array<double, 2>;

struct Epoch {
    double ta = 0, tb = 0;
    double xa = 0, xb = 0;
    double ma = 0, mb = 0;
    int mdir = 1;
    bool neg = false;
    double scale = 1;
    int decimals = 0;
    std::string suffix;
};

struct WheelEpoch {
    bool exists = false;
    bool pinned = false;
    double unit = 1;
    double la = 0, lb = 0;
    double base = 0;
};

struct Run {
    double a = 0, b = 0;
    double from = 0, advance = 0;
    double m0 = 0, m1 = 0;
    double n1 = 0;
    std::string seamOut, seamIn;
};

struct WheelAt { bool exists; bool pinned; double unit; };

struct WheelSpec {
    std::string id;
    int radix = 10;
    std::function<WheelAt(const Epoch&)> at;
    bool hasAfter = false;
    std::string afterChar;
    enum Tie { Wheel, Decimal, Always } tie = Always;
};

inline double pow10i(int n) { return std::pow(10.0, n); }

// hash() of track.ts: Math.imul / >>> on 32-bit integers.
inline uint32_t toU32(double v) {   // ToUint32 of an integral double (all inputs here are small)
    double m = std::fmod(std::trunc(v), 4294967296.0);
    if (m < 0) m += 4294967296.0;
    return static_cast<uint32_t>(m);
}
double hashFn(double a, double b) {
    uint32_t h = (toU32(a + 2654435769.0) * 0x85ebca6bu) ^ (toU32(b + 2135587861.0) * 0xc2b2ae35u);
    h = (h ^ (h >> 15)) * 0x2c1b3c6du;
    h = (h ^ (h >> 12)) * 0x297a2d39u;
    return static_cast<double>(h ^ (h >> 15)) / 4294967296.0;
}

inline double hermite(double s, double m0, double m1) {
    const double s2 = s * s;
    const double s3 = s2 * s;
    return 3 * s2 - 2 * s3 + (s3 - 2 * s2 + s) * m0 + (s3 - s2) * m1;
}

double backFor(double overshoot) {
    if (overshoot <= 0) return 0;
    double lo = 0;
    double hi = 8;
    for (int i = 0; i < 30; i++) {
        const double s = (lo + hi) / 2;
        if ((4 * s * s * s) / (27 * (s + 1) * (s + 1)) < overshoot) lo = s;
        else hi = s;
    }
    return hi;
}

inline double backOut(double x, double s) {
    if (x <= 0) return 0;
    if (x >= 1) return 1;
    const double u = x - 1;
    return 1 + (s + 1) * u * u * u + s * u * u;
}

std::vector<Span> mergeSpans(const std::vector<Span>& spans) {
    std::vector<Span> out;
    for (const Span& s : spans) {
        if (!out.empty() && s[0] <= out.back()[1] + 1e-9) out.back()[1] = std::max(out.back()[1], s[1]);
        else out.push_back(s);
    }
    return out;
}

std::vector<Span> invertSpans(const std::vector<Span>& spans) {
    std::vector<Span> out;
    double at = -INF;
    for (const Span& s : spans) {
        if (s[0] > at) out.push_back({at, s[0]});
        at = s[1];
    }
    if (at < INF) out.push_back({at, INF});
    return out;
}

std::shared_ptr<TrackSlot> charSlot(const std::string& id, const std::string& ch, std::function<double(double)> presence) {
    auto slot = std::make_shared<TrackSlot>();
    slot->id = id;
    slot->wheel = false;
    slot->sample = [ch, presence](double t) {
        SlotState s;
        s.out = ch;
        s.in = "";
        s.p = 0;
        s.presence = presence(t);
        s.pos = 0;
        s.dir = 1;
        return s;
    };
    return slot;
}

// String(n).length for the non-negative integral doubles of wheelSpecs.
int digitCount(double n) { return static_cast<int>(jsIntegerString(n).size()); }

// speedProgress(): the exact easing samples of the speed grid, cached across tracks like the TS
// (a pure cache: it never changes a result).
struct GridCache {
    std::mutex m;
    std::list<std::pair<std::string, std::shared_ptr<std::vector<double>>>> order;
    std::unordered_map<std::string, std::shared_ptr<std::vector<double>>> map;
    size_t size = 0;
};
GridCache& gridCache() { static GridCache c; return c; }
constexpr size_t SPEED_PROGRESS_LIMIT = 262144;

std::shared_ptr<std::vector<double>> speedProgress(const Bezier& easing, double T, double swap, int n, double dt, int taps) {
    std::ostringstream key;
    key.precision(17);
    key << T << ':' << swap << ':' << easing.x1 << ':' << easing.y1 << ':' << easing.x2 << ':' << easing.y2 << ':' << n << ':' << taps;
    GridCache& c = gridCache();
    {
        std::lock_guard<std::mutex> lock(c.m);
        auto it = c.map.find(key.str());
        if (it != c.map.end()) return it->second;
    }
    auto grid = std::make_shared<std::vector<double>>(static_cast<size_t>(n + 1) * taps);
    std::vector<double> offsets(taps);
    for (int k = 0; k < taps; k++) offsets[k] = ((k + 0.5) / taps) * swap;
    for (int i = 0; i <= n; i++) {
        const double t = i == 0 ? 0 : i * dt;
        for (int k = 0; k < taps; k++) (*grid)[static_cast<size_t>(i) * taps + k] = ease(easing, (t - offsets[k]) / T);
    }
    std::lock_guard<std::mutex> lock(c.m);
    while (c.size + grid->size() > SPEED_PROGRESS_LIMIT && !c.order.empty()) {
        c.size -= c.order.front().second->size();
        c.map.erase(c.order.front().first);
        c.order.pop_front();
    }
    c.order.emplace_back(key.str(), grid);
    c.map[key.str()] = grid;
    c.size += grid->size();
    return grid;
}

std::vector<WheelSpec> wheelSpecs(const NumberToken& tok, const std::vector<Epoch>& epochs) {
    std::vector<WheelSpec> specs;
    auto maxUnits = [&](const std::function<double(const Epoch&)>& unitOf) {
        double m = -INF;
        for (const Epoch& e : epochs) m = std::max(m, std::floor((std::max(e.ma, e.mb) * e.scale + 0.5) / unitOf(e)));
        return m;
    };

    if (tok.isTime && tok.time) {
        const int fields = tok.time->fields, lead = tok.time->lead, frac = tok.time->frac;
        const std::string fracSep = tok.time->fracSep;
        const double sec = pow10i(frac);
        auto fixed = [](const std::string& id, double unit, int radix, bool after, const std::string& ch) {
            WheelSpec s;
            s.id = id;
            s.radix = radix;
            s.at = [unit](const Epoch&) { return WheelAt{true, true, unit}; };
            s.hasAfter = after;
            s.afterChar = ch;
            s.tie = WheelSpec::Always;
            return s;
        };
        const double leadUnit = (fields == 3 ? 3600 : 60) * sec;
        const int leadDigits = std::max(lead, digitCount(maxUnits([leadUnit](const Epoch&) { return leadUnit; })));
        for (int i = leadDigits - 1; i >= 0; i--) {
            WheelSpec s;
            s.id = "L" + std::to_string(i);
            s.radix = 10;
            const bool pinned = i < lead;
            const double unit = leadUnit * pow10i(i);
            s.at = [pinned, unit](const Epoch&) { return WheelAt{true, pinned, unit}; };
            if (i == 0) { s.hasAfter = true; s.afterChar = ":"; s.tie = WheelSpec::Always; }
            specs.push_back(s);
        }
        if (fields == 3) {
            specs.push_back(fixed("m1", 600 * sec, 6, false, ""));
            specs.push_back(fixed("m0", 60 * sec, 10, true, ":"));
        }
        specs.push_back(fixed("s1", 10 * sec, 6, false, ""));
        specs.push_back(fixed("s0", sec, 10, frac > 0, fracSep));
        for (int i = frac - 1; i >= 0; i--) specs.push_back(fixed("f" + std::to_string(i), pow10i(i), 10, false, ""));
        return specs;
    }

    int top = tok.minInt - 1;
    int maxDecimals = 0;
    for (const Epoch& e : epochs) {
        const int digits = digitCount(std::floor(std::max(e.ma, e.mb) * e.scale + 0.5));
        top = std::max(top, digits - 1 - e.decimals);
        maxDecimals = std::max(maxDecimals, e.decimals);
    }
    const int minInt = tok.minInt;
    for (int p = top; p >= -maxDecimals; p--) {
        WheelSpec s;
        s.id = "p" + std::to_string(p);
        s.radix = 10;
        if (p > 0 && p % 3 == 0 && !tok.groupSep.empty()) { s.hasAfter = true; s.afterChar = tok.groupSep; s.tie = WheelSpec::Wheel; }
        if (p == 0 && maxDecimals > 0) { s.hasAfter = true; s.afterChar = tok.decimalSep; s.tie = WheelSpec::Decimal; }
        s.at = [p, minInt](const Epoch& e) { return WheelAt{p >= -e.decimals, p < minInt, pow10i(p + e.decimals)}; };
        specs.push_back(s);
    }
    return specs;
}

// All state of one track, shared by its slot closures.
struct TrackCtx {
    NumberToken tok;
    CounterFx fx;
    double windowSeconds = 0;
    double x0 = 0, x1 = 0, span = 0;
    bool counting = true, animated = true;
    double T = 0, swap = 0, cascade = 0;
    std::vector<Epoch> epochs;
    std::vector<double> points;
    int last = 0;
    std::function<double(double)> step;
    double lag = 0;
    bool speedGridBuilt = false;
    int gn = 0;
    double gdt = 0;
    std::vector<double> grate;

    double xAt(double t) const { return x0 + span * ease(fx.easing, t / T); }
    double tRaw(double x) const { return span == 0 ? 0 : T * easeInverse(fx.easing, (x - x0) / span); }
    double tOf(double x) const { return tRaw(x); }   // the TS memoises this; the value is the same
    int epochAt(double t) const {
        for (int i = 0; i < last; i++) if (t < epochs[i].tb) return i;
        return last;
    }
    double magAt(double t, const Epoch& e) const {
        return clamp(std::abs(xAt(t)), std::min(e.ma, e.mb), std::max(e.ma, e.mb));
    }

    std::function<double(double)> presenceOf(const std::vector<Span>& spans, bool hard) const {
        if (spans.empty()) return [](double) { return 0.0; };
        if (spans.size() == 1 && spans[0][0] == -INF && spans[0][1] == INF) {
            if (hard) return [](double t) { return (t >= -INF && t < INF) ? 1.0 : 0.0; };
            return [](double) { return 1.0; };
        }
        const double sw = swap;
        return [spans, hard, sw](double t) {
            double v = 0;
            for (const Span& s : spans) {
                const double a = s[0], b = s[1];
                if (hard) {
                    if (t >= a && t < b) return 1.0;
                    continue;
                }
                v += (a == -INF ? 1 : smoothstep((t - a) / sw)) - (b == INF ? 0 : smoothstep((t - b) / sw));
            }
            return clamp(v);
        };
    }

    std::vector<Span> openEnds(const std::vector<Span>& spans) const {
        std::vector<Span> out = mergeSpans(spans);
        for (Span& s : out) {
            if (s[0] <= 1e-9) s[0] = -INF;
            if (s[1] >= T - 1e-9) s[1] = INF;
        }
        return out;
    }

    void buildSpeedGrid() {
        const double end = T + swap;
        const int n = static_cast<int>(clamp(std::ceil(end * 90), 48, 900));
        const double dt = end / n;
        const int taps = static_cast<int>(clamp(std::ceil(swap / dt), 8, 64));
        std::vector<double> weight;
        for (int k = 0; k < taps; k++) weight.push_back(step((k + 1.0) / taps) - step(static_cast<double>(k) / taps));
        const size_t count = epochs.size();
        std::vector<double> lows(count), highs(count);
        for (size_t e = 0; e < count; e++) {
            lows[e] = std::min(epochs[e].xa, epochs[e].xb);
            highs[e] = std::max(epochs[e].xa, epochs[e].xb);
        }
        const auto progress = speedProgress(fx.easing, T, swap, n, dt, taps);
        std::vector<double> rate(static_cast<size_t>(n) * count);
        std::vector<double> prev(count, 0.0), now(count, 0.0);
        for (int i = 0; i <= n; i++) {
            std::fill(now.begin(), now.end(), 0.0);
            for (int k = 0; k < taps; k++) {
                const double x = x0 + span * (*progress)[static_cast<size_t>(i) * taps + k];
                for (size_t e = 0; e < count; e++) now[e] += weight[k] * clamp(x, lows[e], highs[e]);
            }
            if (i > 0) {
                for (size_t e = 0; e < count; e++) rate[(i - 1) * count + e] = std::abs(now[e] - prev[e]) / dt;
            }
            std::swap(prev, now);
        }
        gn = n;
        gdt = dt;
        grate = std::move(rate);
        speedGridBuilt = true;
    }
};

} // namespace

Track buildTrack(const NumberToken& tok, double fromValue, const CounterFx& fx, double windowSeconds) {
    auto staticTrack = [&]() {
        Track t;
        t.token = tok;
        t.isStatic = true;
        t.cells.push_back(charSlot("literal", tok.source ? *tok.source : formatBody(tok, tok.value, false), [](double) { return 1.0; }));
        if (tok.sign) t.signs.push_back(charSlot("sign", tok.sign == '-' ? tok.minusChar : std::string(1, tok.sign), [](double) { return 1.0; }));
        const double v = tok.value;
        t.valueAt = [v](double) { return v; };
        t.settlesAt = 0;
        return t;
    };
    auto ctx = std::make_shared<TrackCtx>();
    TrackCtx& c = *ctx;
    c.tok = tok;
    c.fx = fx;
    c.windowSeconds = windowSeconds;
    const double q = pow10i(tok.decimals);
    const bool isTime = tok.isTime;
    c.x1 = isTime ? std::abs(tok.value) : tok.value;
    c.x0 = isTime ? std::abs(jsRound(fromValue * q) / q) : jsRound(fromValue * q) / q;
    c.span = c.x1 - c.x0;
    const double x0 = c.x0, x1 = c.x1, span = c.span;
    if (tok.precisionLost || !std::isfinite(span) || !std::isfinite(pow10i(tok.minInt + tok.decimals - 1))) return staticTrack();
    const bool scaled = tok.compact.has_value() && fx.compactScale;
    const int fixedTier = tok.compact && !scaled ? tok.compact->tier : 0;
    c.counting = fx.mode == CounterMode::Count;
    c.animated = fx.style != DigitStyle::None;
    const bool counting = c.counting, animated = c.animated;

    const bool trendUp = span >= 0;
    auto dirOf = [&](int index) -> int {
        const int autoDir = !counting || trendUp ? 1 : -1;
        if (fx.direction == Direction::Up) return 1;
        if (fx.direction == Direction::Down) return -1;
        if (fx.direction == Direction::Alternate) return index % 2 == 0 ? autoDir : (autoDir == 1 ? -1 : 1);
        return autoDir;
    };

    /* ---- epochs ---- */
    auto epochFor = [&](double xa, double xb) {
        const double mid = (xa + xb) / 2;
        const int tier = scaled ? compactTier(tok, std::abs(mid)) : fixedTier;
        const int decimals = tok.compact && tier == 0 ? 0 : tok.decimals;
        double ma = std::abs(xa);
        double mb = std::abs(xb);
        if (scaled) {
            const double edge = compactBoundary(tok, tier);
            if (std::abs(ma - edge) < edge * 1e-9) ma *= 1 - 1e-12;
            if (std::abs(mb - edge) < edge * 1e-9) mb *= 1 - 1e-12;
        }
        Epoch e;
        e.xa = xa;
        e.xb = xb;
        e.ma = ma;
        e.mb = mb;
        e.mdir = mb >= ma ? 1 : -1;
        e.neg = mid < 0;
        e.scale = pow10i(decimals) / std::pow(1000.0, tier);
        e.decimals = decimals;
        e.suffix = tok.compact ? tok.compact->ladder[tier] : "";
        return e;
    };

    std::vector<double> cuts;
    if (counting) {
        if (x0 * x1 < 0) cuts.push_back(0);
        if (scaled && tok.compact) {
            for (int tier = 0; tier < static_cast<int>(tok.compact->ladder.size()) - 1; tier++) {
                for (int sgn : {1, -1}) {
                    const double v = sgn * compactBoundary(tok, tier);
                    if ((v - x0) * (v - x1) < 0) cuts.push_back(v);
                }
            }
        }
        std::stable_sort(cuts.begin(), cuts.end(), [&](double a, double b) { return trendUp ? a < b : b < a; });
    }
    std::vector<double>& points = c.points;
    if (counting) {
        points.push_back(x0);
        points.insert(points.end(), cuts.begin(), cuts.end());
        points.push_back(x1);
    } else {
        points = {x1, x1};
    }
    std::vector<Epoch> bare;
    for (size_t i = 0; i + 1 < points.size(); i++) bare.push_back(epochFor(points[i], points[i + 1]));
    if (!counting && std::abs(x0) * bare[0].scale > MAX_LEVEL) return staticTrack();
    double changes = static_cast<double>(bare.size()) - 1;
    for (const Epoch& e : bare) {
        const double a = e.ma * e.scale;
        const double b = e.mb * e.scale;
        changes += std::abs(jsRound(b) - jsRound(a));
        if (!(std::max({a, b, changes}) <= MAX_LEVEL)) return staticTrack();
    }
    const std::vector<WheelSpec> specs = wheelSpecs(tok, bare);

    /* ---- timing ---- */
    const int wheelCount = static_cast<int>(specs.size());
    double swap = fx.swap;
    double cascade = fx.cascade;
    const double reserve = counting && animated ? swap + cascade * (wheelCount - 1) : 0;
    const double maxReserve = windowSeconds * 0.5;
    if (reserve > maxReserve && reserve > 0) {
        swap *= maxReserve / reserve;
        cascade *= maxReserve / reserve;
    }
    c.T = std::max(1e-4, windowSeconds - (counting && animated ? swap + cascade * (wheelCount - 1) : 0));
    swap = std::max(swap, 1e-4);
    c.swap = swap;
    c.cascade = cascade;
    const double T = c.T;

    c.epochs.resize(bare.size());
    for (size_t i = 0; i < bare.size(); i++) {
        Epoch e = bare[i];
        e.ta = counting ? (i == 0 ? 0 : c.tOf(points[i])) : 0;
        e.tb = counting ? (i == bare.size() - 1 ? T : c.tOf(points[i + 1])) : 0;
        c.epochs[i] = e;
    }
    c.last = static_cast<int>(c.epochs.size()) - 1;
    const int last = c.last;
    const std::vector<Epoch>& epochs = c.epochs;

    /* ---- digit step curve ---- */
    const bool strip = fx.style == DigitStyle::Roll || fx.style == DigitStyle::Drum;
    const double back = backFor(strip && !fx.soft ? 0.16 * fx.bounce : 0);
    if (fx.soft) c.step = [](double x) { return smoothstep(x); };
    else c.step = [back](double x) { return backOut(x, back); };
    c.lag = swap * (fx.soft ? 0.5 : 0.25 - back / 12);

    /* ---- wheels ---- */
    Track track;
    track.token = tok;
    const Epoch finalEpoch = epochs[last];
    std::vector<int> order(specs.size());
    for (size_t i = 0; i < specs.size(); i++) order[i] = static_cast<int>(i);
    if (fx.order == Order::Rtl) std::reverse(order.begin(), order.end());
    if (fx.order == Order::Random) {
        std::stable_sort(order.begin(), order.end(), [](int a, int b) { return hashFn(a, 17) < hashFn(b, 17); });
    }
    auto rankOf = [&](int i) { return static_cast<int>(std::find(order.begin(), order.end(), i) - order.begin()); };

    for (int index = 0; index < wheelCount; index++) {
        const WheelSpec& spec = specs[index];
        struct Wheel {
            std::shared_ptr<TrackCtx> ctx;
            int index = 0;
            int radix = 10;
            int dir = 1;
            double delay = 0;
            std::vector<WheelEpoch> eps;
            double total = 0;
            std::function<double(double)> presence;
            std::vector<Run> runs;
            std::string finalGlyph;
            double lockAt = 0;
            double fromDigit = 0;
            double turnsTotal = 0;
            double slotBack = 0;
            double lockStep = 1;

            std::string glyph(int ei, double level) const {
                const WheelEpoch& w = eps[ei];
                if (!w.exists || (!w.pinned && level <= 0)) return "";
                double digit = std::fmod(level, radix);
                if (digit < 0) digit += radix;
                return std::string(1, DIGITS[static_cast<int>(digit)]);
            }
            bool hasJump(int i) const {
                return i < ctx->last && eps[i + 1].base > eps[i].base + std::abs(eps[i].lb - eps[i].la);
            }
            double digitAt(double n) const {
                for (int i = ctx->last; i >= 0; i--) {
                    if (n >= eps[i].base || i == 0) {
                        const double level = eps[i].la + ctx->epochs[i].mdir * (n - eps[i].base);
                        return std::fmod(std::fmod(level, radix) + radix, radix);
                    }
                }
                return 0;
            }
            std::string glyphAt(double n) const {
                for (int i = ctx->last; i >= 0; i--) {
                    if (n >= eps[i].base || i == 0) return glyph(i, eps[i].la + ctx->epochs[i].mdir * (n - eps[i].base));
                }
                return "";
            }
            double stepsAt(double t) const {
                const TrackCtx& c = *ctx;
                if (t <= 0) return 0;
                if (t >= c.T) return total;
                const int i = c.epochAt(t);
                const Epoch& e = c.epochs[i];
                const WheelEpoch& w = eps[i];
                if (!w.exists) return w.base;
                const double level = std::floor((c.magAt(t, e) * e.scale + 0.5) / w.unit);
                return w.base + clamp(e.mdir * (level - w.la), 0, std::abs(w.lb - w.la));
            }
            double blurred(double t) const {
                const TrackCtx& c = *ctx;
                const double sw = c.swap;
                const double a = t - sw;
                const double g0 = stepsAt(a);
                if (stepsAt(t) == g0) return g0;
                double pos = g0;
                for (int i = 0; i <= c.last; i++) {
                    const Epoch& e = c.epochs[i];
                    const WheelEpoch& w = eps[i];
                    if (e.tb <= a || e.ta > t || !w.exists) {
                        if (hasJump(i) && e.tb > a && e.tb <= t) pos += c.step((t - e.tb) / sw);
                        continue;
                    }
                    const double lo = std::max(a, e.ta);
                    const double hi = std::min(t, e.tb);
                    auto levelAt = [&](double time, double edge, double fallback) {
                        return std::abs(time - edge) < 1e-12 ? fallback : std::floor((c.magAt(time, e) * e.scale + 0.5) / w.unit);
                    };
                    const double l1 = levelAt(lo, e.ta, w.la);
                    const double l2 = levelAt(hi, e.tb, w.lb);
                    const double first = e.mdir == 1 ? l1 + 1 : l2 + 1;
                    const double lastLevel = e.mdir == 1 ? l2 : l1;
                    const double sign = e.neg ? -1 : 1;
                    auto played = [&](double level, bool cached) {
                        const double x = (sign * (level * w.unit - 0.5)) / e.scale;
                        return c.step((t - clamp(cached ? c.tOf(x) : c.tRaw(x), lo, hi)) / sw);
                    };
                    const double count = lastLevel - first + 1;
                    if (count <= EXACT) {
                        for (double j = 0; j < count; j++) pos += played(first + j, true);
                    } else {
                        const double from = first - 0.5;
                        const double h = (lastLevel + 0.5 - from) / SIMPSON;
                        double acc = played(from, false) + played(lastLevel + 0.5, false);
                        for (int k = 1; k < SIMPSON; k++) acc += (k % 2 ? 4 : 2) * played(from + k * h, false);
                        pos += (acc * h) / 3;
                    }
                    if (hasJump(i) && e.tb > a && e.tb <= t) pos += c.step((t - e.tb) / sw);
                }
                return pos;
            }
            SlotState sample(double clock) const {
                const TrackCtx& c = *ctx;
                const CounterFx& fx = c.fx;
                double pos;
                std::string out, inn;
                double pres = 1;
                if (fx.mode == CounterMode::Slot) {
                    pos = turnsTotal * backOut(clock / lockAt, slotBack);
                    const double n = std::floor(pos);
                    auto digit = [&](double k) {
                        double d = std::fmod(std::fmod(fromDigit + k, radix) + radix, radix);
                        return std::string(1, DIGITS[static_cast<int>(d)]);
                    };
                    out = finalGlyph.empty() ? "" : digit(n);
                    inn = finalGlyph.empty() ? "" : digit(n + 1);
                    pres = finalGlyph.empty() ? 0 : 1;
                } else if (fx.mode == CounterMode::Scramble) {
                    pos = std::min(std::max(clock, 0.0) * fx.rate, lockStep);
                    const double n = std::floor(pos);
                    auto digit = [&](double k) {
                        if (k >= lockStep) return finalGlyph;
                        return std::string(1, DIGITS[static_cast<int>(std::floor(hashFn(index * 131 + 7, k) * radix))]);
                    };
                    out = finalGlyph.empty() ? "" : digit(n);
                    inn = finalGlyph.empty() ? "" : digit(n + 1);
                    pres = finalGlyph.empty() ? 0 : 1;
                } else {
                    const double t = clock - delay;
                    const Run* run = nullptr;
                    if (c.animated) {
                        for (const Run& r : runs) if (t >= r.a && t < r.b) { run = &r; break; }
                    }
                    if (run) pos = run->from + run->advance * hermite((t - run->a) / (run->b - run->a), run->m0, run->m1);
                    else pos = c.animated ? blurred(t) : stepsAt(t);
                    const double n = std::floor(pos + 1e-9);
                    auto shown = [&](double k) {
                        if (run && k == run->n1) return run->seamOut;
                        if (run && k == run->n1 + 1) return run->seamIn;
                        return glyphAt(k);
                    };
                    out = shown(n);
                    inn = shown(n + 1);
                    pres = presence(t);
                }
                double p = pos - std::floor(pos + 1e-9);
                if (p < 1e-5 || !c.animated) p = 0;
                SlotState s;
                s.out = out;
                s.in = inn;
                s.p = p;
                s.presence = pres;
                s.pos = pos;
                s.dir = dir;
                return s;
            }
        };
        auto w = std::make_shared<Wheel>();
        w->ctx = ctx;
        w->index = index;
        w->radix = spec.radix;
        w->dir = dirOf(index);
        w->delay = counting && animated ? cascade * (wheelCount - 1 - index) : 0;
        for (const Epoch& e : epochs) {
            const WheelAt a = spec.at(e);
            WheelEpoch we;
            we.exists = a.exists;
            we.pinned = a.pinned;
            we.unit = a.unit;
            auto level = [&](double m) { return a.exists ? std::floor((m * e.scale + 0.5) / a.unit) : 0.0; };
            we.la = level(e.ma);
            we.lb = level(e.mb);
            we.base = 0;
            w->eps.push_back(we);
        }
        for (size_t i = 1; i < w->eps.size(); i++) {
            const double jump = w->glyph(static_cast<int>(i) - 1, w->eps[i - 1].lb) != w->glyph(static_cast<int>(i), w->eps[i].la) ? 1 : 0;
            w->eps[i].base = w->eps[i - 1].base + std::abs(w->eps[i - 1].lb - w->eps[i - 1].la) + jump;
        }
        w->total = w->eps[last].base + std::abs(w->eps[last].lb - w->eps[last].la);

        std::vector<Span> spans;
        for (size_t i = 0; i < epochs.size(); i++) {
            const Epoch& e = epochs[i];
            const WheelEpoch& we = w->eps[i];
            if (!we.exists) continue;
            const double need = (we.unit - 0.5) / e.scale;
            if (we.pinned || std::min(e.ma, e.mb) >= need) spans.push_back({e.ta, e.tb});
            else if (std::max(e.ma, e.mb) >= need) {
                const double cross = c.tOf((e.neg ? -1 : 1) * need);
                spans.push_back(e.mdir == 1 ? Span{cross, e.tb} : Span{e.ta, cross});
            }
        }
        const std::vector<Span> vis = counting ? c.openEnds(spans) : std::vector<Span>{};
        w->presence = c.presenceOf(vis, !animated);

        /* stretches that would spin too fast are replayed with whole turns removed */
        if (counting && animated && span != 0 && fx.maxSpeed > 0 && w->total > spec.radix) {
            const double cap = fx.maxSpeed * (0.82 + 0.36 * std::fmod(0.37 + index * 0.618034, 1.0));
            if (!c.speedGridBuilt) c.buildSpeedGrid();
            const int n = c.gn;
            const double dt = c.gdt;
            const std::vector<double>& rate = c.grate;
            int open = -1;
            for (int i = 0; i <= n; i++) {
                bool fast = false;
                if (i < n) {
                    double speed = 0;
                    for (int e = 0; e <= last; e++) {
                        if (w->eps[e].exists) speed += (rate[static_cast<size_t>(i) * epochs.size() + e] * epochs[e].scale) / w->eps[e].unit;
                    }
                    fast = speed > cap;
                }
                if (fast && open < 0) open = i;
                if (!fast && open >= 0) {
                    const double a = open * dt;
                    const double b = i * dt;
                    open = -1;
                    if (b - a < 0.02) continue;
                    const double from = w->blurred(a);
                    const double real = w->blurred(b) - from;
                    const double turns = std::min(jsRound((real - cap * (b - a)) / spec.radix), std::floor((real - 1) / spec.radix));
                    if (!(turns >= 1)) continue;
                    double advance = real - turns * spec.radix;
                    const double end = from + real;
                    const double n0 = std::floor(from + 1e-9);
                    const bool moving = from - n0 > 1e-6;
                    static const int tries[] = {0, 1, -1, 2, -2, 3, -3, 4, -4, 5};
                    auto fits = [&](int d, bool both) {
                        if (advance + d < 1) return false;
                        const double n1 = std::floor(end - advance - d + 1e-9);
                        return w->digitAt(n1) == w->digitAt(n0) && (!both || w->digitAt(n1 + 1) == w->digitAt(n0 + 1));
                    };
                    int pick = 0;
                    bool found = false;
                    for (int d : tries) if (fits(d, moving)) { pick = d; found = true; break; }
                    if (!found) for (int d : tries) if (fits(d, false)) { pick = d; found = true; break; }
                    advance += found ? pick : 0;
                    const double h = std::min(1e-3, (b - a) / 10);
                    const double va = a == 0 ? cap : (w->blurred(a + h) - w->blurred(a - h)) / (2 * h);
                    const double vb = i == n ? 0 : (w->blurred(b + h) - w->blurred(b - h)) / (2 * h);
                    const double k = (b - a) / advance;
                    Run r;
                    r.a = a;
                    r.b = b;
                    r.from = end - advance;
                    r.advance = advance;
                    r.m0 = clamp(va * k, 0, 1.6);
                    r.m1 = clamp(vb * k, 0, 1.6);
                    r.n1 = std::floor(end - advance + 1e-9);
                    r.seamOut = w->glyphAt(n0);
                    r.seamIn = w->glyphAt(n0 + 1);
                    w->runs.push_back(r);
                }
            }
        }
        /* slot and scramble */
        const WheelEpoch& fin = w->eps[last];
        w->finalGlyph = w->glyph(last, fin.lb);
        const int rank = rankOf(index);
        w->lockAt = windowSeconds * (0.45 + (0.55 * (rank + 1)) / wheelCount);
        w->fromDigit = std::fmod(std::floor((std::abs(x0) * finalEpoch.scale + 0.5) / fin.unit), spec.radix);
        const double toDigit = std::fmod(std::fmod(fin.lb, spec.radix) + spec.radix, spec.radix);
        w->turnsTotal = std::fmod(toDigit - w->fromDigit + spec.radix, spec.radix) +
                        spec.radix * (std::max(0.0, jsRound(fx.spins)) + rank);
        w->slotBack = backFor((0.3 * fx.bounce) / std::max(w->turnsTotal, 1.0));
        w->lockStep = std::max(1.0, std::floor(fx.rate * w->lockAt));

        auto slot = std::make_shared<TrackSlot>();
        slot->id = spec.id;
        slot->wheel = true;
        slot->sample = [w](double clock) { return w->sample(clock); };
        track.cells.push_back(slot);

        if (spec.hasAfter) {
            std::function<double(double)> pres = [](double) { return 1.0; };
            if (!counting) {
                if (spec.tie == WheelSpec::Wheel && w->finalGlyph.empty()) pres = [](double) { return 0.0; };
                if (spec.tie == WheelSpec::Decimal && finalEpoch.decimals == 0) pres = [](double) { return 0.0; };
            } else if (spec.tie == WheelSpec::Wheel) {
                const double delay = w->delay;
                auto presence = w->presence;
                pres = [presence, delay](double t) { return presence(t - delay); };
            } else if (spec.tie == WheelSpec::Decimal) {
                std::vector<Span> withDec;
                for (const Epoch& e : epochs) if (e.decimals > 0) withDec.push_back({e.ta, e.tb});
                pres = c.presenceOf(c.openEnds(withDec), !animated);
            }
            track.cells.push_back(charSlot(spec.id + "+", spec.afterChar, pres));
        }
    }

    /* ---- K / M suffix, ordinal suffix, sign ---- */
    if (tok.compact) {
        std::vector<std::string> names;
        for (const Epoch& e : epochs) {
            if (e.suffix.empty()) continue;
            if (std::find(names.begin(), names.end(), e.suffix) == names.end()) names.push_back(e.suffix);
        }
        for (const std::string& name : names) {
            std::function<double(double)> pres = [](double) { return 1.0; };
            if (counting) {
                std::vector<Span> sp;
                for (const Epoch& e : epochs) if (e.suffix == name) sp.push_back({e.ta, e.tb});
                pres = c.presenceOf(c.openEnds(sp), !animated);
            }
            track.cells.push_back(charSlot("suffix:" + name, name, pres));
        }
    }
    if (tok.ordinal) {
        auto slot = std::make_shared<TrackSlot>();
        slot->id = "ordinal";
        slot->wheel = false;
        slot->sample = [ctx](double t) {
            const TrackCtx& c = *ctx;
            const double lagged = c.counting && c.animated ? t - c.lag : t;
            SlotState s;
            s.out = ordinalSuffix(jsRound(std::abs(c.counting ? c.xAt(lagged) : c.x1)));
            s.in = "";
            s.p = 0;
            s.presence = 1;
            s.pos = 0;
            s.dir = 1;
            return s;
        };
        track.cells.push_back(slot);
    }

    if (tok.signAt >= 0 || x0 < 0 || x1 < 0) {
        std::vector<Span> negSpans;
        if (counting) {
            for (const Epoch& e : epochs) {
                if (!e.neg) continue;
                const double need = 0.5 / e.scale;
                if (std::min(e.ma, e.mb) >= need) negSpans.push_back({e.ta, e.tb});
                else if (std::max(e.ma, e.mb) >= need) {
                    const double cross = c.tOf(-need);
                    negSpans.push_back(e.mdir == 1 ? Span{cross, e.tb} : Span{e.ta, cross});
                }
            }
        } else if (x1 < 0) {
            negSpans.push_back({-INF, INF});
        }
        const std::vector<Span> neg = counting ? c.openEnds(negSpans) : negSpans;
        track.signs.push_back(charSlot("minus", tok.minusChar, c.presenceOf(neg, !animated || !counting)));
        if (tok.sign == '+') track.signs.push_back(charSlot("plus", "+", c.presenceOf(invertSpans(neg), !animated || !counting)));
    }

    track.settlesAt = counting ? T + (animated ? swap + std::max(0.0, cascade * (wheelCount - 1)) : 0)
                    : fx.mode != CounterMode::Scramble ? windowSeconds
                    : fx.rate > 0 ? std::max(windowSeconds, 1 / fx.rate) : INF;
    track.valueAt = [ctx](double t) {
        const TrackCtx& c = *ctx;
        return c.counting ? c.xAt(clamp(t, 0, c.T)) : (t >= c.windowSeconds ? c.x1 : c.x0);
    };
    return track;
}

} // namespace counter
} // namespace text
} // namespace openshot
