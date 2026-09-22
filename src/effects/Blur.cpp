/**
 * @file
 * @brief Source file for Blur effect class
 * @author Jonathan Thomas <jonathan@openshot.org>
 *
 * @ref License
 */

// Copyright (c) 2008-2019 OpenShot Studios, LLC
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "Blur.h"
#include "Exceptions.h"
#include "MagickUtilities.h"
#include "skia/include/core/SkM44.h"
#include "EffectShaders.h"
#include "../gpu/GpuDevice.h"

#include <algorithm>
#include <cstddef>
#include "./image-processing-lib/src/Effects/effects.h"

#include "skia/include/effects/SkRuntimeEffect.h"

using namespace openshot;

namespace
{

// Credit: http://blog.ivank.net/fastest-gaussian-blur.html (MIT License)
// Modified to process all four channels in a pixel array
void boxBlurH(const unsigned char *scl, unsigned char *tcl, int w, int h, int r) {
    float iarr = 1.0 / (r + r + 1);

    #pragma omp parallel for shared (scl, tcl)
    for (int i = 0; i < h; ++i) {
        for (int ch = 0; ch < 4; ++ch) {
            int ti = i * w, li = ti, ri = ti + r;
            int fv = scl[ti * 4 + ch], lv = scl[(ti + w - 1) * 4 + ch], val = (r + 1) * fv;
            for (int j = 0; j < r; ++j) {
                val += scl[(ti + j) * 4 + ch];
            }
            for (int j = 0; j <= r; ++j) {
                val += scl[ri++ * 4 + ch] - fv;
                tcl[ti++ * 4 + ch] = round(val * iarr);
            }
            for (int j = r + 1; j < w - r; ++j) {
                val += scl[ri++ * 4 + ch] - scl[li++ * 4 + ch];
                tcl[ti++ * 4 + ch] = round(val * iarr);
            }
            for (int j = w - r; j < w; ++j) {
                val += lv - scl[li++ * 4 + ch];
                tcl[ti++ * 4 + ch] = round(val * iarr);
            }
        }
    }
}

void boxBlurT(const unsigned char *scl, unsigned char *tcl, int w, int h, int r) {
    float iarr = 1.0 / (r + r + 1);

    #pragma omp parallel for shared (scl, tcl)
    for (int i = 0; i < w; i++) {
        for (int ch = 0; ch < 4; ++ch) {
            int ti = i, li = ti, ri = ti + r * w;
            int fv = scl[ti * 4 + ch], lv = scl[(ti + w * (h - 1)) * 4 + ch], val = (r + 1) * fv;
            for (int j = 0; j < r; j++) val += scl[(ti + j * w) * 4 + ch];
            for (int j = 0; j <= r; j++) {
                val += scl[ri * 4 + ch] - fv;
                tcl[ti * 4 + ch] = round(val * iarr);
                ri += w;
                ti += w;
            }
            for (int j = r + 1; j < h - r; j++) {
                val += scl[ri * 4 + ch] - scl[li * 4 + ch];
                tcl[ti * 4 + ch] = round(val * iarr);
                li += w;
                ri += w;
                ti += w;
            }
            for (int j = h - r; j < h; j++) {
                val += lv - scl[li * 4 + ch];
                tcl[ti * 4 + ch] = round(val * iarr);
                li += w;
                ti += w;
            }
        }
    }
}

}

/// Blank constructor, useful when using Json to load the effect properties
Blur::Blur()
    : horizontal_radius(6.0), vertical_radius(6.0), diagonal_radius(0)
    , radial_blur_angle(0), sigma(3.0), iterations(3.0) {
	// Init effect properties
	init_effect_details();
}

// Default constructor
Blur::Blur(const Keyframe& new_horizontal_radius, const Keyframe& new_vertical_radius, const Keyframe& new_diagonal_radius, const Keyframe& new_radial_blur_angle,
           const Keyframe& new_zoom_blur_radius, const Keyframe& new_zoomBlurCenterX, const Keyframe& new_zoomBlurCenterY,
           const Keyframe& new_sigma, const Keyframe& new_iterations) :
		horizontal_radius(new_horizontal_radius), vertical_radius(new_vertical_radius), radial_blur_angle(new_radial_blur_angle),
        diagonal_radius(new_diagonal_radius), zoom_blur_radius(new_zoom_blur_radius), sigma(new_sigma), iterations(new_iterations),
        zoomBlurCenterX(new_zoomBlurCenterX), zoomBlurCenterY(new_zoomBlurCenterY)
{
	// Init effect properties
	init_effect_details();
}

