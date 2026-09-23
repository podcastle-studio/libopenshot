#include "Exposure.h"

#include "EffectShaders.h"
#include "image-processing-lib/src/Planner/EffectPlan.h"

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

	// The shader when there is a GPU to run it on, the C++ otherwise. It has to come
	// before GetImage(): on a GPU-backed frame that call IS the one readback, so
	// asking for the pixels first flattens the frame and the shader then pays an
	// upload and a readback every frame -- measured at 3.3 ms a pass against 0.15 ms
	// with the order this way round.
	if (ApplyOnGpu(frame, frame_number))
		return frame;

	// Get the frame's image, which is always Format_RGBA8888_Premultiplied: GetImage()
	// either reads back a GPU surface into that format or returns an image AddImage()
	// has already converted to it.
	//
	// This used to convert to Format_ARGB32 first and hand the copy to AddImage(),
	// which converted it straight back in place -- so applyExposureEffect saw
	// premultiplied RGBA either way, having gone through an unpremultiply and a
	// re-premultiply to get there. The branch always fired, because a Frame's image
	// is always Format_RGBA8888_Premultiplied.
	//
	// Removed 2026-09-22 as dead work, and measured rather than assumed: Qt's round
	// trip turns out to be *lossless*, so not one pixel of any golden moved and the
	// GPU parity numbers did not shift either. What it cost was time -- two full-image
	// conversions and an allocation per frame, 7.0-7.8 ms down to 5.9-6.6 ms at 1080p,
	// about 15 %. (The earlier guess that this round trip explained Exposure's
	// remaining GPU divergence was wrong; that is the unpremultiply's division, the
	// same one every dividing fragment pays. See GPU-DECISIONS.md, W19.)
	std::shared_ptr<QImage> frame_image = frame->GetImage();

	// Retrieve the raw pixel data and image dimensions.
	uchar *bits = frame_image->bits();
	int width = frame_image->width();
	int height = frame_image->height();

	// Apply the exposure effect.
	Podcastle::Effects::applyExposureEffect(bits, width, height, exposure_value);

	// Return the modified frame.
	return frame;
}

// The shared SkSL source lives in image-processing-lib/shaders/ so the editor loads the
// same bytes through CanvasKit; it is embedded here at build time. Read it there --
// including why it is written the way it is.
const char* Exposure::GpuShaderSource() const
{
	return openshot::shaders::kExposure;
}

bool Exposure::SetGpuUniforms(SkRuntimeEffectBuilder& builder, int64_t frame_number,
					int width, int height) const
{
	// Resolved by the shared planner (image-processing-lib/src/Planner), the same code the editor
	// runs, so the two cannot disagree about what these values mean. It declines -- and the C++
	// twin runs -- for every case the fragment does not cover.
	return BindPlan(builder, Podcastle::Effects::planEffect(
		"EXPOSURE", {{"exposure", exposure.GetValue(frame_number)}}, width, height));
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
