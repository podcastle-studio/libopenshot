#include "BorderReflectedMove.h"

#include "skia/include/core/SkM44.h"
#include "skia/include/effects/SkRuntimeEffect.h"
#include "Exceptions.h"
#include "./image-processing-lib/src/Effects/effects.h"

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

    // The shader when there is a GPU to run it on, OpenCV otherwise. Before GetImageCV(), which
    // on a GPU-backed frame is a readback and two full-frame cv::Mat conversions.
    if (ApplyOnGpu(frame, frame_number))
        return frame;

    auto imageCv = frame->GetImageCV();
    Podcastle::Effects::applyBorderReflectedMoveEffect(imageCv, dx_value, dy_value);
    frame->SetImageCV(imageCv);

	// return the modified frame
	return frame;
}

// The SkSL twin of applyBorderReflectedMoveEffect.
//
// The C++ builds a reflected border, warps the bordered image by the shift, then crops back to the
// original size. Composed, all of that is one thing: **dst(x, y) = src_reflected(x - shiftX,
// y - shiftY)**, sampled bilinearly. The border exists only so warpAffine has valid pixels to read;
// it is not visible in the result, so the fragment reproduces the function and not the machinery.
//
// The sampling is OpenCV's INTER_LINEAR, which uses 5-bit fixed-point weights where this uses
// float, so the two agree to about an LSB rather than exactly -- W20's gate is 45 dB for exactly
// this reason.
const char* BorderReflectedMove::GpuShaderSource() const
{
	return R"SKSL(
uniform float2 size;
uniform float2 shift;   // in pixels; positive moves the content in +x / +y

float4 main(float2 p) {
	return osSampleReflectedLinear(p - shift, size) / 255.0;
}
)SKSL";
}

bool BorderReflectedMove::SetGpuUniforms(SkRuntimeEffectBuilder& builder, int64_t frame_number,
										 int width, int height) const
{
	// dx/dy are fractions of the frame, which is why this effect is resolution-independent and
	// not caught by the blur radii's missing reference resolution.
	builder.uniform("size") = SkV2{static_cast<float>(width), static_cast<float>(height)};
	builder.uniform("shift") =
		SkV2{static_cast<float>(dx.GetValue(frame_number) * width),
			 static_cast<float>(dy.GetValue(frame_number) * height)};
	return true;
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
