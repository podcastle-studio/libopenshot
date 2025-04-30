#include "Exposure.h"
#include "Exceptions.h"
#include "./image-processing-lib/src/Effects/effects.h"

using namespace openshot;

/// Blank constructor, useful when using Json to load the effect properties
Exposure::Exposure() : exposure(0.0) {
	// Init effect properties
	init_effect_details();
}

// Default constructor
Exposure::Exposure(Keyframe new_exposure) : exposure(new_exposure)
{
	// Init effect properties
	init_effect_details();
}

// Init effect settings
void Exposure::init_effect_details()
{
	/// Initialize the values of the EffectInfo struct.
	InitEffectInfo();

	/// Set the effect info
	info.class_name = "Exposure";
	info.name = "Exposure";
	info.description = "Adjust exposure of the frame's image.";
	info.has_audio = false;
	info.has_video = true;
}

// This method is required for all derived classes of EffectBase, and returns a
// modified openshot::Frame object
std::shared_ptr<openshot::Frame> Exposure::GetFrame(std::shared_ptr<openshot::Frame> frame, int64_t frame_number)
{
	// Get the frame's image
	std::shared_ptr<QImage> frame_image = frame->GetImage();

	// Get keyframe exposure value (ensuring a minimum value of 1.0)
	auto exposure_value = std::max(1.0, exposure.GetValue(frame_number));

	// Ensure the image is in a 32-bit format (ARGB32)
	if (frame_image->format() != QImage::Format_ARGB32 && frame_image->format() != QImage::Format_RGB32) {
		frame_image = std::make_shared<QImage>(frame_image->convertToFormat(QImage::Format_ARGB32));
		frame->AddImage(frame_image);
	}

	// Retrieve the raw pixel data and image dimensions.
	uchar *bits = frame_image->bits();
	int width = frame_image->width();
	int height = frame_image->height();

	// Apply the exposure effect.
	Podcastle::Effects::applyExposureEffect(bits, width, height, exposure_value);

	// Return the modified frame.
	return frame;
}

// Generate JSON string of this object
std::string Exposure::Json() const {

	// Return formatted string
	return JsonValue().toStyledString();
}

// Generate Json::Value for this object
Json::Value Exposure::JsonValue() const {

	// Create root json object
	Json::Value root = EffectBase::JsonValue(); // get parent properties
	root["type"] = info.class_name;
	root["exposure"] = exposure.JsonValue();

	// return JsonValue
	return root;
}

// Load JSON string into this object
void Exposure::SetJson(const std::string value) {

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
void Exposure::SetJsonValue(const Json::Value root) {
	// Set parent data
	EffectBase::SetJsonValue(root);

	// Set data from Json (if key is found)
	if (!root["exposure"].isNull())
		exposure.SetJsonValue(root["exposure"]);
}

// Get all properties for a specific frame
std::string Exposure::PropertiesJSON(int64_t requested_frame) const {

	// Generate JSON properties list
	Json::Value root = BasePropertiesJSON(requested_frame);

	// Keyframes
	root["exposure"] = add_property_json("Exposure", exposure.GetValue(requested_frame), "float", "", &exposure, -1.0, 1.0, false, requested_frame);

	// Return formatted string
	return root.toStyledString();
}
