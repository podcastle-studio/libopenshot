// Port of presets.ts (async-counters-handoff-v4): DEFAULT_FX, the 30 presets, the 20 templates.
// Presets are data: a partial CounterFx laid over the defaults.

#include "CounterEngine.h"

namespace openshot {
namespace text {
namespace counter {

namespace {
const Bezier LINEAR{0, 0, 1, 1};
const Bezier EASE_OUT{0.22, 0.61, 0.36, 1};
const Bezier SMOOTH_OUT{0.16, 1, 0.3, 1};
const Bezier STRONG_OUT{0.05, 0.9, 0.1, 1};
const Bezier EASE_IN_OUT{0.65, 0, 0.35, 1};
const Bezier SOFT_IN_OUT{0.45, 0, 0.55, 1};
const Bezier EASE_IN{0.55, 0, 1, 0.45};
} // namespace

CounterFx defaultFx() { return CounterFx{}; }

const std::vector<Preset>& presets() {
    using S = DigitStyle;
    using M = CounterMode;
    static const std::vector<Preset> list = {
        /* classic */
        {"count", "classic", [](CounterFx& f) { f.style = S::None; f.easing = EASE_OUT; f.shutter = 0; }},
        {"linear", "classic", [](CounterFx& f) { f.style = S::None; f.easing = LINEAR; f.shutter = 0; }},
        {"stop-motion", "classic", [](CounterFx& f) { f.style = S::None; f.easing = EASE_OUT; f.stepFps = 10; f.shutter = 0; }},
        {"punch", "classic", [](CounterFx& f) { f.style = S::None; f.easing = EASE_IN; f.landPop = 1; f.shutter = 180; }},
        {"grow", "classic", [](CounterFx& f) { f.style = S::None; f.easing = SMOOTH_OUT; f.grow = 0.7; f.landPop = 0.5; f.shutter = 180; }},
        /* roll */
        {"smooth-roll", "roll", [](CounterFx&) {}},
        {"odometer", "roll", [](CounterFx& f) { f.clip = true; f.soft = true; f.swap = 0.44; f.maxSpeed = 9; f.easing = EASE_IN_OUT; f.distance = 1.08; f.cascade = 0; }},
        {"bouncy-roll", "roll", [](CounterFx& f) { f.bounce = 1; f.swap = 0.5; }},
        {"cascade", "roll", [](CounterFx& f) { f.cascade = 0.07; f.swap = 0.42; f.bounce = 0.45; }},
        {"turbo", "roll", [](CounterFx& f) { f.maxSpeed = 26; f.shutter = 360; f.swap = 0.26; f.easing = STRONG_OUT; }},
        {"zipper", "roll", [](CounterFx& f) { f.direction = Direction::Alternate; f.swap = 0.34; }},
        {"drop", "roll", [](CounterFx& f) { f.direction = Direction::Down; f.bounce = 0.85; f.swap = 0.46; }},
        {"drum", "roll", [](CounterFx& f) { f.style = S::Drum; f.swap = 0.42; f.maxSpeed = 11; f.distance = 1.1; }},
        {"combination", "roll", [](CounterFx& f) { f.style = S::Drum; f.soft = true; f.clip = true; f.cascade = 0.06; f.swap = 0.5; f.maxSpeed = 8; f.distance = 1.1; }},
        /* slide */
        {"slide", "slide", [](CounterFx& f) { f.style = S::Slide; f.swap = 0.3; f.maxSpeed = 10; f.shutter = 120; }},
        {"spring", "slide", [](CounterFx& f) { f.style = S::Slide; f.bounce = 1; f.swap = 0.44; f.maxSpeed = 9; f.shutter = 120; }},
        {"fall", "slide", [](CounterFx& f) { f.style = S::Slide; f.direction = Direction::Down; f.swap = 0.32; f.maxSpeed = 10; f.shutter = 120; }},
        /* soft */
        {"crossfade", "soft", [](CounterFx& f) { f.style = S::Fade; f.swap = 0.28; f.maxSpeed = 10; f.shutter = 0; f.cascade = 0; }},
        {"focus", "soft", [](CounterFx& f) { f.style = S::Blur; f.swap = 0.34; f.maxSpeed = 10; f.shutter = 0; }},
        {"dream", "soft", [](CounterFx& f) { f.style = S::Blur; f.soft = true; f.swap = 0.55; f.maxSpeed = 6; f.easing = SOFT_IN_OUT; f.shutter = 0; }},
        /* pop */
        {"pop", "pop", [](CounterFx& f) { f.style = S::Pop; f.bounce = 1; f.swap = 0.32; f.maxSpeed = 10; f.landPop = 0.6; f.shutter = 120; }},
        {"zoom", "pop", [](CounterFx& f) { f.style = S::Zoom; f.swap = 0.34; f.maxSpeed = 9; f.shutter = 180; }},
        /* flip */
        {"flip", "flip", [](CounterFx& f) { f.style = S::Flip; f.swap = 0.36; f.maxSpeed = 9; f.shutter = 90; f.cascade = 0.03; }},
        {"flip-clock", "flip", [](CounterFx& f) { f.style = S::Flip; f.soft = true; f.swap = 0.46; f.maxSpeed = 5; f.easing = LINEAR; f.shutter = 90; f.cascade = 0; }},
        /* slot */
        {"slot", "slot", [](CounterFx& f) { f.mode = M::Slot; f.style = S::Roll; f.clip = true; f.spins = 2; f.bounce = 0.8; f.shutter = 220; f.order = Order::Ltr; }},
        {"jackpot", "slot", [](CounterFx& f) { f.mode = M::Slot; f.style = S::Roll; f.spins = 4; f.bounce = 1; f.shutter = 360; f.order = Order::Rtl; f.landPop = 0.9; }},
        {"lottery", "slot", [](CounterFx& f) { f.mode = M::Slot; f.style = S::Drum; f.spins = 3; f.bounce = 0.6; f.shutter = 220; f.order = Order::Random; f.distance = 1.1; }},
        /* scramble */
        {"decode", "scramble", [](CounterFx& f) { f.mode = M::Scramble; f.style = S::None; f.rate = 22; f.order = Order::Ltr; f.shutter = 0; }},
        {"shuffle", "scramble", [](CounterFx& f) { f.mode = M::Scramble; f.style = S::Slide; f.rate = 12; f.order = Order::Random; f.shutter = 120; }},
        {"departures", "scramble", [](CounterFx& f) { f.mode = M::Scramble; f.style = S::Flip; f.rate = 9; f.order = Order::Ltr; f.shutter = 90; }},
    };
    return list;
}

const Preset* presetById(const std::string& id) {
    for (const Preset& p : presets()) if (p.id == id) return &p;
    return nullptr;
}

CounterFx applyPreset(const CounterFx* current, const Preset& preset) {
    CounterFx base = current ? *current : defaultFx();
    if (!current) base.from.clear();
    // LOOK keys back to DEFAULT_FX
    const CounterFx d = defaultFx();
    base.easing = d.easing; base.mode = d.mode; base.style = d.style; base.direction = d.direction;
    base.swap = d.swap; base.soft = d.soft; base.bounce = d.bounce; base.maxSpeed = d.maxSpeed;
    base.cascade = d.cascade; base.distance = d.distance; base.clip = d.clip; base.stepFps = d.stepFps;
    base.shutter = d.shutter; base.landPop = d.landPop; base.grow = d.grow; base.spins = d.spins;
    base.order = d.order; base.rate = d.rate;
    preset.apply(base);
    base.preset = preset.id;
    return base;
}

const std::vector<Template>& templates() {
    static const std::vector<Template> list = {
        {"money", "$1,250,000", std::nullopt, "smooth-roll", {}, std::string("USD"), std::nullopt},
        {"budget", "1\xC2\xA0" "250\xC2\xA0" "000\xC2\xA0\xE2\x82\xAC", std::nullopt, "odometer", {}, std::string("EUR"), std::nullopt},
        {"price", "\xE2\x82\xAC" "49.99", std::nullopt, "slide", {}, std::string("EUR"), std::nullopt},
        {"percent", "87.5%", std::nullopt, "slide", {}, std::nullopt, std::nullopt},
        {"delta", "+12.4%", -3.2, "spring", {}, std::nullopt, std::nullopt},
        {"followers", "1.2M", std::nullopt, "bouncy-roll", [](CounterFx& f) { f.easing = EASE_IN_OUT; }, std::nullopt, std::nullopt},
        {"views", "850K", std::nullopt, "smooth-roll", [](CounterFx& f) { f.easing = EASE_IN_OUT; }, std::nullopt, std::nullopt},
        {"users", "10,000+", std::nullopt, "cascade", {}, std::nullopt, std::nullopt},
        {"rating", "4.8/5", std::nullopt, "pop", {}, std::nullopt, std::nullopt},
        {"multiplier", "\xC3\x97" "10", 1.0, "pop", {}, std::nullopt, std::nullopt},
        {"fraction", "3/4", std::nullopt, "slide", {}, std::nullopt, std::nullopt},
        {"timer", "00:30", std::nullopt, "flip-clock", {}, std::nullopt, std::string("JetBrains Mono Variable")},
        {"stopwatch", "1:24.50", std::nullopt, "linear", {}, std::nullopt, std::string("JetBrains Mono Variable")},
        {"countdown", "0", 10.0, "pop", [](CounterFx& f) { f.easing = LINEAR; }, std::nullopt, std::nullopt},
        {"temperature", "+24\xC2\xB0" "C", -8.0, "crossfade", {}, std::nullopt, std::nullopt},
        {"year", "2026", 1990.0, "odometer", {}, std::nullopt, std::nullopt},
        {"place", "1st", 24.0, "drop", {}, std::nullopt, std::nullopt},
        {"distance", "42.195 km", std::nullopt, "smooth-roll", {}, std::nullopt, std::nullopt},
        {"jackpot", "777", std::nullopt, "jackpot", {}, std::nullopt, std::nullopt},
        {"code", "4815", std::nullopt, "decode", {}, std::nullopt, std::string("JetBrains Mono Variable")},
    };
    return list;
}

CounterFx fxFromTemplate(const Template& tpl) {
    const Preset* preset = presetById(tpl.preset);
    CounterFx fx = applyPreset(nullptr, preset ? *preset : presets()[0]);
    if (tpl.fx) tpl.fx(fx);
    fx.from.clear();
    if (tpl.from) fx.from[0] = *tpl.from;
    return fx;
}

} // namespace counter
} // namespace text
} // namespace openshot
