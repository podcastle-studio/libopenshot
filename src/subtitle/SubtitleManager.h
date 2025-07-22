#pragma once

#include "SubtitleTypes.h"

#include "json/json.h"
#include <vector>
#include <memory>
#include <string>


// Forward declarations
class QImage;
class SkCanvas;

namespace openshot {
namespace subtitle {

// Forward declarations for subtitle types
struct SubtitleSegment;
struct SubtitleTextStyle;
struct SegmentSettings;
struct AnimationSettings;

class SubtitleManager {
public:
    explicit SubtitleManager(float fps);
    ~SubtitleManager();

    // Configuration
    void setEnabled(bool enable);
    bool isEnabled() const;

    void setDefaultStyle(const SubtitleTextStyle& style);
    SubtitleTextStyle& getDefaultStyle();

    void setMaxWidth(float width);

    // Segment management
    void addSegment(const SubtitleSegment& segment);
    void clearSegments();
    std::vector<SubtitleSegment>& getSegments();

    // Load from JSON
    void loadFromJSON(const std::string& jsonPath);
    void loadFromJSONString(const std::string& jsonString);

    // Rendering
    void renderAtFrame(std::shared_ptr<QImage> frameImage, int64_t frameNumber) const;

    // Utility
    bool hasActiveSubtitlesAtFrame(int64_t frameNumber) const;

private:
    std::vector<SubtitleSegment> segments;
    SegmentSettings defaultSettings;
    bool enabled = true;
    float fps;

    SegmentSettings parseSegmentSettings(const Json::Value& settingsJson) const;
    static void parseTextStyle(const Json::Value& styleJson, SubtitleTextStyle& style);
    static void parseAnimationSettings(const Json::Value& animJson, AnimationSettings& settings);

    void parseGlobalSettings(const Json::Value& settingsJson);
    void parseJSONRoot(const Json::Value& root);
};
}
}