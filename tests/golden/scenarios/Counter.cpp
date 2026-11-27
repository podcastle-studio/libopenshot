// Animated number counters (src/text/counter) — engine parity with the TypeScript reference,
// the TS unit tests ported, and render scenarios through TextClipReader.
//
//   unit.counter_parse   parser + formatter against fixtures generated from parse.ts
//   unit.counter_parity  glyph lists of all 30 presets, all 20 templates and the odd formats,
//                        at ~58 times each, against evaluateLayer() of the TS engine (1e-6)
//   unit.counter_engine  the assertions of engine.test.ts (final text, start values, real value,
//                        K/M tiers, wheel continuity, speed limit, rest, templates)
//
// Fixtures: tests/golden/counter/fixtures.json, written by tools/counter-fixtures/generate.sh
// from the handoff's src/engine. Regenerate them when the TS engine changes, port the change to
// src/text/counter, and this scenario says whether the two still agree.

#include "Recipes.h"
#include "Timeline.h"
#include "Image.h"
#include "Clip.h"

#include "text/counter/CounterEngine.h"
#include "text/counter/CounterJson.h"

#include "Json.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace golden;
namespace C = openshot::text::counter;

namespace {

// The stub measurer of engine.test.ts / gen.ts: per codepoint, '1' = 6, spaces = 4, else 10.
class StubMeasurer : public C::Measurer {
public:
    explicit StubMeasurer(double fontSize) : size(fontSize) {}
    double width(const std::string& text) const override {
        double w = 0;
        for (char32_t c : C::toCodepoints(text)) w += c == U'1' ? 6 : (c == U' ' || c == 0xA0 || c == 0x202F) ? 4 : 10;
        return w;
    }
    double ascent() const override { return size * 0.9; }
    double descent() const override { return size * 0.25; }
    double digit() const override { return size * 0.72; }
private:
    double size;
};

void counterScene(Scene& s) { s.makeTimeline().Open(); }

std::string fixturesPath() { return std::string(GOLDEN_COUNTER_FIXTURES); }

bool loadFixtures(Json::Value& root, std::string& err) {
    std::ifstream in(fixturesPath());
    if (!in) { err = "cannot open " + fixturesPath(); return false; }
    Json::CharReaderBuilder b;
    if (!Json::parseFromStream(b, in, &root, &err)) return false;
    return true;
}

bool near(double a, double b, double tol = 1e-6) {
    if (std::isinf(a) || std::isinf(b) || std::abs(a) >= 1e299 || std::abs(b) >= 1e299) return (a > 0) == (b > 0) && std::abs(a) >= 1e299 == std::abs(b) >= 1e299 || a == b;
    return std::abs(a - b) <= tol * std::max(1.0, std::abs(b));
}

C::LayerInput inputFromCase(const Json::Value& c, std::vector<std::string>* warnings = nullptr) {
    C::LayerInput in;
    in.text = c["text"].asString();
    in.style.fontSize = c["style"]["fontSize"].asDouble();
    in.style.letterSpacing = c["style"]["letterSpacing"].asDouble();
    in.style.lineHeight = c["style"]["lineHeight"].asDouble();
    const std::string a = c["style"]["align"].asString();
    in.style.align = a == "left" ? C::Align::Left : a == "right" ? C::Align::Right : C::Align::Center;
    in.duration = c["duration"].asDouble();
    in.counter = C::counterFxFromJson(c["counter"], warnings);
    return in;
}

// What a viewer would read: every glyph that is mostly visible and unclipped, left to right.
std::string readFrame(const C::LayerFrame& f) {
    std::vector<const C::Glyph*> gs;
    for (const auto& g : f.glyphs) if (g.opacity > 0.5 && !g.hasClip) gs.push_back(&g);
    std::stable_sort(gs.begin(), gs.end(), [](const C::Glyph* a, const C::Glyph* b) { return a->y != b->y ? a->y < b->y : a->x < b->x; });
    std::string out;
    for (const auto* g : gs) out += g->text;
    return out;
}

C::LayerInput counterLayer(const std::string& text, const C::CounterFx& fx) {
    C::LayerInput in;
    in.text = text;
    in.style.fontSize = 180;
    in.style.letterSpacing = -0.02;
    in.style.lineHeight = 1.1;
    in.style.align = C::Align::Center;
    in.duration = 4;
    in.counter = fx;
    in.counter->range[0] = 0;
    in.counter->range[1] = 0.75;
    return in;
}

std::string readAt(const C::LayerInput& in, double t) {
    StubMeasurer m(in.style.fontSize);
    auto c = C::compileLayer(in, m);
    return readFrame(C::evaluateLayer(*c, t));
}

struct Tally {
    size_t total = 0, failed = 0;
    std::string first;
    void expect(bool ok, const std::string& what) {
        ++total;
        if (!ok && failed++ == 0) first = what;
    }
    Check check(const std::string& name) const {
        return {name, failed == 0, failed == 0 ? std::to_string(total) + " ok"
                                               : std::to_string(failed) + "/" + std::to_string(total) + " differ; first: " + first};
    }
};

std::string fmt(double v) { std::ostringstream o; o.precision(17); o << v; return o.str(); }

// ── unit.counter_parse ───────────────────────────────────────────────────────

void parseChecks(std::vector<Check>& checks) {
    Json::Value root;
    std::string err;
    if (!loadFixtures(root, err)) { checks.push_back({"fixtures", false, err}); return; }
    const Json::Value& values = root["values"];
    Tally tokens, body, fixed, rewrite;
    for (const Json::Value& entry : root["parse"]) {
        const std::string text = entry["text"].asString();
        const std::vector<C::NumberToken> got = C::parseText(text);
        tokens.expect(got.size() == entry["tokens"].size(), text + ": token count " + std::to_string(got.size()));
        for (Json::ArrayIndex k = 0; k < std::min<size_t>(got.size(), entry["tokens"].size()); ++k) {
            const Json::Value& w = entry["tokens"][k];
            const C::NumberToken& t = got[k];
            const std::string tag = text + " #" + std::to_string(k);
            tokens.expect(t.isTime == (w["kind"].asString() == "time"), tag + " kind");
            tokens.expect(t.start == w["start"].asInt() && t.end == w["end"].asInt(), tag + " span " + std::to_string(t.start) + ".." + std::to_string(t.end));
            tokens.expect(t.signAt == w["signAt"].asInt(), tag + " signAt");
            tokens.expect(std::string(t.sign ? std::string(1, t.sign) : "") == w["sign"].asString(), tag + " sign");
            tokens.expect(t.minusChar == w["minusChar"].asString(), tag + " minusChar");
            tokens.expect(near(t.value, w["value"].asDouble(), 1e-12), tag + " value " + fmt(t.value));
            tokens.expect(t.precisionLost == w["precisionLost"].asBool(), tag + " precisionLost");
            tokens.expect(t.decimals == w["decimals"].asInt(), tag + " decimals");
            tokens.expect(t.decimalSep == w["decimalSep"].asString(), tag + " decimalSep");
            tokens.expect(t.groupSep == w["groupSep"].asString(), tag + " groupSep");
            tokens.expect(t.minInt == w["minInt"].asInt(), tag + " minInt");
            tokens.expect(t.ordinal == w["ordinal"].asBool(), tag + " ordinal");
            tokens.expect(t.source && *t.source == w["source"].asString(), tag + " source");
            tokens.expect(t.compact.has_value() == !w["compact"].isNull(), tag + " compact");
            if (t.compact && !w["compact"].isNull()) {
                tokens.expect(t.compact->tier == w["compact"]["tier"].asInt(), tag + " tier");
                bool ladder = t.compact->ladder.size() == w["compact"]["ladder"].size();
                for (size_t i = 0; ladder && i < t.compact->ladder.size(); ++i)
                    ladder = t.compact->ladder[i] == w["compact"]["ladder"][static_cast<Json::ArrayIndex>(i)].asString();
                tokens.expect(ladder, tag + " ladder");
            }
            tokens.expect(t.time.has_value() == !w["time"].isNull(), tag + " time");
            if (t.time && !w["time"].isNull()) {
                tokens.expect(t.time->fields == w["time"]["fields"].asInt() && t.time->lead == w["time"]["lead"].asInt() &&
                              t.time->frac == w["time"]["frac"].asInt() && t.time->fracSep == w["time"]["fracSep"].asString(), tag + " time fields");
            }
            for (Json::ArrayIndex v = 0; v < values.size(); ++v) {
                const double x = values[v].asDouble();
                const std::string b = C::formatBody(t, x);
                body.expect(b == w["body"][v].asString(), tag + " formatBody(" + fmt(x) + ") = '" + b + "' want '" + w["body"][v].asString() + "'");
                const std::string f = C::formatBody(t, x, false);
                fixed.expect(f == w["fixed"][v].asString(), tag + " formatBody(" + fmt(x) + ", fixed) = '" + f + "' want '" + w["fixed"][v].asString() + "'");
                const std::string r = C::rewriteToken(text, t, x);
                rewrite.expect(r == w["rewrite"][v].asString(), tag + " rewrite(" + fmt(x) + ") = '" + r + "' want '" + w["rewrite"][v].asString() + "'");
            }
        }
    }
    checks.push_back(tokens.check("tokens"));
    checks.push_back(body.check("format_scaled"));
    checks.push_back(fixed.check("format_fixed"));
    checks.push_back(rewrite.check("rewrite"));
}

// ── unit.counter_parity ──────────────────────────────────────────────────────

void parityChecks(std::vector<Check>& checks) {
    Json::Value root;
    std::string err;
    if (!loadFixtures(root, err)) { checks.push_back({"fixtures", false, err}); return; }
    std::map<std::string, Tally> groups;   // preset / template / format
    size_t glyphs = 0;
    for (const Json::Value& c : root["cases"]) {
        const std::string name = c["name"].asString();
        const std::string group = name.substr(0, name.find(':'));
        Tally& tally = groups[group];
        std::vector<std::string> warnings;
        const C::LayerInput in = inputFromCase(c, &warnings);
        tally.expect(warnings.empty(), name + ": json warning " + (warnings.empty() ? "" : warnings[0]));
        StubMeasurer m(in.style.fontSize);
        auto compiled = C::compileLayer(in, m);
        tally.expect(C::trackCount(*compiled) == c["tracks"].asUInt(), name + ": track count");
        for (const Json::Value& s : c["samples"]) {
            const double t = s["t"].asDouble(), ft = s["ft"].asDouble();
            const C::LayerFrame f = C::evaluateLayer(*compiled, t, ft);
            const std::string at = name + " @" + fmt(t) + "/" + fmt(ft);
            const Json::Value& want = s["g"];
            if (f.glyphs.size() != want.size()) {
                tally.expect(false, at + ": " + std::to_string(f.glyphs.size()) + " glyphs, want " + std::to_string(want.size()));
                continue;
            }
            for (Json::ArrayIndex i = 0; i < want.size(); ++i) {
                const C::Glyph& g = f.glyphs[i];
                const Json::Value& w = want[i];
                ++glyphs;
                bool ok = g.text == w[0].asString() && near(g.x, w[1].asDouble()) && near(g.y, w[2].asDouble()) &&
                          near(g.w, w[3].asDouble()) && g.run == (w[4].asInt() == 1) && near(g.dy, w[5].asDouble()) &&
                          near(g.sx, w[6].asDouble()) && near(g.sy, w[7].asDouble()) && near(g.pivot, w[8].asDouble()) &&
                          near(g.opacity, w[9].asDouble()) && near(g.blur, w[10].asDouble()) &&
                          g.hasClip == !w[11].isNull() && near(g.feather, w[13].asDouble());
                if (ok && g.hasClip) ok = near(g.clip0, w[11].asDouble()) && near(g.clip1, w[12].asDouble());
                std::ostringstream d;
                if (!ok) {
                    d.precision(10);
                    d << at << " glyph " << i << ": '" << g.text << "' x=" << g.x << " dy=" << g.dy << " op=" << g.opacity
                      << " sy=" << g.sy << " | want '" << w[0].asString() << "' x=" << w[1].asDouble() << " dy=" << w[5].asDouble()
                      << " op=" << w[9].asDouble() << " sy=" << w[7].asDouble();
                }
                tally.expect(ok, d.str());
            }
            const double b[4] = {f.bounds.x, f.bounds.y, f.bounds.w, f.bounds.h};
            const double xf[7] = {f.xf.dx, f.xf.dy, f.xf.scale, f.xf.px, f.xf.py, f.xf.opacity, f.xf.blur};
            bool ok = true;
            for (int k = 0; k < 4; ++k) ok = ok && near(b[k], s["b"][k].asDouble());
            for (int k = 0; k < 7; ++k) ok = ok && near(xf[k], s["xf"][k].asDouble());
            tally.expect(ok, at + ": bounds / layer transform (scale " + fmt(f.xf.scale) + " want " + fmt(s["xf"][2].asDouble()) + ")");
            ok = f.pose.size() == s["pose"].size();
            for (Json::ArrayIndex k = 0; ok && k < s["pose"].size(); ++k) ok = near(f.pose[k], s["pose"][k].asDouble());
            tally.expect(ok, at + ": pose");
        }
    }
    for (const auto& [group, tally] : groups) checks.push_back(tally.check(group));
    checks.push_back({"glyphs_compared", glyphs > 10000, std::to_string(glyphs)});
}

// ── unit.counter_engine (engine.test.ts) ─────────────────────────────────────

std::string withCommas(double v) {
    std::string d = C::jsIntegerString(std::abs(v));
    std::string out;
    for (size_t k = 0; k < d.size(); ++k) {
        if (k > 0 && (d.size() - k) % 3 == 0) out += ',';
        out += d[k];
    }
    return (v < 0 ? "-" : "") + out;
}

void engineChecks(std::vector<Check>& checks) {
    const std::vector<std::string> texts = {"$1,250,000", "87.5%", "1.2M", "850K", "01:30", "1:02:03.5", "4.8/5", "1st",
                                            "+12.4%", "1 250 000 \xE2\x82\xBD", "007", "10,000+ users", "2026"};
    {   // every preset ends on exactly the typed text
        Tally t;
        for (const C::Preset& p : C::presets()) {
            for (const std::string& text : texts) {
                const C::LayerInput in = counterLayer(text, C::applyPreset(nullptr, p));
                const std::string a = readAt(in, 3.0001), b = readAt(in, 4);
                t.expect(a == text, p.id + " / " + text + " = '" + a + "'");
                t.expect(b == text, p.id + " / " + text + " at clip end = '" + b + "'");
            }
        }
        checks.push_back(t.check("presets_land_on_text"));
    }
    {   // starts on the start value
        Tally t;
        auto fx = [](std::function<void(C::CounterFx&)> f = {}, std::optional<double> from = std::nullopt) {
            C::CounterFx x = C::defaultFx();
            if (f) f(x);
            if (from) x.from[0] = *from;
            return x;
        };
        const std::vector<std::pair<C::LayerInput, std::string>> cases = {
            {counterLayer("$1,250,000", fx()), "$0"},
            {counterLayer("87.5%", fx()), "0.0%"},
            {counterLayer("1.2M", fx()), "0"},
            {counterLayer("1.2M", fx([](C::CounterFx& f) { f.compactScale = false; })), "0.0M"},
            {counterLayer("01:30", fx()), "00:00"},
            {counterLayer("2026", fx({}, 1990)), "1990"},
            {counterLayer("0", fx({}, 10)), "10"},
            {counterLayer("+24\xC2\xB0" "C", fx({}, -8)), "\xE2\x88\x92" "8\xC2\xB0" "C"},
            {counterLayer("1st", fx({}, 24)), "24th"},
        };
        for (const auto& [in, want] : cases) {
            const std::string got = readAt(in, 0);
            t.expect(got == want, in.text + " at 0 = '" + got + "' want '" + want + "'");
        }
        checks.push_back(t.check("starts_on_from"));
    }
    {   // unsupported amounts stay as typed, other targets still animate
        Tally t;
        const std::string amount = "\xE2\x88\x92$9,007,199,254,740,993.50";
        C::CounterFx f = C::defaultFx();
        f.targets = {0, 1};
        f.from[0] = -9007199254740990.0;
        const C::LayerInput in = counterLayer(amount + " \xC2\xB7 42", f);
        t.expect(readAt(in, 0) == amount + " \xC2\xB7 0", "static amount at 0: '" + readAt(in, 0) + "'");
        t.expect(readAt(in, 4) == in.text, "static amount at end: '" + readAt(in, 4) + "'");
        C::CounterFx g = C::defaultFx();
        g.maxSpeed = 0;
        const C::LayerInput over = counterLayer("0.12345678901234567890%", g);
        t.expect(readAt(over, 1.25) == over.text, "overprecise");
        C::CounterFx h = C::defaultFx();
        h.from[0] = 1.7976931348623157e308;
        const C::LayerInput big = counterLayer("\xE2\x82\xAC" "12.50", h);
        t.expect(readAt(big, 0) == big.text, "oversized start: '" + readAt(big, 0) + "'");
        checks.push_back(t.check("static_amounts"));
    }
    {   // plain count always shows the real value (needs the track's valueAt: rebuild it)
        Tally t;
        C::CounterFx f = C::defaultFx();
        f.style = C::DigitStyle::None;
        f.easing = *C::easingByName("easeOut");
        const C::LayerInput in = counterLayer("$1,250,000", f);
        const C::NumberToken tok = C::parseText(in.text)[0];
        const C::Track track = C::buildTrack(tok, 0, *in.counter, 3);
        for (int i = 0; i <= 60; i++) {
            const double tt = (i / 60.0) * 3;
            const std::string want = "$" + withCommas(C::jsRound(track.valueAt(tt)));
            const std::string got = readAt(in, tt);
            t.expect(got == want, "at " + fmt(tt) + " '" + got + "' want '" + want + "'");
        }
        checks.push_back(t.check("count_is_real_value"));
    }
    {   // K/M scaling passes through the tiers
        C::CounterFx f = C::defaultFx();
        f.style = C::DigitStyle::None;
        f.easing = *C::easingByName("linear");
        const C::LayerInput in = counterLayer("1.2M", f);
        StubMeasurer m(in.style.fontSize);
        auto c = C::compileLayer(in, m);
        std::vector<std::string> seen;
        for (int i = 0; i <= 3000; i++) {
            std::string r = readFrame(C::evaluateLayer(*c, (i / 3000.0) * 3));
            r.erase(std::remove_if(r.begin(), r.end(), [](char ch) { return (ch >= '0' && ch <= '9') || ch == '.'; }), r.end());
            if (std::find(seen.begin(), seen.end(), r) == seen.end()) seen.push_back(r);
        }
        const bool ok = seen == std::vector<std::string>{"", "K", "M"};
        std::string s;
        for (auto& x : seen) s += "[" + x + "]";
        checks.push_back({"compact_tiers", ok, s});
    }
    const std::vector<std::string> easings = {"linear", "easeOut", "smoothOut", "strongOut", "easeInOut", "softInOut", "easeIn", "lateRush", "quickStart"};
    {   // what a wheel shows never teleports
        Tally t;
        const std::vector<std::pair<std::string, double>> cases = {{"$1,250,000", 0}, {"87.5%", 0}, {"1.2M", 0}, {"0", 250}, {"01:30", 0}, {"+24\xC2\xB0" "C", -8}};
        for (const auto& [text, from] : cases) {
            for (const std::string& e : easings) {
                C::CounterFx f = C::defaultFx();
                f.easing = *C::easingByName(e);
                f.maxSpeed = 16;
                f.bounce = 0;
                f.from[0] = from;
                f.range[0] = 0;
                f.range[1] = 0.75;
                const C::Track track = C::buildTrack(C::parseText(text)[0], from, f, 3);
                const double dt = 1.0 / 960;
                for (const auto& wheel : track.cells) {
                    if (!wheel->wheel) continue;
                    C::SlotState prev = wheel->sample(0);
                    double worst = 0;
                    double worstT = 0;
                    for (double tt = dt; tt <= 3.2; tt += dt) {
                        const C::SlotState now = wheel->sample(tt);
                        double moved = 1e9;
                        if (now.out == prev.out && now.in == prev.in) moved = std::abs(now.p - prev.p);
                        else if (now.out == prev.in) moved = 1 - prev.p + now.p;
                        if (moved > worst) { worst = moved; worstT = tt; }
                        prev = now;
                    }
                    t.expect(worst < 0.3, text + " " + e + " " + wheel->id + " jumps " + fmt(worst) + " at " + fmt(worstT));
                }
            }
        }
        checks.push_back(t.check("wheel_continuity"));
    }
    {   // long fast counts keep every wheel near the speed limit
        Tally t;
        for (const std::string& e : easings) {
            C::CounterFx f = C::defaultFx();
            f.easing = *C::easingByName(e);
            f.maxSpeed = 16;
            f.bounce = 0;
            const C::Track track = C::buildTrack(C::parseText("$1,250,000")[0], 0, f, 3);
            const double dt = 1.0 / 240;
            int wheelIndex = 0;
            for (const auto& wheel : track.cells) {
                if (!wheel->wheel) continue;
                if (wheelIndex++ < 2) continue;
                C::SlotState prev = wheel->sample(0);
                double worst = 0;
                for (double tt = dt; tt <= 3.2; tt += dt) {
                    const C::SlotState now = wheel->sample(tt);
                    const double a = (now.out.empty() ? 0 : std::stod(now.out)) + now.p;
                    const double b = (prev.out.empty() ? 0 : std::stod(prev.out)) + prev.p;
                    const double up = std::fmod(std::fmod(a - b, 10) + 10, 10);
                    worst = std::max(worst, std::min(up, 10 - up) / dt);
                    prev = now;
                }
                t.expect(worst < 16 * 1.9, e + " " + wheel->id + " " + fmt(worst) + " digits/s");
            }
        }
        checks.push_back(t.check("speed_limit"));
    }
    {   // at rest outside the count window
        Tally t;
        C::CounterFx f = C::defaultFx();
        f.bounce = 1;
        f.range[0] = 0;
        f.range[1] = 0.75;
        const C::Track track = C::buildTrack(C::parseText("$1,250,000")[0], 0, f, 3);
        for (double tt : {0.0, 3.0, 3.5, 4.0}) for (const auto& slot : track.cells) t.expect(slot->sample(tt).p == 0, slot->id + " at " + fmt(tt));
        checks.push_back(t.check("rest_outside_window"));
    }
    {   // templates build and land on their text
        Tally t;
        for (const C::Template& tpl : C::templates()) {
            C::LayerInput in;
            in.text = tpl.text;
            in.style.fontSize = 180;
            in.style.letterSpacing = -0.02;
            in.duration = 4;
            in.counter = C::fxFromTemplate(tpl);
            const std::string got = readAt(in, 4);
            t.expect(got == tpl.text, tpl.id + " = '" + got + "'");
        }
        checks.push_back(t.check("templates_land"));
    }
    {   // payload JSON: defaults, preset look, explicit overrides, junk tolerated
        Tally t;
        Json::Value v = openshot::stringToJson(R"({"preset":"flip","swap":0.5,"from":{"0":10},"range":[0.1,0.9],"style":"bogus","easing":"linear"})");
        std::vector<std::string> w;
        const auto fx = C::counterFxFromJson(v, &w);
        t.expect(fx.has_value(), "parsed");
        t.expect(fx && fx->style == C::DigitStyle::Flip && fx->swap == 0.5 && fx->maxSpeed == 9 && fx->cascade == 0.03, "preset look + override");
        t.expect(fx && fx->from.at(0) == 10 && fx->range[0] == 0.1 && fx->easing.x2 == 1, "from / range / named easing");
        t.expect(w.size() == 1, "one warning for the bogus style (" + std::to_string(w.size()) + ")");
        t.expect(!C::counterFxFromJson(Json::Value()).has_value(), "null = plain text");
        const auto round = C::counterFxFromJson(C::counterFxToJson(*fx));
        t.expect(round && C::counterFxToJson(*round) == C::counterFxToJson(*fx), "round trip");
        checks.push_back(t.check("payload_json"));
    }
}

// ── render scenarios ─────────────────────────────────────────────────────────

using namespace golden::recipes;

C::CounterFx presetFx(const std::string& id, std::function<void(C::CounterFx&)> patch = {}) {
    C::CounterFx fx = C::applyPreset(nullptr, *C::presetById(id));
    if (patch) patch(fx);
    return fx;
}

TextSpec counterSpec(Scene& s, const std::string& value, const C::CounterFx& fx, double size = 22.0) {
    TextSpec t;
    t.value = value;
    t.style = baseStyle(s.font("NotoSans-Bold.ttf"));
    t.style.fontWeight = 700;
    t.transformation.size = size;
    t.start = 0;
    t.end = 3;
    t.counter = fx;
    return t;
}

void counterOne(Scene& s, const TextSpec& t) {
    auto& tl = s.makeTimeline();
    tl.AddClip(backgroundClip(s, s.media("background_960x540.png")));
    tl.AddClip(textClip(s, t, s.fps.ToDouble()));
    tl.Open();
}

// The reader of the scene's text clip (the last clip added).
openshot::TextClipReader* counterReader(Scene& s) {
    for (auto it = s.readers.rbegin(); it != s.readers.rend(); ++it)
        if (auto* r = dynamic_cast<openshot::TextClipReader*>(*it)) return r;
    return nullptr;
}

// A counter clip's last frame must read exactly as the typed text. With proportional digits and
// no letter spacing its glyphs land exactly where the static text puts them, so the last frame of
// the counter is compared with the same clip rendered without one.
void finalFrameChecks(Scene&, std::vector<Captured>& captured, std::vector<Check>& checks) {
    struct Case { std::string name, value; std::string preset; openshot::text::TextAlignment align; double maxWidth; };
    const std::vector<Case> cases = {
        {"money_center", "$1,250,000", "smooth-roll", openshot::text::TextAlignment::CENTER, 0},
        {"label_left", "Revenue $1,250,000", "flip", openshot::text::TextAlignment::LEFT, 0},
        {"wrapped_right", "We raised $4,815,162 from 1,024 backers", "slot", openshot::text::TextAlignment::RIGHT, 14},
        {"clock", "1:24.50", "decode", openshot::text::TextAlignment::CENTER, 0},
    };
    for (const Case& c : cases) {
        Image frames[2];
        std::vector<std::string> lines;
        bool hasCounter = false;
        for (int k = 0; k < 2; ++k) {
            Scene s;
            s.width = 960; s.height = 540; s.fps = openshot::Fraction(30, 1);
            s.mediaDir = GOLDEN_MEDIA_DIR;
            TextSpec t = counterSpec(s, c.value, presetFx(c.preset, [](C::CounterFx& f) { f.tabular = false; }), 16.0);
            t.style.textAlign = c.align;
            t.transformation.maxWidth = c.maxWidth;
            if (k == 1) t.counter.reset();
            counterOne(s, t);
            if (k == 0) {
                auto* r = counterReader(s);
                hasCounter = r && r->HasCounter();
            }
            frames[k] = fromFrame(s.timeline->GetFrame(90));
        }
        const Metrics m = compare(frames[1], frames[0]);
        checks.push_back({c.name + "_has_counter", hasCounter, hasCounter ? "counter active" : "counter not applied"});
        checks.push_back({c.name + "_last_frame_is_text", m.psnr >= 45.0, "PSNR vs static text " + fmt(m.psnr) + " dB"});
        captured.push_back({"final_" + c.name, frames[0]});
        if (m.psnr < 45.0) {   // show what differs: the static render and an amplified diff
            captured.push_back({"static_" + c.name, frames[1]});
            captured.push_back({"diff_" + c.name, diffHeatmap(frames[1], frames[0])});
        }
    }
}

} // namespace

