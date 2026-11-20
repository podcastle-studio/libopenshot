#include "BorderReflectedRotation.h"

#include "skia/include/core/SkM44.h"
#include "EffectShaders.h"
#include "image-processing-lib/src/Planner/EffectPlan.h"

#include "skia/include/effects/SkRuntimeEffect.h"

#include <opencv2/imgproc.hpp>

#include <cmath>
#include "Exceptions.h"
#include "./image-processing-lib/src/Effects/effects.h"

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
    const auto angle_value = angle.GetValue(frame_number);

    if (angle_value == 0) {
        return frame;
    }

    // The shader when there is a GPU to run it on, OpenCV otherwise. Before GetImageCV(), which
    // on a GPU-backed frame is a readback and two full-frame cv::Mat conversions.
    if (ApplyOnGpu(frame, frame_number))
        return frame;

    auto imageCv = frame->GetImageCV();
    Podcastle::Effects::applyBorderReflectedRotationEffect(imageCv, angle_value);
    frame->SetImageCV(imageCv);

	// return the modified frame
	return frame;
}

// The shared SkSL source lives in image-processing-lib/shaders/ so the editor loads the
// same bytes through CanvasKit; it is embedded here at build time. Read it there --
// including why it is written the way it is.
const char* BorderReflectedRotation::GpuShaderSource() const
{
	return openshot::shaders::kBorderReflectedRotation;
}

bool BorderReflectedRotation::SetGpuUniforms(SkRuntimeEffectBuilder& builder, int64_t frame_number,
								int width, int height) const
{
	// Resolved by the shared planner (image-processing-lib/src/Planner), the same code the editor
	// runs, so the two cannot disagree about what these values mean. It declines -- and the C++
	// twin runs -- for every case the fragment does not cover.
	Podcastle::Effects::EffectPlan plan;
	PlanForFrame(frame_number, width, height, plan);
	return BindPlan(builder, plan);
}

bool BorderReflectedRotation::PlanForFrame(int64_t frame_number, int width, int height,
                                          Podcastle::Effects::EffectPlan& plan) const
{
	// The planner's answer for ApplyOnGpu too, so an identity, a clear or a multi-pass plan runs
	// on the GPU rather than declining to the C++.
	plan = Podcastle::Effects::planEffect(
		"BORDER_REFLECTED_ROTATION", {{"angle", angle.GetValue(frame_number)}}, width, height);
	return true;
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
