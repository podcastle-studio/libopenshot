#include "Exposure.h"

#include "skia/include/effects/SkRuntimeEffect.h"

#include <algorithm>
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
	// Get keyframe exposure value (ensuring a minimum value of 1.0)
	auto exposure_value = std::max(1.0, exposure.GetValue(frame_number));

	// The shader when there is a GPU to run it on, the C++ otherwise.
	//
	// Note what the GPU path skips: the ARGB32 conversion below is a round trip, not
	// a conversion. A Frame's image is Format_RGBA8888_Premultiplied, so the branch
	// always fires, and AddImage converts the copy straight back to premultiplied
	// RGBA in place -- so applyExposureEffect still sees premultiplied RGBA, having
	// been through an unpremultiply and a re-premultiply in 8 bits on the way. That
	// costs up to 1 LSB on a partially transparent pixel and buys nothing. The
	// fragment does not reproduce it; whether that shows up is measured by
	// openshot-gpu-effect-parity rather than assumed.
	// It has to come before GetImage(): on a GPU-backed frame that call IS the one
	// readback, so asking for the pixels first flattens the frame and the shader
	// then pays an upload and a readback every frame -- measured at 3.3 ms a pass
	// against 0.15 ms with the order this way round.
	if (ApplyOnGpu(frame, frame_number))
		return frame;

	// Get the frame's image
	std::shared_ptr<QImage> frame_image = frame->GetImage();

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

// The SkSL twin of applyExposureEffect: unpremultiply, scale, clamp, premultiply.
const char* Exposure::GpuShaderSource() const
{
	return R"SKSL(
uniform float exposure;   // >= 1.0, clamped on the host as GetFrame clamps it

float4 main(float2 p) {
	float4 bytes = osBytes(p);
	float alpha_percent = osAlphaPercent(bytes.a);
	float3 c = osUnpremul(bytes.rgb, alpha_percent);
	c = osConstrain3(osToInt3(c * exposure));
	return osPremul(c, alpha_percent, bytes.a);
}
)SKSL";
}

bool Exposure::SetGpuUniforms(SkRuntimeEffectBuilder& builder, int64_t frame_number,
							  int width, int height) const
{
	// The C++ multiplies an unsigned char by the keyframe value as a *double*;
	// SkSL uniforms are float only, so this narrows and the product can land on the
	// other side of an integer boundary. That is a known and measured cost of the
	// port, not an oversight -- see the parity numbers in the W19 worklist item.
	builder.uniform("exposure") =
		static_cast<float>(std::max(1.0, exposure.GetValue(frame_number)));
	return true;
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
