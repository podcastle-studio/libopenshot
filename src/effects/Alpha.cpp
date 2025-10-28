#include "Alpha.h"
#include "Exceptions.h"
#include "./image-processing-lib/src/Effects/effects.h"

using namespace openshot;

namespace {
uint8_t scaleByte(uint8_t v, float k255_div255) {
	// v * k  where k is [0..255], result back to [0..255]
	// k255_div255 is (k / 255.0f)
	return static_cast<uint8_t>(v * k255_div255);
}

void applyAlphaPremultiplied(QImage& img, double alphaValue)
{
	// clamp alphaValue to [0,1]
	if (alphaValue <= 0.0) {
		// fully transparent: just zero the whole thing fast
		// (this is a micro-opt for 0, optional)
		const int totalBytes = img.sizeInBytes();
		memset(img.bits(), 0, totalBytes);
		return;
	}
	if (alphaValue >= 1.0) {
		// alpha of 1.0 means no change needed
		return;
	}

	// Convert alpha to a float we can reuse
	// We'll multiply each channel by this factor.
	const float k = static_cast<float>(alphaValue);

	// Sanity: ensure we're working in premultiplied 32bpp RGBA
	if (img.format() != QImage::Format_RGBA8888_Premultiplied) {
		img = img.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
	}

	const int w = img.width();
	const int h = img.height();
	const int stride = img.bytesPerLine();
	uint8_t* base = img.bits();

	for (int y = 0; y < h; ++y) {
		uint8_t* row = base + y * stride;
		for (int x = 0; x < w; ++x) {
			uint8_t* px = row + x * 4;
			// px[0] = R (premultiplied)
			// px[1] = G (premultiplied)
			// px[2] = B (premultiplied)
			// px[3] = A
			px[0] = static_cast<uint8_t>(px[0] * k);
			px[1] = static_cast<uint8_t>(px[1] * k);
			px[2] = static_cast<uint8_t>(px[2] * k);
			px[3] = static_cast<uint8_t>(px[3] * k);
		}
	}
}

}

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
	const double alphaValue = alpha.GetValue(frame_number);
	if (alphaValue >= 1.0) {
		return frame;
	}

	std::shared_ptr<QImage> img = frame->GetImage();
	applyAlphaPremultiplied(*img, alphaValue);
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
