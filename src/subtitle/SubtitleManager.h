#pragma once

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

class SubtitleManager {
private:
    class Impl; // Private implementation
    std::unique_ptr<Impl> pImpl;

public:
    explicit SubtitleManager(float fps);
    ~SubtitleManager();

    // Configuration
    void setEnabled(bool enable) const;
    bool isEnabled() const;

    void setDefaultStyle(const subtitle::SubtitleTextStyle& style) const;
    SubtitleTextStyle& getDefaultStyle() const;

    void setMaxWidth(float width) const;

    // Segment management
    void addSegment(const subtitle::SubtitleSegment& segment) const;
    void clearSegments() const;
    std::vector<SubtitleSegment>& getSegments() const;

    // Load from JSON
    void loadFromJSON(const std::string& jsonPath) const;

    // Rendering
    void renderAtFrame(std::shared_ptr<QImage> frameImage, int64_t frameNumber) const;
    void renderAtTime(SkCanvas* canvas, float timeInSeconds) const;

    // Utility
    bool hasActiveSubtitlesAtFrame(int64_t frameNumber) const;
    void createExampleSubtitles() const;
};
}
}

