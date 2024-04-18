#include "Exposure.h"
#include "Exceptions.h"

using namespace openshot;

/// Blank constructor, useful when using Json to load the effect properties
Exposure::Exposure() : alpha(0.0) {
	// Init effect properties
	init_effect_details();
}

// Default constructor
Exposure::Exposure(Keyframe new_alpha) : alpha(new_alpha)
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

	// Get keyframe values for this frame
	float alpha_value = std::max(1.0, alpha.GetValue(frame_number));

    if (frame_image->format() != QImage::Format_ARGB32 && frame_image->format() != QImage::Format_RGB32) {
        frame_image = std::make_shared<QImage>(frame_image->convertToFormat(QImage::Format_ARGB32));
        frame->AddImage(frame_image);
    }

    uchar *bits = frame_image->bits();
    int numBytes = frame_image->byteCount();
    int pixelCount = numBytes / 4; // Each pixel is 4 bytes (ARGB)

    for (int i = 0; i < pixelCount; i++) {
        int r = bits[4 * i + 2];     // Red
        int g = bits[4 * i + 1];     // Green
        int b = bits[4 * i];         // Blue
        r = std::min(255, static_cast<int>(r * alpha_value));
        g = std::min(255, static_cast<int>(g * alpha_value));
        b = std::min(255, static_cast<int>(b * alpha_value));

        bits[4 * i + 2] = r;
        bits[4 * i + 1] = g;
        bits[4 * i] = b;
    }

	// return the modified frame
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
	root["alpha"] = alpha.JsonValue();

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
	if (!root["brightness"].isNull())
		alpha.SetJsonValue(root["alpha"]);
}

// Get all properties for a specific frame
std::string Exposure::PropertiesJSON(int64_t requested_frame) const {

	// Generate JSON properties list
	Json::Value root = BasePropertiesJSON(requested_frame);

	// Keyframes
	root["alpha"] = add_property_json("Brightness", alpha.GetValue(requested_frame), "float", "", &alpha, -1.0, 1.0, false, requested_frame);

	// Return formatted string
	return root.toStyledString();
}
