#pragma once

#include "SubtitleTypes.h"

namespace openshot {
namespace subtitle {

// Transform text based on style
std::string transformText(const std::string& text, const SubtitleTextStyle& style);

// Get animated value from keyframes using OpenShot's Keyframe class
double getAnimatedValue(const std::vector<Point>& keyframes, float timeMs, float fps);

// Get animated color value
std::string getAnimatedColor(const std::vector<std::pair<float, std::string>>& keyframes, float timeMs, InterpolationType interpolation);

// Apply animation parameters to style
SubtitleTextStyle applyAnimationParams(const std::vector<AnimationParam>& params, float timeMs, float fps, const SubtitleTextStyle& baseStyle);

// Convert milliseconds to frame number
int64_t msToFrame(const float ms, const float fps);

// Convert frame number to milliseconds
float frameToMs(const int64_t frame, const float fps);

// Process segment animation
std::vector<WordAnimation> processSegmentAnimation(const std::vector<WordDetail>& wordDetails, const SegmentSettings& settings, const float fps);

}

} // namespace openshot