namespace golden {

void registerCounterScenarios() {
    addCustom("unit.counter_parse", {"unit", "counter"}, counterScene,
              [](Scene&, std::vector<Captured>&, std::vector<Check>& checks) { parseChecks(checks); });
    addCustom("unit.counter_parity", {"unit", "counter"}, counterScene,
              [](Scene&, std::vector<Captured>&, std::vector<Check>& checks) { parityChecks(checks); });
    addCustom("unit.counter_engine", {"unit", "counter"}, counterScene,
              [](Scene&, std::vector<Captured>&, std::vector<Check>& checks) { engineChecks(checks); });

    const Tolerance text = Tolerance::Loose();
    const std::vector<int64_t> F = {1, 12, 24, 40, 60, 90};
    using openshot::text::TextAlignment;
    using openshot::text::TextAnimations;

    add("text.counter_roll", {"text", "counter"}, F, [](Scene& s) {
        counterOne(s, counterSpec(s, "$1,250,000", presetFx("smooth-roll")));
    }, text);
    add("text.counter_odometer", {"text", "counter"}, F, [](Scene& s) {
        counterOne(s, counterSpec(s, "2026", presetFx("odometer", [](C::CounterFx& f) { f.from[0] = 1990; })));
    }, text);
    add("text.counter_flip", {"text", "counter"}, F, [](Scene& s) {
        counterOne(s, counterSpec(s, "00:30", presetFx("flip-clock", [](C::CounterFx& f) { f.from[0] = 90; })));
    }, text);
    add("text.counter_slot", {"text", "counter"}, F, [](Scene& s) {
        counterOne(s, counterSpec(s, "777", presetFx("jackpot")));
    }, text);
    add("text.counter_scramble", {"text", "counter"}, F, [](Scene& s) {
        counterOne(s, counterSpec(s, "4815", presetFx("decode")));
    }, text);
    add("text.counter_turbo_shutter", {"text", "counter"}, F, [](Scene& s) {
        counterOne(s, counterSpec(s, "1.2M followers", presetFx("turbo")));
    }, text);
    add("text.counter_pop_grow", {"text", "counter"}, F, [](Scene& s) {
        counterOne(s, counterSpec(s, "+12.4%", presetFx("grow", [](C::CounterFx& f) { f.from[0] = -3.2; })));
    }, text);
    add("text.counter_stroke_shadow", {"text", "counter", "style"}, F, [](Scene& s) {
        TextSpec t = counterSpec(s, "\xE2\x82\xAC" "49.99", presetFx("bouncy-roll"));
        t.style.color = "linear-gradient(180deg, #ffe259 0%, #ff7a18 100%)";
        t.style.strokeColor = "#1b1b3a";
        t.style.strokeWidthRatio = 0.06;
        t.style.shadowColor = "rgba(0,0,0,0.6)";
        t.style.shadowBlurRatio = 0.08;
        t.style.shadowDistanceRatio = 0.08;
        t.style.shadowAngle = 45;
        t.currency = "EUR";
        counterOne(s, t);
    }, text);
    add("text.counter_align_left", {"text", "counter", "layout"}, F, [](Scene& s) {
        TextSpec t = counterSpec(s, "Revenue $1,250,000\nup 87.5% in 2026", presetFx("cascade", [](C::CounterFx& f) {
            f.targets = {0, 1, 2}; f.from[2] = 1990; }), 12.0);
        t.style.textAlign = TextAlignment::LEFT;
        t.posX = 0.1f;
        counterOne(s, t);
    }, text);
    add("text.counter_align_right", {"text", "counter", "layout"}, F, [](Scene& s) {
        TextSpec t = counterSpec(s, "Countdown 10", presetFx("pop", [](C::CounterFx& f) { f.from[0] = 250; }), 16.0);
        t.style.textAlign = TextAlignment::RIGHT;
        t.posX = 0.9f;
        counterOne(s, t);
    }, text);
    add("text.counter_in_animation_chars", {"text", "counter", "animation"}, F, [](Scene& s) {
        TextSpec t = counterSpec(s, "Users 10,000+", presetFx("drop"), 16.0);
        TextAnimations a; a.inAnimationId = "rise-chars"; a.inAnimationDuration = 1.2;
        t.animations = a;
        counterOne(s, t);
    }, text);
    add("text.counter_in_animation_block", {"text", "counter", "animation"}, F, [](Scene& s) {
        TextSpec t = counterSpec(s, "850K views", presetFx("slide"), 18.0);
        // Not hyper-zoom: its frames after the zoom sit ~1 px apart on GPU and CPU, with or without
        // a counter (pre-existing; doc/GPU-RENDERING.md, known issues).
        TextAnimations a; a.inAnimationId = "fade-in"; a.inAnimationDuration = 1.0;
        a.loopAnimationId = "pulse"; a.loopAnimationDuration = 1.0;
        t.animations = a;
        counterOne(s, t);
    }, text);
    add("text.counter_glow_background", {"text", "counter", "style"}, F, [](Scene& s) {
        TextSpec t = counterSpec(s, "42.195 km", presetFx("drum"), 18.0);
        t.style.backgroundColor = "rgba(20,20,40,0.85)";
        t.style.backgroundPaddingXRatio = 0.4;
        t.style.backgroundPaddingYRatio = 0.25;
        t.style.backgroundRadiusRatio = 0.5;
        t.style.glowColor = "#38bdf8";
        t.style.glowIntensityRatio = 0.8;
        t.style.glowRangeRatio = 0.3;
        counterOne(s, t);
    }, text);
    add("text.counter_tilt", {"text", "counter", "style"}, F, [](Scene& s) {
        TextSpec t = counterSpec(s, "1st", presetFx("zoom", [](C::CounterFx& f) { f.from[0] = 24; }), 24.0);
        t.transformation.tiltX = 20;
        t.transformation.tiltY = -25;
        counterOne(s, t);
    }, text);
    addCustom("unit.counter_final_text", {"unit", "counter"}, counterScene, finalFrameChecks, text);
}

} // namespace golden
