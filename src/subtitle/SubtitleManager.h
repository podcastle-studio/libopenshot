#pragma once

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
    void setEnabled(bool enable) const;
    bool isEnabled() const;

    void setDefaultStyle(const SubtitleTextStyle& style) const;
    SubtitleTextStyle& getDefaultStyle() const;

    void setMaxWidth(float width) const;

    // Segment management
    void addSegment(const SubtitleSegment& segment) const;
    void clearSegments() const;
    std::vector<SubtitleSegment>& getSegments() const;

    // Load from JSON
    void loadFromJSON(const std::string& jsonPath) const;

    // Rendering
    void renderAtFrame(std::shared_ptr<QImage> frameImage, int64_t frameNumber) const;

    // Utility
    bool hasActiveSubtitlesAtFrame(int64_t frameNumber) const;

private:
    class Impl;
    std::unique_ptr<Impl> pImpl;

    SegmentSettings parseSegmentSettings(const Json::Value& settingsJson) const;
    static void parseTextStyle(const Json::Value& styleJson, SubtitleTextStyle& style) ;
    static void parseAnimationSettings(const Json::Value& animJson, AnimationSettings& settings) ;

    void parseGlobalSettings(const Json::Value& settingsJson) const;
};
}
}

