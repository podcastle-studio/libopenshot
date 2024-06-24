#include "BorderReflectedRotation.h"
#include "Exceptions.h"
#include "image-processing-lib/effects.h"

using namespace openshot;

/// Blank constructor, useful when using Json to load the effect properties
BorderReflectedRotation::BorderReflectedRotation() : angle(0) {
	// Init effect properties
	init_effect_details();
}

// Default constructor
BorderReflectedRotation::BorderReflectedRotation(Keyframe newAngle) : angle(newAngle) {
	// Init effect properties
	init_effect_details();
}

// Init effect settings
void BorderReflectedRotation::init_effect_details()
{
	/// Initialize the values of the EffectInfo struct.
	InitEffectInfo();

	/// Set the effect info
	info.class_name = "BorderReflectedRotation";
	info.name = "BorderReflectedRotation";
	info.description = "Rotate image by specified angle and fill appeared black parts with mirrored edges";
	info.has_audio = false;
	info.has_video = true;
}

// This method is required for all derived classes of EffectBase, and returns a
// modified openshot::Frame object
std::shared_ptr<openshot::Frame> BorderReflectedRotation::GetFrame(std::shared_ptr<openshot::Frame> frame, int64_t frame_number)
{
    const auto angle_value = -angle.GetValue(frame_number);

    if (angle_value == 0) {
        return frame;
    }

    auto imageCv = frame->GetImageCV();
    Podcastle::Effects::applyBorderReflectedRotationEffect(imageCv, angle_value);
    frame->SetImageCV(imageCv);

	// return the modified frame
	return frame;
}

// Generate JSON string of this object
std::string BorderReflectedRotation::Json() const {

	// Return formatted string
	return JsonValue().toStyledString();
}

// Generate Json::Value for this object
Json::Value BorderReflectedRotation::JsonValue() const {
	// Create root json object
	Json::Value root = EffectBase::JsonValue(); // get parent properties
	root["type"] = info.class_name;
	root["angle"] = angle.JsonValue();

	// return JsonValue
	return root;
}

// Load JSON string into this object
void BorderReflectedRotation::SetJson(const std::string value) {

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
void BorderReflectedRotation::SetJsonValue(const Json::Value root) {

	// Set parent data
	EffectBase::SetJsonValue(root);

	// Set data from Json (if key is found)
	if (!root["angle"].isNull())
        angle.SetJsonValue(root["angle"]);
}

// Get all properties for a specific frame
std::string BorderReflectedRotation::PropertiesJSON(int64_t requested_frame) const {

	// Generate JSON properties list
	Json::Value root = BasePropertiesJSON(requested_frame);

	// Keyframes
	root["angle"] = add_property_json("angle", angle.GetValue(requested_frame), "float", "", &angle, 0, 100, false, requested_frame);

	// Return formatted string
	return root.toStyledString();
}
