#include "BorderReflectedMove.h"
#include "Exceptions.h"
#include "image-processing-lib/effects.h"

using namespace openshot;

/// Blank constructor, useful when using Json to load the effect properties
BorderReflectedMove::BorderReflectedMove() : dx(0), dy(0) {
	// Init effect properties
	init_effect_details();
}

// Default constructor
BorderReflectedMove::BorderReflectedMove(Keyframe dx_, Keyframe dy_) : dx(dx_), dy(dy_) {
	// Init effect properties
	init_effect_details();
}

// Init effect settings
void BorderReflectedMove::init_effect_details()
{
	/// Initialize the values of the EffectInfo struct.
	InitEffectInfo();

	/// Set the effect info
	info.class_name = "BorderReflectedMove";
	info.name = "BorderReflectedMove";
	info.description = "Move image by specified (dx,dy) and fill appeared black parts with mirrored edges";
	info.has_audio = false;
	info.has_video = true;
}

// This method is required for all derived classes of EffectBase, and returns a
// modified openshot::Frame object
std::shared_ptr<openshot::Frame> BorderReflectedMove::GetFrame(std::shared_ptr<openshot::Frame> frame, int64_t frame_number)
{
    const auto dx_value = dx.GetValue(frame_number);
    const auto dy_value = dy.GetValue(frame_number);

    if (dx_value == 0 && dy_value == 0) {
        return frame;
    }

    auto imageCv = frame->GetImageCV();
    Podcastle::Effects::applyBorderReflectedMoveEffect(imageCv, dx_value, dy_value);
    frame->SetImageCV(imageCv);

	// return the modified frame
	return frame;
}

// Generate JSON string of this object
std::string BorderReflectedMove::Json() const {

	// Return formatted string
	return JsonValue().toStyledString();
}

// Generate Json::Value for this object
Json::Value BorderReflectedMove::JsonValue() const {
	// Create root json object
	Json::Value root = EffectBase::JsonValue(); // get parent properties
	root["type"] = info.class_name;
	root["dx"] = dx.JsonValue();
	root["dy"] = dy.JsonValue();

	// return JsonValue
	return root;
}

// Load JSON string into this object
void BorderReflectedMove::SetJson(const std::string value) {

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
void BorderReflectedMove::SetJsonValue(const Json::Value root) {

	// Set parent data
	EffectBase::SetJsonValue(root);

	// Set data from Json (if key is found)
	if (!root["dx"].isNull())
		dx.SetJsonValue(root["dx"]);
	if (!root["dy"].isNull())
		dy.SetJsonValue(root["dy"]);
}

// Get all properties for a specific frame
std::string BorderReflectedMove::PropertiesJSON(int64_t requested_frame) const {

	// Generate JSON properties list
	Json::Value root = BasePropertiesJSON(requested_frame);

	// Keyframes
	root["dx"] = add_property_json("dx", dx.GetValue(requested_frame), "float", "", &dx, 0, 100, false, requested_frame);
	root["dy"] = add_property_json("dy", dy.GetValue(requested_frame), "float", "", &dy, 0, 100, false, requested_frame);

	// Return formatted string
	return root.toStyledString();
}
