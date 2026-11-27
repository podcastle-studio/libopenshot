#pragma once

// The `counter` object of a TEXT clip in the render payload: CounterFx exactly as the TS engine
// stores it (types.ts). Missing fields take the handoff defaults: DEFAULT_FX, with the named
// `preset`'s look laid over it first (applyPreset), then every field the payload states.

#include "CounterEngine.h"

#include <json/json.h>

#include <optional>
#include <string>
#include <vector>

namespace openshot {
namespace text {
namespace counter {

/// Parse a payload `counter` value. Null / absent / not an object -> nullopt (plain text).
/// Never throws: an unknown preset, enum value or a field of the wrong type is skipped with a
/// message in `warnings` and the default kept.
std::optional<CounterFx> counterFxFromJson(const Json::Value& value, std::vector<std::string>* warnings = nullptr);

/// The full CounterFx as JSON (every field), the inverse of counterFxFromJson.
Json::Value counterFxToJson(const CounterFx& fx);

} // namespace counter
} // namespace text
} // namespace openshot