// Init effect settings
void Blur::init_effect_details()
{
	/// Initialize the values of the EffectInfo struct.
	InitEffectInfo();

	/// Set the effect info
	info.class_name = "Blur";
	info.name = "Blur";
	info.description = "Adjust the blur of the frame's image.";
	info.has_audio = false;
	info.has_video = true;
}

// This method is required for all derived classes of EffectBase, and returns a
// modified openshot::Frame object
std::shared_ptr<openshot::Frame> Blur::GetFrame(std::shared_ptr<openshot::Frame> frame, int64_t frame_number)
{
	// Get the current blur radius
	const auto horizontal_radius_value = horizontal_radius.GetValue(frame_number);
	const auto vertical_radius_value = vertical_radius.GetValue(frame_number);
	const auto diagonal_radius_value = diagonal_radius.GetValue(frame_number);
	const auto zoom_blur_radius_value = zoom_blur_radius.GetValue(frame_number);
	const auto radial_blur_angle_value = radial_blur_angle.GetValue(frame_number);

    // diagonal blur (if any)
    if (diagonal_radius_value > 0) {
        if (!ApplyDiagonalBlurOnGpu(frame, frame_number, diagonal_radius_value)) {
            auto imageCv = frame->GetImageCV();
            Podcastle::Effects::applyDiagonalBlurEffect(imageCv, diagonal_radius_value);
            frame->SetImageCV(imageCv);
        }
    }

    // radial blur (if any)
    if (radial_blur_angle_value > 0) {
        if (!ApplyRotationalBlurOnGpu(frame, frame_number, radial_blur_angle_value)) {
            auto imageCv = frame->GetImageCV();
            Podcastle::Effects::applyRotationalBlur(imageCv, radial_blur_angle_value);
            frame->SetImageCV(imageCv);
        }
    }

    // zoom blur (if any)
    if (zoom_blur_radius_value > 0) {
        const auto centerPoint = std::make_pair(zoomBlurCenterX.GetValue(frame_number), zoomBlurCenterY.GetValue(frame_number));
        // Not a shader: see doc/gpu-migration/spikes/zoom-blur-polar/. The port is written and
        // measured, and it is the inverse polar map's angle that stops it.
        auto imageCv = frame->GetImageCV();
        Podcastle::Effects::applyZoomBlurEffect(imageCv, zoom_blur_radius_value, centerPoint);
        frame->SetImageCV(imageCv);
    }

    if (horizontal_radius_value > 0 || vertical_radius_value > 0) {
        // The shader when there is a GPU to run it on, OpenCV otherwise. Before GetImageCV(),
        // which on a GPU-backed frame is a readback and two full-frame cv::Mat conversions --
        // and this effect sits in the middle of the {Zoom, Blur, Alpha} transition, so getting
        // that round the wrong way is what used to split the chain in two.
        if (!ApplyBoxBlurOnGpu(frame, frame_number, horizontal_radius_value, vertical_radius_value)) {
            auto imageCv = frame->GetImageCV();
            Podcastle::Effects::applyBlurEffect(imageCv, horizontal_radius_value, vertical_radius_value);
            frame->SetImageCV(imageCv);
        }
    }

	// return the modified frame
	return frame;
}

/* ---------- GPU ---------- */

// The largest kernel blur.sksl's loop can run. It matches the constant in the fragment, and
// covers the widest pass this effect's own parameter range can ask for (radius 100 at 4K is 173
// taps). A frame that somehow asks for more declines rather than blurring by the wrong amount.
static constexpr int kMaxBlurTaps = 255;

// The same, for diagonal_blur.sksl's loop.
static constexpr int kMaxDiagonalTaps = 513;

