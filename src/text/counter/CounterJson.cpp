#include "CounterJson.h"

#include <cmath>
#include <cstdlib>
#include <memory>

namespace openshot {
namespace text {
namespace counter {

namespace {

void warn(std::vector<std::string>* w, const std::string& msg) {
    if (w) w->push_back(msg);
}

bool num(const Json::Value& v, const char* key, double& out, std::vector<std::string>* w) {
    const Json::Value& x = v[key];
    if (x.isNull()) return false;
    if (!x.isNumeric() || !std::isfinite(x.asDouble())) {
        warn(w, std::string("counter.") + key + " is not a finite number; default kept");
        return false;
    }
    out = x.asDouble();
    return true;
}

bool boolean(const Json::Value& v, const char* key, bool& out, std::vector<std::string>* w) {
    const Json::Value& x = v[key];
    if (x.isNull()) return false;
    if (!x.isBool()) {
        warn(w, std::string("counter.") + key + " is not a boolean; default kept");
        return false;
    }
    out = x.asBool();
    return true;
}

template <typename E>
bool enumField(const Json::Value& v, const char* key, E& out, std::initializer_list<std::pair<const char*, E>> names,
               std::vector<std::string>* w) {
    const Json::Value& x = v[key];
    if (x.isNull()) return false;
    if (x.isString()) {
        for (const auto& n : names) {
            if (x.asString() == n.first) { out = n.second; return true; }
        }
    }
    warn(w, std::string("counter.") + key + " has an unsupported value; default kept");
    return false;
}

const std::initializer_list<std::pair<const char*, DigitStyle>> STYLES = {
    {"none", DigitStyle::None}, {"roll", DigitStyle::Roll}, {"drum", DigitStyle::Drum}, {"slide", DigitStyle::Slide},
    {"fade", DigitStyle::Fade}, {"blur", DigitStyle::Blur}, {"pop", DigitStyle::Pop}, {"zoom", DigitStyle::Zoom},
    {"flip", DigitStyle::Flip}};
const std::initializer_list<std::pair<const char*, CounterMode>> MODES = {
    {"count", CounterMode::Count}, {"slot", CounterMode::Slot}, {"scramble", CounterMode::Scramble}};
const std::initializer_list<std::pair<const char*, Direction>> DIRECTIONS = {
    {"auto", Direction::Auto}, {"up", Direction::Up}, {"down", Direction::Down}, {"alternate", Direction::Alternate}};
const std::initializer_list<std::pair<const char*, Order>> ORDERS = {
    {"ltr", Order::Ltr}, {"rtl", Order::Rtl}, {"random", Order::Random}};

template <typename E>
const char* nameOf(E e, std::initializer_list<std::pair<const char*, E>> names) {
    for (const auto& n : names) if (n.second == e) return n.first;
    return "";
}

} // namespace

std::optional<CounterFx> counterFxFromJson(const Json::Value& v, std::vector<std::string>* w) {
    if (v.isNull() || !v.isObject()) {
        if (!v.isNull()) warn(w, "counter is not an object; rendered as plain text");
        return std::nullopt;
    }
    CounterFx fx = defaultFx();
    // The preset's look first, then every stated field on top (a hand-tuned preset keeps its id).
    if (v["preset"].isString()) {
        const std::string id = v["preset"].asString();
        if (const Preset* p = presetById(id)) fx = applyPreset(&fx, *p);
        else if (!id.empty()) warn(w, "counter.preset '" + id + "' is unknown; its fields are used as given");
        fx.preset = id;
    }
    const Json::Value& targets = v["targets"];
    if (targets.isArray()) {
        fx.targets.clear();
        for (const Json::Value& t : targets) if (t.isNumeric()) fx.targets.push_back(static_cast<int>(t.asDouble()));
    } else if (!targets.isNull()) {
        warn(w, "counter.targets is not an array; default kept");
    }
    const Json::Value& from = v["from"];
    if (from.isObject()) {
        fx.from.clear();
        for (const std::string& key : from.getMemberNames()) {
            char* end = nullptr;
            const long idx = std::strtol(key.c_str(), &end, 10);
            if (end == key.c_str() || *end != '\0' || !from[key].isNumeric()) {
                warn(w, "counter.from['" + key + "'] skipped");
                continue;
            }
            fx.from[static_cast<int>(idx)] = from[key].asDouble();
        }
    } else if (from.isArray()) {   // tolerated: [v0, v1, ...]
        fx.from.clear();
        for (Json::ArrayIndex i = 0; i < from.size(); ++i) if (from[i].isNumeric()) fx.from[static_cast<int>(i)] = from[i].asDouble();
    } else if (!from.isNull()) {
        warn(w, "counter.from is not an object; default kept");
    }
    const Json::Value& range = v["range"];
    if (range.isArray() && range.size() == 2 && range[0].isNumeric() && range[1].isNumeric()) {
        fx.range[0] = range[0].asDouble();
        fx.range[1] = range[1].asDouble();
    } else if (!range.isNull()) {
        warn(w, "counter.range is not [start, end]; default kept");
    }
    const Json::Value& easing = v["easing"];
    if (easing.isArray() && easing.size() == 4) {
        bool ok = true;
        for (const Json::Value& e : easing) ok = ok && e.isNumeric();
        if (ok) fx.easing = Bezier{easing[0].asDouble(), easing[1].asDouble(), easing[2].asDouble(), easing[3].asDouble()};
        else warn(w, "counter.easing is not four numbers; default kept");
    } else if (easing.isString()) {
        if (auto b = easingByName(easing.asString())) fx.easing = *b;
        else warn(w, "counter.easing '" + easing.asString() + "' is unknown; default kept");
    } else if (!easing.isNull()) {
        warn(w, "counter.easing is not a cubic-bezier [x1, y1, x2, y2]; default kept");
    }
    enumField(v, "mode", fx.mode, MODES, w);
    enumField(v, "style", fx.style, STYLES, w);
    enumField(v, "direction", fx.direction, DIRECTIONS, w);
    enumField(v, "order", fx.order, ORDERS, w);
    num(v, "swap", fx.swap, w);
    boolean(v, "soft", fx.soft, w);
    num(v, "bounce", fx.bounce, w);
    num(v, "maxSpeed", fx.maxSpeed, w);
    num(v, "cascade", fx.cascade, w);
    num(v, "distance", fx.distance, w);
    boolean(v, "clip", fx.clip, w);
    num(v, "stepFps", fx.stepFps, w);
    num(v, "shutter", fx.shutter, w);
    num(v, "landPop", fx.landPop, w);
    num(v, "grow", fx.grow, w);
    num(v, "spins", fx.spins, w);
    num(v, "rate", fx.rate, w);
    boolean(v, "compactScale", fx.compactScale, w);
    boolean(v, "tabular", fx.tabular, w);
    return fx;
}

std::optional<CounterFx> counterFxFromJsonText(const std::string& text, std::vector<std::string>* w) {
    if (text.empty()) return std::nullopt;
    Json::CharReaderBuilder builder;
    std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    Json::Value root;
    std::string err;
    if (!reader->parse(text.data(), text.data() + text.size(), &root, &err)) {
        warn(w, "counter is not valid JSON (" + err + "); rendered as plain text");
        return std::nullopt;
    }
    return counterFxFromJson(root, w);
}

Json::Value counterFxToJson(const CounterFx& fx) {
    Json::Value j(Json::objectValue);
    j["preset"] = fx.preset;
    Json::Value targets(Json::arrayValue);
    for (int t : fx.targets) targets.append(t);
    j["targets"] = targets;
    Json::Value from(Json::objectValue);
    for (const auto& kv : fx.from) from[std::to_string(kv.first)] = kv.second;
    j["from"] = from;
    Json::Value range(Json::arrayValue);
    range.append(fx.range[0]);
    range.append(fx.range[1]);
    j["range"] = range;
    Json::Value easing(Json::arrayValue);
    easing.append(fx.easing.x1);
    easing.append(fx.easing.y1);
    easing.append(fx.easing.x2);
    easing.append(fx.easing.y2);
    j["easing"] = easing;
    j["mode"] = nameOf(fx.mode, MODES);
    j["style"] = nameOf(fx.style, STYLES);
    j["direction"] = nameOf(fx.direction, DIRECTIONS);
    j["swap"] = fx.swap;
    j["soft"] = fx.soft;
    j["bounce"] = fx.bounce;
    j["maxSpeed"] = fx.maxSpeed;
    j["cascade"] = fx.cascade;
    j["distance"] = fx.distance;
    j["clip"] = fx.clip;
    j["stepFps"] = fx.stepFps;
    j["shutter"] = fx.shutter;
    j["landPop"] = fx.landPop;
    j["grow"] = fx.grow;
    j["spins"] = fx.spins;
    j["order"] = nameOf(fx.order, ORDERS);
    j["rate"] = fx.rate;
    j["compactScale"] = fx.compactScale;
    j["tabular"] = fx.tabular;
    return j;
}

} // namespace counter
} // namespace text
} // namespace openshot
