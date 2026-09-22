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
		/// The SkSL twin of applyCircleMaskEffect's per-pixel half. The circle itself is
		/// rasterised by OpenCV on the CPU and uploaded -- see the .cpp.
		const char* GpuShaderSource() const override;

		/// The coverage mask as a texture, or false when there is nothing to apply.
		bool SetGpuUniforms(SkRuntimeEffectBuilder& builder, int64_t frame_number,
							int width, int height) const override;

	private:
		/// The rasterised circle, cached across frames and keyed on the radius, the frame
		/// size and GpuDevice::Generation(). Defined in the .cpp because this header is
		/// installed and compiled without Skia on the include path.
		struct CoverageCache;
		mutable std::shared_ptr<CoverageCache> coverage;

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