// The box blur as up to six fragment passes.
//
// applyBlurEffect is three cv::blur calls, and each of those is itself separable: an exact
// integer row sum, then an exact integer column sum, then one rounding to the byte. This runs the
// same two halves as two draws, which is what makes the cost proportional to the kernel width
// rather than to its square -- the reasoning, and what the extra intermediate rounding costs, is
// in shaders/blur.sksl.
//
// A half whose kernel is one tap is the identity, so it is skipped rather than drawn: round(s/1)
// is s. That is bit-identical and saves a pass whenever only one axis is blurred, which is what
// the vocabulary's horizontal-only and vertical-only transitions do.
bool Blur::ApplyBoxBlurOnGpu(std::shared_ptr<openshot::Frame> frame, int64_t frame_number,
                             int horizontal, int vertical)
{
    if (!frame)
        return false;
    // Asked before anything is drawn: every reason to decline has to be found up front, because
    // a pass that has already run left its result as the frame's pixels and the C++ twin would
    // then blur an already-blurred frame.
    if (!GpuDevice::Instance().available())
        return false;

    const int width = frame->GetWidth();
    const int height = frame->GetHeight();
    if (width <= 0 || height <= 0)
        return false;

    // Resolved by the same function the CPU twin calls, so the two cannot disagree about what
    // this frame's radii mean.
    const Podcastle::Effects::BlurBoxes boxes =
        Podcastle::Effects::blurBoxSizes(width, horizontal, vertical);
    if (boxes.identity)
        return true;   // applyBlurEffect returns the image untouched, so there is nothing to run

    for (int pass = 0; pass < 3; ++pass)
        if (boxes.x[pass] > kMaxBlurTaps || boxes.y[pass] > kMaxBlurTaps)
            return false;

    for (int pass = 0; pass < 3; ++pass) {
        const int taps[2] = {boxes.x[pass], boxes.y[pass]};
        for (int axis = 0; axis < 2; ++axis) {
            if (taps[axis] <= 1)
                continue;   // the identity half
            gpu_pass = GpuPass::Box;
            gpu_dir_x = axis == 0 ? 1.0f : 0.0f;
            gpu_dir_y = axis == 0 ? 0.0f : 1.0f;
            gpu_taps = static_cast<float>(taps[axis]);
            if (!ApplyOnGpu(frame, frame_number))
                return false;
        }
    }
    return true;
}

// The diagonal blur as one fragment pass.
//
// Only for the sizes the C++ does not downscale: above a megapixel it halves the image with
// INTER_AREA, blurs, rounds to 8 bit and upsamples with INTER_LINEAR, and both the resampling and
// the change of size are things ApplyOnGpu cannot express. See shaders/diagonal_blur.sksl.
bool Blur::ApplyDiagonalBlurOnGpu(std::shared_ptr<openshot::Frame> frame, int64_t frame_number,
                                  int authored_radius)
{
    if (!frame || !GpuDevice::Instance().available())
        return false;

    const int width = frame->GetWidth();
    const int height = frame->GetHeight();
    if (width <= 0 || height <= 0)
        return false;

    // applyDiagonalBlurEffect's own threshold, read from the C++ rather than restated: above it
    // the effect works at half size and this fragment does not apply.
    constexpr std::size_t kLargeImageThreshold = 1000000;
    if (static_cast<std::size_t>(width) * static_cast<std::size_t>(height) > kLargeImageThreshold)
        return false;

    // The same parameter arithmetic the C++ runs, through the same function.
    const int blur_amount = std::max(1, Podcastle::Effects::scaledLength(width, authored_radius));
    const int radius = std::max(1, blur_amount / 2);
    const int kernel_size = radius * 2 + 1;
    if (kernel_size > kMaxDiagonalTaps)
        return false;

    gpu_pass = GpuPass::Diagonal;
    gpu_diag_taps = static_cast<float>(kernel_size);
    gpu_diag_radius = static_cast<float>(radius);
    // The C++ multiplies by this float reciprocal rather than dividing, so the host computes it
    // the same way and in the same precision. Dividing in the fragment would be more accurate and
    // therefore wrong.
    gpu_diag_inv_taps = 1.0f / static_cast<float>(kernel_size);
    return ApplyOnGpu(frame, frame_number);
}

