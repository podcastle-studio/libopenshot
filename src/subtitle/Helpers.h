#pragma once

#include "SubtitleTypes.h"

namespace openshot {
namespace subtitle {

// Transform text based on style
std::string transformText(const std::string& text, const SubtitleTextStyle& style);

// Apply animation parameters to style
SubtitleTextStyle applyAnimationParams(const std::vector<AnimationParam>& params,
    const float timeMs, const float fps, const SubtitleTextStyle& baseStyle);

// Convert frame number to milliseconds
float frameToMs(const int64_t frame, const float fps);

// Process segment animation
std::vector<WordAnimation> processSegmentAnimation(const std::vector<WordDetail>& wordDetails, const SegmentSettings& settings, const float fps);

}

} // namespace openshot