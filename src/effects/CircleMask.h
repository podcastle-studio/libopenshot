#ifndef OPENSHOT_CIRCLE_MASK_EFFECT_H
#define OPENSHOT_CIRCLE_MASK_EFFECT_H

#include "../GpuEffect.h"

#include "../Json.h"
#include "../KeyFrame.h"

#include <string>
#include <memory>

namespace openshot
{
	// Forward declaration
	class ReaderBase;

	class CircleMask : public GpuEffect
	{
		/// Init effect settings
		void init_effect_details();

	protected:
		/// The SkSL twin of applyCircleMaskEffect, with an analytic edge where the C++ has
		/// OpenCV's rasterised one (see circle_mask.sksl).
		const char* GpuShaderSource() const override;

		/// The circle's centre and radius, from the shared planner.
		bool SetGpuUniforms(SkRuntimeEffectBuilder& builder, int64_t frame_number,
							int width, int height) const override;

		/// The planner's answer, so a radius of 0 is cleared on the GPU instead of being
		/// read back for the C++'s zero fill.
		bool PlanForFrame(int64_t frame_number, int width, int height,
						  Podcastle::Effects::EffectPlan& plan) const override;

	public:

        Keyframe circleRadius;

		/// Blank constructor, useful when using Json to load the effect properties
		CircleMask();

		explicit CircleMask(Keyframe _circleRadius);

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
	};

}

#endif