// The rotational blur as one fragment pass.
//
// Only for the widths the C++ works at natively: above the reference width it resizes, blurs the
// copy and resizes back, which is a resample at a different size and ApplyOnGpu cannot express it.
//
// Everything the fragment needs is resolved here, including thirty of OpenCV's own inverse affine
// matrices -- warpAffine inverts the cv::Matx23f it is handed, in double, and re-deriving that
// from the angle in the shader would be a different matrix in the last bits.
bool Blur::ApplyRotationalBlurOnGpu(std::shared_ptr<openshot::Frame> frame, int64_t frame_number,
                                    double angle_degrees)
{
    if (!frame || !GpuDevice::Instance().available())
        return false;

    const int width = frame->GetWidth();
    const int height = frame->GetHeight();
    if (width <= 0 || height <= 0)
        return false;

    const double abs_blur = std::abs(angle_degrees);
    if (abs_blur < 0.1)
        return true;   // the C++ calls this negligible and returns the frame untouched

    // Above the reference width the effect works on a resized copy. See the shader.
    if (Podcastle::Effects::referenceWorkingScale(width) != 1.0)
        return false;

    const int iterations = abs_blur < 15.0
        ? std::max(8, std::min(25, static_cast<int>(abs_blur * 1.2)))
        : std::max(3, std::min(30, static_cast<int>(abs_blur * 0.6)));
    if (iterations > kMaxRotationalIterations)
        return false;

    const float center_x = static_cast<float>(width) / 2.0f;
    const float center_y = static_cast<float>(height) / 2.0f;
    const double max_angle_rad = angle_degrees * CV_PI / 180.0;

    for (int i = 0; i < iterations; ++i) {
        double angle_rad;
        if (abs_blur < 15.0) {
            // The C++'s "avoid exact centre / spokes" nudge, reproduced rather than tidied away.
            constexpr double offset = 0.01;
            angle_rad = (((static_cast<double>(i) + offset) / iterations) - 0.5) * max_angle_rad;
        } else if (iterations == 1) {
            angle_rad = -max_angle_rad / 2.0;
        } else {
            angle_rad = ((static_cast<double>(i) / (iterations - 1)) - 0.5) * max_angle_rad;
        }

        // Matx23f, not Mat: the C++ stores the matrix as float32 before warpAffine widens it
        // back to double and inverts it, so the float32 rounding is part of the answer.
        const cv::Matx23f m = cv::getRotationMatrix2D(cv::Point2f(center_x, center_y),
                                                      angle_rad * 180.0 / CV_PI, 1.0);
        cv::Mat m_double;
        cv::Mat(m).convertTo(m_double, CV_64F);
        cv::Mat m_inverse;
        cv::invertAffineTransform(m_double, m_inverse);

        for (int c = 0; c < 3; ++c) {
            gpu_rot_inv_row0[i * 4 + c] = static_cast<float>(m_inverse.at<double>(0, c));
            gpu_rot_inv_row1[i * 4 + c] = static_cast<float>(m_inverse.at<double>(1, c));
        }
        gpu_rot_inv_row0[i * 4 + 3] = 0.0f;
        gpu_rot_inv_row1[i * 4 + 3] = 0.0f;
    }
    for (int i = iterations; i < kMaxRotationalIterations; ++i)
        for (int c = 0; c < 4; ++c) {
            gpu_rot_inv_row0[i * 4 + c] = 0.0f;
            gpu_rot_inv_row1[i * 4 + c] = 0.0f;
        }

    gpu_pass = GpuPass::Rotational;
    gpu_rot_iters = static_cast<float>(iterations);
    // The C++'s `weight`, in float, because that is the precision it multiplies by.
    gpu_rot_inv_iters = 1.0f / static_cast<float>(iterations);
    gpu_rot_use_reflect = abs_blur < 10.0 ? 0.0f : 1.0f;
    return ApplyOnGpu(frame, frame_number);
}

