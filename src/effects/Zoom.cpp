#include "Zoom.h"

#include "skia/include/core/SkM44.h"
#include "skia/include/effects/SkRuntimeEffect.h"

#include <algorithm>
#include "Exceptions.h"
#include "./image-processing-lib/src/Effects/effects.h"

using namespace openshot;

/// Blank constructor, useful when using Json to load the effect properties
Zoom::Zoom() : zoomPercent(100) {
	// Init effect properties
	init_effect_details();
}

// Default constructor
Zoom::Zoom(const Keyframe& zoomPercent_, const Keyframe& anchorX_, const Keyframe& anchorY_)
    : zoomPercent(zoomPercent_), anchorX(anchorX_), anchorY(anchorY_)
{
	// Init effect properties
	init_effect_details();
}

// Init effect settings
void Zoom::init_effect_details()
{
	/// Initialize the values of the EffectInfo struct.
	InitEffectInfo();

	/// Set the effect info
	info.class_name = "Zoom";
	info.name = "Zoom";
	info.description = "Add zoom effect to your video.";
	info.has_audio = false;
	info.has_video = true;
}

// This method is required for all derived classes of EffectBase, and returns a
// modified openshot::Frame object
std::shared_ptr<openshot::Frame> Zoom::GetFrame(std::shared_ptr<openshot::Frame> frame, int64_t frame_number)
{
    const auto zoomPercentVal = zoomPercent.GetValue(frame_number);
    const auto anchorValX = anchorX.GetValue(frame_number);
    const auto anchorValY = anchorY.GetValue(frame_number);

    if (zoomPercentVal == 100) {
        return frame;
    }
    // The shader when there is a GPU to run it on, OpenCV otherwise. Before GetImageCV(), which
    // on a GPU-backed frame is a readback and two full-frame cv::Mat conversions.
    if (ApplyOnGpu(frame, frame_number))
        return frame;

    auto imageCv = frame->GetImageCV();
    Podcastle::Effects::applyZoomEffect(imageCv, zoomPercentVal, anchorValX, anchorValY);
    frame->SetImageCV(imageCv);
	// return the modified frame
	return frame;
}

// The SkSL twin of applyZoomEffect, zoom-IN only.
//
// **Zoom-out is deliberately not here.** That branch downscales with cv::resize and then pads back
// out with copyMakeBorder, and both the resized size and the four paddings are computed with
// independent roundings and then clamped to >= 0 -- so the result is not reliably the frame's own
// size, and the C++ assigns it back with `image = result`, changing the frame's dimensions.
// ApplyOnGpu draws into a surface of a fixed size and has no way to express that. SetGpuUniforms
// declines, and the OpenCV path runs.
//
// The zoom-in branch is a crop to an integer rectangle followed by cv::resize back to full size.
// cv::resize's INTER_LINEAR maps dst to src as (dst + 0.5) * scale - 0.5 and clamps at the edges,
// which is what this reproduces; its weights are 11-bit fixed point where this is float, so the
// two agree closely rather than exactly.
const char* Zoom::GpuShaderSource() const
{
	return R"SKSL(
uniform float2 size;      // the frame
uniform float2 cropOrigin; // the ROI's top-left, in pixels
uniform float2 cropSize;   // the ROI's size, in pixels

float4 main(float2 p) {
	float2 q = floor(p);
	// cv::resize's coordinate convention, then offset into the crop rectangle.
	float2 scale = cropSize / size;
	float2 src = (q + 0.5) * scale - 0.5 + cropOrigin;

	// Bilinear, clamped to the crop rectangle -- cv::resize replicates its edge pixels.
	float2 lo = cropOrigin;
	float2 hi = cropOrigin + cropSize - 1.0;
	float2 base = floor(src);
	float2 f = src - base;
	float4 c00 = osBytes(clamp(base + float2(0.0, 0.0), lo, hi) + 0.5);
	float4 c10 = osBytes(clamp(base + float2(1.0, 0.0), lo, hi) + 0.5);
	float4 c01 = osBytes(clamp(base + float2(0.0, 1.0), lo, hi) + 0.5);
	float4 c11 = osBytes(clamp(base + float2(1.0, 1.0), lo, hi) + 0.5);
	return mix(mix(c00, c10, f.x), mix(c01, c11, f.x), f.y) / 255.0;
}
)SKSL";
}

bool Zoom::SetGpuUniforms(SkRuntimeEffectBuilder& builder, int64_t frame_number,
						  int width, int height) const
{
	const double zoom_value = zoomPercent.GetValue(frame_number);
	if (zoom_value <= 100)
		return false;   // zoom-out, or no zoom -- see above

	// Every one of these truncates in the C++, including the /2s, which are integer divisions.
	const int anchor_x = static_cast<int>(anchorX.GetValue(frame_number) * width);
	const int anchor_y = static_cast<int>(anchorY.GetValue(frame_number) * height);
	const int new_width = static_cast<int>(width * 100 / zoom_value);
	const int new_height = static_cast<int>(height * 100 / zoom_value);
	if (new_width <= 0 || new_height <= 0)
		return false;

	int x = std::max(anchor_x - new_width / 2, 0);
	int y = std::max(anchor_y - new_height / 2, 0);
	x = std::min(x, width - new_width);
	y = std::min(y, height - new_height);

	builder.uniform("size") = SkV2{static_cast<float>(width), static_cast<float>(height)};
	builder.uniform("cropOrigin") = SkV2{static_cast<float>(x), static_cast<float>(y)};
	builder.uniform("cropSize") = SkV2{static_cast<float>(new_width), static_cast<float>(new_height)};
	return true;
}

// Generate JSON string of this object
std::string Zoom::Json() const {

	// Return formatted string
	return JsonValue().toStyledString();
}

// Generate Json::Value for this object
Json::Value Zoom::JsonValue() const {

	// Create root json object
	Json::Value root = EffectBase::JsonValue(); // get parent properties
	root["type"] = info.class_name;
	root["zoomPercent"] = zoomPercent.JsonValue();
	root["anchorX"] = anchorX.JsonValue();
	root["anchorY"] = anchorY.JsonValue();

	// return JsonValue
	return root;
}

// Load JSON string into this object
void Zoom::SetJson(const std::string value) {

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
void Zoom::SetJsonValue(const Json::Value root) {

	// Set parent data
	EffectBase::SetJsonValue(root);

	// Set data from Json (if key is found)
	if (!root["zoomPercent"].isNull())
		zoomPercent.SetJsonValue(root["zoomPercent"]);
	if (!root["anchorX"].isNull())
		anchorX.SetJsonValue(root["anchorX"]);
	if (!root["anchorY"].isNull())
		anchorY.SetJsonValue(root["anchorY"]);
}

// Get all properties for a specific frame
std::string Zoom::PropertiesJSON(int64_t requested_frame) const {

	// Generate JSON properties list
	Json::Value root = BasePropertiesJSON(requested_frame);

	// Keyframes
	root["zoomPercent"] = add_property_json("Zoom percent", 0.0, "zoomPercent", "", &zoomPercent, 0, std::numeric_limits<float>::infinity(), false, requested_frame);

	// Return formatted string
	return root.toStyledString();
}
