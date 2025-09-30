#include "Alpha.h"
#include "Exceptions.h"
#include "./image-processing-lib/src/Effects/effects.h"

using namespace openshot;

/// Blank constructor, useful when using Json to load the effect properties
Alpha::Alpha() : alpha(1) {
	// Init effect properties
	init_effect_details();
}

// Default constructor
Alpha::Alpha(Keyframe alpha) : alpha(alpha) {
	// Init effect properties
	init_effect_details();
}

// Init effect settings
void Alpha::init_effect_details() {
	/// Initialize the values of the EffectInfo struct.
	InitEffectInfo();

	/// Set the effect info
	info.class_name = "Alpha";
	info.name = "Alpha";
	info.description = "Apply alpha on frame";
	info.has_audio = false;
	info.has_video = true;
}

// This method is required for all derived classes of EffectBase, and returns a
// modified openshot::Frame object
std::shared_ptr<openshot::Frame> Alpha::GetFrame(std::shared_ptr<openshot::Frame> frame, int64_t frame_number) {
    const auto alphaValue = alpha.GetValue(frame_number);
    if (alphaValue == 1) {
        return frame;
    }

	auto imageCv = frame->GetImageCV();
	Podcastle::Effects::applyAlphaEffect(imageCv, alphaValue);
	frame->SetImageCV(imageCv);

	// return the modified frame
	return frame;
}

// Generate JSON string of this object
std::string Alpha::Json() const {

	// Return formatted string
	return JsonValue().toStyledString();
}

// Generate Json::Value for this object
Json::Value Alpha::JsonValue() const {
	// Create root json object
	Json::Value root = EffectBase::JsonValue(); // get parent properties
	root["type"] = info.class_name;
	root["alpha"] = alpha.JsonValue();

	// return JsonValue
	return root;
}

// Load JSON string into this object
void Alpha::SetJson(const std::string value) {

	// Parse JSON string into JSON objects
	try
	{
		const Json::Value root = openshot::stringToJson(value);
		// Set all values that match
		SetJsonValue(root);
	}
	catch (const std::exception& e)
	{
		// Error parsing JSON (or missing keys)
		throw InvalidJSON("JSON is invalid (missing keys or invalid data types)");
	}
}

// Load Json::Value into this object
void Alpha::SetJsonValue(const Json::Value root) {

	// Set parent data
	EffectBase::SetJsonValue(root);

	// Set data from Json (if key is found)
	if (!root["alpha"].isNull())
        alpha.SetJsonValue(root["alpha"]);
}

// Get all properties for a specific frame
std::string Alpha::PropertiesJSON(int64_t requested_frame) const {

	// Generate JSON properties list
	Json::Value root = BasePropertiesJSON(requested_frame);

	// Keyframes
	root["alpha"] = add_property_json("alpha", alpha.GetValue(requested_frame), "float", "", &alpha, 0, 100, false, requested_frame);

	// Return formatted string
	return root.toStyledString();
}