// The shared SkSL sources live in image-processing-lib/shaders/ so the editor loads the same
// bytes through CanvasKit; they are embedded here at build time. Read them there -- including
// why each is written the way it is.
const char* Blur::GpuShaderSource() const
{
	switch (gpu_pass) {
	case GpuPass::Diagonal:   return openshot::shaders::kDiagonalBlur;
	case GpuPass::Rotational: return openshot::shaders::kRotationalBlur;
	case GpuPass::Box:        break;
	}
	return openshot::shaders::kBlur;
}

bool Blur::SetGpuUniforms(SkRuntimeEffectBuilder& builder, int64_t frame_number,
						  int width, int height) const
{
	builder.uniform("size") = SkV2{static_cast<float>(width), static_cast<float>(height)};
	switch (gpu_pass) {
	case GpuPass::Box:
		builder.uniform("dir") = SkV2{gpu_dir_x, gpu_dir_y};
		builder.uniform("taps") = gpu_taps;
		break;
	case GpuPass::Diagonal:
		builder.uniform("taps") = gpu_diag_taps;
		builder.uniform("radius") = gpu_diag_radius;
		builder.uniform("invTaps") = gpu_diag_inv_taps;
		break;
	case GpuPass::Rotational:
		builder.uniform("iters") = gpu_rot_iters;
		builder.uniform("invIters") = gpu_rot_inv_iters;
		builder.uniform("useReflect") = gpu_rot_use_reflect;
		builder.uniform("invRow0").set(gpu_rot_inv_row0, kMaxRotationalIterations * 4);
		builder.uniform("invRow1").set(gpu_rot_inv_row1, kMaxRotationalIterations * 4);
		break;
	}
	return true;
}

// Generate JSON string of this object
std::string Blur::Json() const {

	// Return formatted string
	return JsonValue().toStyledString();
}

// Generate Json::Value for this object
Json::Value Blur::JsonValue() const {
	// Create root json object
	Json::Value root = EffectBase::JsonValue(); // get parent properties
	root["type"] = info.class_name;
	root["horizontal_radius"] = horizontal_radius.JsonValue();
	root["vertical_radius"] = vertical_radius.JsonValue();
	root["diagonal_radius"] = diagonal_radius.JsonValue();
	root["sigma"] = sigma.JsonValue();
	root["iterations"] = iterations.JsonValue();

	// return JsonValue
	return root;
}

// Load JSON string into this object
void Blur::SetJson(const std::string value) {

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
void Blur::SetJsonValue(const Json::Value root) {

	// Set parent data
	EffectBase::SetJsonValue(root);

	// Set data from Json (if key is found)
	if (!root["horizontal_radius"].isNull())
		horizontal_radius.SetJsonValue(root["horizontal_radius"]);
	if (!root["vertical_radius"].isNull())
		vertical_radius.SetJsonValue(root["vertical_radius"]);
    if (!root["diagonal_radius"].isNull())
        diagonal_radius.SetJsonValue(root["diagonal_radius"]);
	if (!root["sigma"].isNull())
		sigma.SetJsonValue(root["sigma"]);
	if (!root["iterations"].isNull())
		iterations.SetJsonValue(root["iterations"]);
}

// Get all properties for a specific frame
std::string Blur::PropertiesJSON(int64_t requested_frame) const {

	// Generate JSON properties list
	Json::Value root = BasePropertiesJSON(requested_frame);

	// Keyframes
	root["horizontal_radius"] = add_property_json("Horizontal Radius", horizontal_radius.GetValue(requested_frame), "float", "", &horizontal_radius, 0, 100, false, requested_frame);
	root["vertical_radius"] = add_property_json("Vertical Radius", vertical_radius.GetValue(requested_frame), "float", "", &vertical_radius, 0, 100, false, requested_frame);
	root["diagonal_radius"] = add_property_json("Diagonal Radius", diagonal_radius.GetValue(requested_frame), "float", "", &diagonal_radius, 0, 100, false, requested_frame);
	root["sigma"] = add_property_json("Sigma", sigma.GetValue(requested_frame), "float", "", &sigma, 0, 100, false, requested_frame);
	root["iterations"] = add_property_json("Iterations", iterations.GetValue(requested_frame), "float", "", &iterations, 0, 100, false, requested_frame);

	// Return formatted string
	return root.toStyledString();
}
