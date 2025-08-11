#pragma once

#include "SubtitleTypes.h"

namespace openshot {
namespace subtitle {

inline SubtitleTextStyle oneWordBase(const SubtitleTextStyle& s) {
    auto r = s;
    r.opacity           = 0;
    r.backgroundOpacity = 0;
    r.shadowOpacity     = 0;
    r.strokeOpacity     = 0;
    return r;
}

std::string transformText(const std::string& text, const SubtitleTextStyle& style, const SubtitleContainerStyle& containerStyle);

SubtitleTextStyle applyAnimationParams(const std::vector<AnimationParam>& params, const float timeMs, const float fps, const SubtitleTextStyle& baseStyle);

float frameToMs(const int64_t frame, const float fps);

// Process segment animation
std::vector<WordAnimation> processSegmentAnimation(const std::vector<WordDetail>& wordDetails,
        const SegmentSettings&         settings,
        float                          fps,
        const std::vector<std::vector<size_t>>& wordsPerLine = {});

}

} // namespace openshot