/**
 * @file
 * @brief Header file for Blur effect class
 * @author Jonathan Thomas <jonathan@openshot.org>
 *
 * @ref License
 */

// Copyright (c) 2008-2019 OpenShot Studios, LLC
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#ifndef OPENSHOT_BLUR_EFFECT_H
#define OPENSHOT_BLUR_EFFECT_H

#include "../GpuEffect.h"

#include "../Frame.h"
#include "../Json.h"
#include "../KeyFrame.h"

#include <map>
#include <memory>
#include <string>

namespace openshot
{

	/**
	 * @brief This class adjusts the blur of an image, and can be animated
	 * with openshot::Keyframe curves over time.
	 *
	 * Adjusting the blur of an image over time can create many different powerful effects. To achieve a
	 * box blur effect, use identical horizontal and vertical blur values. To achieve a Gaussian blur,
	 * use 3 iterations, a sigma of 3.0, and a radius between 3 and X (depending on how much blur you want).
	 */
	class Blur : public GpuEffect
	{
	public:
		Keyframe horizontal_radius;	///< Horizontal blur radius keyframe. The size of the horizontal blur operation in pixels.
		Keyframe vertical_radius;	///< Vertical blur radius keyframe. The size of the vertical blur operation in pixels.
		Keyframe diagonal_radius;   ///< Diagonal blur radius keyframe. The size of the diagonal blur operation in pixels.
		Keyframe radial_blur_angle;   ///< Radial blur angle keyframe. The size of the radial blur operation in pixels.
        Keyframe zoom_blur_radius;   ///< Zoom blur radius keyframe. The size of the zoom blur operation in pixels.

        Keyframe sigma;				///< Sigma keyframe. The amount of spread in the blur operation. Should be larger than radius.
		Keyframe iterations;		///< Iterations keyframe. The # of blur iterations per pixel. 3 iterations = Gaussian.

        Keyframe zoomBlurCenterX;   ///< Zoom blur center X keyframe. The X coordinate of the zoom blur center.
        Keyframe zoomBlurCenterY;   ///< Zoom blur center Y keyframe. The Y coordinate of the zoom blur center.

		/// Blank constructor, useful when using Json to load the effect properties
		Blur();

		/// Default constructor, which takes 1 curve. The curve adjusts the blur radius
		/// of a frame's image.
		///
		/// @param new_horizontal_radius The curve to adjust the horizontal blur radius (between 0 and 100, rounded to int)
		/// @param new_vertical_radius The curve to adjust the vertical blur radius (between 0 and 100, rounded to int)
		/// @param new_diagonal_radius The curve to adjust the diagonal blur radius (between 0 and 100, rounded to int)
		/// @param new_diagonal_direction
		/// @param new_sigma The curve to adjust the sigma amount (the size of the blur brush (between 0 and 100), float values accepted)
		/// @param new_iterations The curve to adjust the # of iterations (between 1 and 100)
		explicit Blur(const Keyframe& new_horizontal_radius, const Keyframe& new_vertical_radius = 0, const Keyframe& new_diagonal_radius = 0, const Keyframe& new_radial_blur_angle = 0,
             const Keyframe& new_zoom_blur_radius = 0, const Keyframe& new_zoomBlurCenterX = 0, const Keyframe& new_zoomBlurCenterY = 0,
             const Keyframe& new_sigma = 1, const Keyframe& new_iterations = 1);

		/// @brief This method is required for all derived classes of ClipBase, and returns a
		/// new openshot::Frame object. All Clip keyframes and effects are resolved into
		/// pixels.
		///
		/// @returns A new openshot::Frame object
		/// @param frame_number The frame number (starting at 1) of the clip or effect on the timeline.
		std::shared_ptr<openshot::Frame> GetFrame(int64_t frame_number) override { return GetFrame(std::make_shared<openshot::Frame>(), frame_number); }

		/// @brief This method is required for all derived classes of ClipBase, and returns a
		/// modified openshot::Frame object
		///
		/// The frame object is passed into this method and used as a starting point (pixels and audio).
		/// All Clip keyframes and effects are resolved into pixels.
		///
		/// @returns The modified openshot::Frame object
		/// @param frame The frame object that needs the clip or effect applied to it
		/// @param frame_number The frame number (starting at 1) of the clip or effect on the timeline.
		std::shared_ptr<openshot::Frame> GetFrame(std::shared_ptr<openshot::Frame> frame, int64_t frame_number) override;

		// Get and Set JSON methods
		std::string Json() const override; ///< Generate JSON string of this object
		void SetJson(const std::string value) override; ///< Load JSON string into this object
		Json::Value JsonValue() const override; ///< Generate Json::Value for this object
		void SetJsonValue(const Json::Value root) override; ///< Load Json::Value into this object

		/// Get all properties for a specific frame (perfect for a UI to display the current state
		/// of all properties at any time)
		std::string PropertiesJSON(int64_t requested_frame) const override;
	protected:
		/// The fragment for the pass GetFrame is about to run. Blur is four effects in
		/// one class and each is its own SkSL source, so this is not a constant — see
		/// GpuEffect::GpuShaderSource() on why the cache keys on the pointer.
		const char* GpuShaderSource() const override;

		/// Bind the pass GetFrame selected. Every value is resolved here rather than in
		/// the fragment, and by the same functions the CPU twin uses.
		bool SetGpuUniforms(SkRuntimeEffectBuilder& builder, int64_t frame_number,
							int width, int height) const override;

	private:
		/// Init effect settings
		void init_effect_details();

		/// The box blur as up to six fragment passes — horizontal and vertical for each
		/// of applyBlurEffect's three. False means nothing was touched and the caller
		/// must run the C++; it is decided before the first pass, so a half-blurred
		/// frame is not a state this can leave behind.
		bool ApplyBoxBlurOnGpu(std::shared_ptr<openshot::Frame> frame, int64_t frame_number,
							   int horizontal, int vertical);

		/// The diagonal blur as one fragment pass, for the frame sizes applyDiagonalBlurEffect
		/// does not downscale. False means the caller must run the C++.
		bool ApplyDiagonalBlurOnGpu(std::shared_ptr<openshot::Frame> frame, int64_t frame_number,
									int authored_radius);

		/// The rotational blur as one fragment pass, for the frame widths applyRotationalBlur
		/// does not downscale. False means the caller must run the C++.
		bool ApplyRotationalBlurOnGpu(std::shared_ptr<openshot::Frame> frame, int64_t frame_number,
									  double angle_degrees);

		/// The zoom blur as three passes -- forward polar, a box blur along rho, inverse
		/// polar -- with its own padded-size buffer in between. False means the caller
		/// must run the C++; nothing is attached to the frame until the last pass lands,
		/// so a failure half way through leaves the frame untouched.
		bool ApplyZoomBlurOnGpu(std::shared_ptr<openshot::Frame> frame, int64_t frame_number,
								int authored_strength, double center_x, double center_y);

		/// Plan one of the four modes through the shared planner and run it: true when it ran
		/// or had nothing to do, false when the caller must run the C++ (no GPU, or the plan
		/// is a CPU step -- a case no fragment covers).
		bool ApplyPlannedBlurOnGpu(std::shared_ptr<openshot::Frame> frame, int64_t frame_number,
								   const std::map<std::string, double>& params);
	};

}

#endif
