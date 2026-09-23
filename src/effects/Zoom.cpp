#include "Zoom.h"

#include "skia/include/core/SkM44.h"
#include "EffectShaders.h"
#include "image-processing-lib/src/Planner/EffectPlan.h"

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

// The shared SkSL source lives in image-processing-lib/shaders/ so the editor loads the
// same bytes through CanvasKit; it is embedded here at build time. Read it there --
// including why it is written the way it is.
const char* Zoom::GpuShaderSource() const
{
	return openshot::shaders::kZoom;
}

bool Zoom::SetGpuUniforms(SkRuntimeEffectBuilder& builder, int64_t frame_number,
				int width, int height) const
{
	// Resolved by the shared planner (image-processing-lib/src/Planner), the same code the editor
	// runs, so the two cannot disagree about what these values mean. It declines -- and the C++
	// twin runs -- for every case the fragment does not cover.
	return BindPlan(builder, Podcastle::Effects::planEffect(
		"ZOOM", {{"zoomPercent", zoomPercent.GetValue(frame_number)}, {"anchorX", anchorX.GetValue(frame_number)}, {"anchorY", anchorY.GetValue(frame_number)}}, width, height));
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
