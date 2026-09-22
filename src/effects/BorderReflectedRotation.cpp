#include "BorderReflectedRotation.h"

#include "skia/include/core/SkM44.h"
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

// The SkSL twin of applyBorderReflectedRotationEffect.
//
// **This one samples NEAREST**, not linear -- the C++ passes cv::INTER_NEAREST with the
// INTER_LINEAR alternative commented out beside it, which CLAUDE.md records as a deliberate
// choice shared with the front end's WASM build. So there is no interpolation to approximate here
// and the only thing between this and bit-exactness is where the rounding falls.
//
// warpAffine maps destination to source through the INVERSE of the matrix it is given, so the
// inverse is computed on the host by OpenCV itself (getRotationMatrix2D then
// invertAffineTransform) rather than re-derived here -- that way the matrix is OpenCV's own, and
// only the sampling is this fragment's.
const char* BorderReflectedRotation::GpuShaderSource() const
{
	return R"SKSL(
uniform float2 size;
uniform float3 invRow0;   // the inverse affine, row 0: x' = a*x + b*y + c
uniform float3 invRow1;

float4 main(float2 p) {
	// warpAffine maps the destination pixel's INTEGER coordinate through the matrix, not its
	// centre. Using the centre instead is a half-pixel error, which on smooth content reads as
	// ~1 LSB and on anything high-frequency reads as the wrong pixel entirely -- measured 11 dB
	// on a noise image against 55 dB on a ramp, which is what made it obvious.
	float2 q = floor(p);

	// warpAffine does not evaluate the map in floating point. It precomputes per-column and
	// per-row terms in 10-bit fixed point and adds them as integers:
	//
	//     adelta[x] = round(M0 * x * 1024)
	//     X0        = round((M1 * y + M2) * 1024) + 512      (the +512 is nearest's rounding)
	//     srcX      = (X0 + adelta[x]) >> 10
	//
	// Doing it in float instead is right to within a fraction of a pixel, which is invisible on
	// smooth content and completely wrong on anything high-frequency: it left 252 pixels of a
	// noise image sampling the wrong texel, at 39 dB. Reproducing the fixed-point pipeline costs
	// two rounds and a shift, and osIDiv makes the shift exact on a GPU that is allowed 2.5 ULP
	// on a divide.
	float ax = floor(invRow0.x * q.x * 1024.0 + 0.5);
	float x0 = floor((invRow0.y * q.y + invRow0.z) * 1024.0 + 0.5) + 512.0;
	float ay = floor(invRow1.x * q.x * 1024.0 + 0.5);
	float y0 = floor((invRow1.y * q.y + invRow1.z) * 1024.0 + 0.5) + 512.0;

	float2 src = float2(osIDiv(x0 + ax, 1024.0), osIDiv(y0 + ay, 1024.0));
	return osBytes(osReflect2(src, size) + 0.5) / 255.0;
}
)SKSL";
}

bool BorderReflectedRotation::SetGpuUniforms(SkRuntimeEffectBuilder& builder, int64_t frame_number,
											 int width, int height) const
{
	const double angle_value = angle.GetValue(frame_number);
	// The C++ returns the frame untouched for these, so decline rather than run an identity pass.
	if (angle_value == 0 || std::abs(angle_value) == 360)
		return false;

	const cv::Point2f center(width / 2.0F, height / 2.0F);
	const cv::Mat forward = cv::getRotationMatrix2D(center, -angle_value, 1.0);
	cv::Mat inverse;
	cv::invertAffineTransform(forward, inverse);

	builder.uniform("size") = SkV2{static_cast<float>(width), static_cast<float>(height)};
	builder.uniform("invRow0") = SkV3{static_cast<float>(inverse.at<double>(0, 0)),
									  static_cast<float>(inverse.at<double>(0, 1)),
									  static_cast<float>(inverse.at<double>(0, 2))};
	builder.uniform("invRow1") = SkV3{static_cast<float>(inverse.at<double>(1, 0)),
									  static_cast<float>(inverse.at<double>(1, 1)),
									  static_cast<float>(inverse.at<double>(1, 2))};
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
