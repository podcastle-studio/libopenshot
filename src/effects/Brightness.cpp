/**
 * @file
 * @brief Source file for Brightness class
 * @author Jonathan Thomas <jonathan@openshot.org>
 *
 * @ref License
 */

// Copyright (c) 2008-2019 OpenShot Studios, LLC
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "Brightness.h"
#include "Exceptions.h"

#include "EffectShaders.h"

#include "skia/include/effects/SkRuntimeEffect.h"
#include "./image-processing-lib/src/Effects/effects.h"

using namespace openshot;

/// Blank constructor, useful when using Json to load the effect properties
Brightness::Brightness() : brightness(0.0), contrast(3.0) {
	// Init effect properties
	init_effect_details();
}

// Default constructor
Brightness::Brightness(Keyframe new_brightness, Keyframe new_contrast) : brightness(new_brightness), contrast(new_contrast)
{
	// Init effect properties
	init_effect_details();
}

// Init effect settings
void Brightness::init_effect_details()
{
	/// Initialize the values of the EffectInfo struct.
	InitEffectInfo();

	/// Set the effect info
	info.class_name = "Brightness";
	info.name = "Brightness & Contrast";
	info.description = "Adjust the brightness and contrast of the frame's image.";
	info.has_audio = false;
	info.has_video = true;
}

// The contrast factor and brightness shift, computed once for a frame. Kept in
// one place so the shader and the C++ cannot drift: the shader is only
// bit-identical while it is handed exactly these two floats, rounded from double
// at exactly this point.
namespace {
	struct BrightnessUniforms { float factor; float shift; };

	// float, not double, on purpose: GetFrame() below narrows the keyframe values to
	// float before handing them to applyBrightnessEffect(), which then widens them
	// again for this arithmetic. Taking doubles here would skip that round trip and
	// the shader would stop being bit-identical for no visible reason.
	BrightnessUniforms brightnessUniforms(float brightness_value, float contrast_value)
	{
		return {
			static_cast<float>((259.0 * (contrast_value + 255.0)) /
							   (255.0 * (259.0 - contrast_value))),
			static_cast<float>(255.0 * brightness_value),
		};
	}
}

// The shared SkSL source lives in image-processing-lib/shaders/ so the editor loads the
// same bytes through CanvasKit; it is embedded here at build time. Read it there --
// including why it is written the way it is.
const char* Brightness::GpuShaderSource() const
{
	return openshot::shaders::kBrightness;
}

bool Brightness::SetGpuUniforms(SkRuntimeEffectBuilder& builder, int64_t frame_number,
								int width, int height) const
{
	const BrightnessUniforms u = brightnessUniforms(
		static_cast<float>(brightness.GetValue(frame_number)),
		static_cast<float>(contrast.GetValue(frame_number)));
	builder.uniform("factor") = u.factor;
	builder.uniform("shift") = u.shift;
	return true;
}

// This method is required for all derived classes of EffectBase, and returns a
// modified openshot::Frame object
std::shared_ptr<openshot::Frame> Brightness::GetFrame(std::shared_ptr<openshot::Frame> frame, int64_t frame_number)
{
	// The shader when there is a GPU to run it on, the C++ otherwise. ApplyOnGpu
	// declining is the normal no-GPU answer, and the CPU path below is untouched.
	if (ApplyOnGpu(frame, frame_number))
		return frame;

	// Get the frame's image
	std::shared_ptr<QImage> frame_image = frame->GetImage();

	// Get keyframe values for this frame
	float brightness_value = brightness.GetValue(frame_number);
	float contrast_value = contrast.GetValue(frame_number);

    Podcastle::Effects::applyBrightnessEffect(frame_image->bits(), frame_image->width(), frame_image->height(), brightness_value, contrast_value);

	// return the modified frame
	return frame;
}

// Generate JSON string of this object
std::string Brightness::Json() const {

	// Return formatted string
	return JsonValue().toStyledString();
}

// Generate Json::Value for this object
Json::Value Brightness::JsonValue() const {

	// Create root json object
	Json::Value root = EffectBase::JsonValue(); // get parent properties
	root["type"] = info.class_name;
	root["brightness"] = brightness.JsonValue();
	root["contrast"] = contrast.JsonValue();

	// return JsonValue
	return root;
}

// Load JSON string into this object
void Brightness::SetJson(const std::string value) {

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
void Brightness::SetJsonValue(const Json::Value root) {

	// Set parent data
	EffectBase::SetJsonValue(root);

	// Set data from Json (if key is found)
	if (!root["brightness"].isNull())
		brightness.SetJsonValue(root["brightness"]);
	if (!root["contrast"].isNull())
		contrast.SetJsonValue(root["contrast"]);
}

// Get all properties for a specific frame
std::string Brightness::PropertiesJSON(int64_t requested_frame) const {

	// Generate JSON properties list
	Json::Value root = BasePropertiesJSON(requested_frame);

	// Keyframes
	root["brightness"] = add_property_json("Brightness", brightness.GetValue(requested_frame), "float", "", &brightness, -1.0, 1.0, false, requested_frame);
	root["contrast"] = add_property_json("Contrast", contrast.GetValue(requested_frame), "float", "", &contrast, -128, 128.0, false, requested_frame);

	// Return formatted string
	return root.toStyledString();
}
