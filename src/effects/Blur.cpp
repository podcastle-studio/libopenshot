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

void diagonalBlur(cv::Mat& src, int blurAmount, int iterations = 1) {
    // cv::GaussianBlur(src, src, cv::Size(11, 11), 5);

    // Specify the kernel size. The greater the size, the more the blur.
    int kernelSize = blurAmount * 2 + 1; // Ensure kernel size is odd

    // Create the diagonal kernel.
    cv::Mat kernel_d = cv::Mat::zeros(kernelSize, kernelSize, CV_32F);

    // Fill the diagonal with ones and also normalize
    for (int i = 0; i < kernelSize; ++i) {
        kernel_d.at<float>(i, i) = 1.0f / kernelSize;
    }

    // Apply the diagonal kernel.
    for (int i = 0; i < iterations; ++i) {
        cv::filter2D(src, src, -1, kernel_d, cv::Point(-1, -1), 0, cv::BORDER_DEFAULT);
    }
}

cv::Mat zoomBlur(cv::Mat& src, int blurStrength, const std::pair<float, float>& center) {
    int width = src.cols;
    int height = src.rows;
    const cv::Point2f customCenter(center.first * width, center.second * height);

    // Ensure blur strength is odd to use it as kernel size
    if (blurStrength % 2 == 0) {
        blurStrength += 1;
    }

    // Calculate padding size based on blur strength to ensure no black borders
    int paddingSize = blurStrength;

    // Add mirrored padding around the image
    cv::Mat paddedSrc;
    cv::copyMakeBorder(src, paddedSrc, paddingSize, paddingSize, paddingSize, paddingSize, cv::BORDER_REFLECT);

    // Adjust center for the padded image
    const cv::Point2f paddedCenter(customCenter.x + paddingSize, customCenter.y + paddingSize);

    // Calculate the maximum radius to cover the whole image from the new center
    double maxRadius = 0.0;
    std::vector<cv::Point2f> corners = {
            cv::Point2f(0, 0),
            cv::Point2f(paddedSrc.cols - 1, 0),
            cv::Point2f(0, paddedSrc.rows - 1),
            cv::Point2f(paddedSrc.cols - 1, paddedSrc.rows - 1)
    };

    for (const auto& corner : corners) {
        double radius = cv::norm(corner - paddedCenter);
        maxRadius = std::max(maxRadius, radius);
    }

    // Convert to polar coordinates
    cv::Mat polarImage;
    cv::linearPolar(paddedSrc, polarImage, paddedCenter, maxRadius, cv::WARP_FILL_OUTLIERS);

    // Apply horizontal blur on the polar image
    cv::Mat blurredPolar;
    cv::blur(polarImage, blurredPolar, cv::Size(blurStrength, 1));

    // Convert back to Cartesian coordinates
    cv::Mat paddedResult;
    cv::linearPolar(blurredPolar, paddedResult, paddedCenter, maxRadius, cv::WARP_INVERSE_MAP);

    // Crop the padded result back to original size
    cv::Rect cropRegion(paddingSize, paddingSize, width, height);
    cv::Mat result = paddedResult(cropRegion);

    // Apply a standard blur to the final result
    int gaussBlurStrength = std::max(19, static_cast<int>(0.1 * blurStrength));
    if (gaussBlurStrength % 2 == 0) {
        gaussBlurStrength += 1;
    }
    // cv::GaussianBlur(result, result, cv::Size(gaussBlurStrength, gaussBlurStrength), 0);
    return result;
}

std::shared_ptr<QImage> radialBlur(std::shared_ptr<QImage> src, double amount) {
    // Load the input image
    auto image = openshot::QImage2Magick(src);
    // Apply radial blur
    image->rotationalBlur(amount);
    return openshot::Magick2QImage(image);
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
	int horizontal_radius_value = horizontal_radius.GetValue(frame_number);
	int vertical_radius_value = vertical_radius.GetValue(frame_number);
	int diagonal_radius_value = diagonal_radius.GetValue(frame_number);
	int zoom_blur_radius_value = zoom_blur_radius.GetValue(frame_number);
	int radial_blur_angle_value = radial_blur_angle.GetValue(frame_number);
	int iteration_value = iterations.GetInt(frame_number);

    // Get the frame's image
    std::shared_ptr<QImage> frame_image = frame->GetImage();

    // diagonal blur (if any)
    if (diagonal_radius_value > 0.0) {
        auto frame_image_cv = frame->GetImageCV();
        diagonalBlur(frame_image_cv, diagonal_radius_value, iteration_value);
        frame->SetImageCV(frame_image_cv);
    }

    // radia blur (if any)
    if (radial_blur_angle_value > 0.0) {
        frame->AddImage(radialBlur(frame_image, radial_blur_angle_value));
    }

    // zoom blur (if any)
    if (zoom_blur_radius_value > 0.0) {
        auto centerPoint = std::make_pair(zoomBlurCenterX.GetValue(frame_number), zoomBlurCenterY.GetValue(frame_number));
        auto src = frame->GetImageCV();
        frame->SetImageCV(zoomBlur(src, zoom_blur_radius_value, centerPoint));
    }

    // Loop through each iteration
    for (int iteration = 0; iteration < iteration_value; ++iteration)
    {
        int w = frame_image->width();
        int h = frame_image->height();

        // horizontal blur (if any)
        if (horizontal_radius_value > 0.0) {
            std::shared_ptr<QImage> frame_image_2 = std::make_shared<QImage>(*frame_image);

            // Apply horizontal blur to target RGBA channels
            boxBlurH(frame_image->bits(), frame_image_2->bits(), w, h, horizontal_radius_value);

            // Swap output image back to input
            frame_image.swap(frame_image_2);
        }

        // vertical blur (if any)
        if (vertical_radius_value > 0.0) {
            std::shared_ptr<QImage> frame_image_2 = std::make_shared<QImage>(*frame_image);

            // Apply vertical blur to target RGBA channels
            boxBlurT(frame_image->bits(), frame_image_2->bits(), w, h, vertical_radius_value);

            // Swap output image back to input
            frame_image.swap(frame_image_2);
        }
    }

    if (horizontal_radius_value > 0.0 || vertical_radius_value > 0.0)
        frame->AddImage(frame_image);

	// return the modified frame
	return frame;
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
