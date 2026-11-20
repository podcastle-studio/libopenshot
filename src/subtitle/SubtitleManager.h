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

    void addSegment(const SubtitleSegment& segment);

    // Load from JSON
    void loadFromJSON(const std::string& jsonPath);
    void loadFromJSONString(const std::string& jsonString);

    // Rendering.
    //
    // The canvas overload is the real entry point: subtitles are drawn straight
    // onto whatever surface the caller already has, and nothing under here builds
    // an offscreen of its own, so a GPU-backed canvas keeps the entire subtitle
    // pass on the GPU and a raster one behaves exactly as it always has. That is
    // the whole of what subtitles need to run on the GPU — see the note on the
    // QImage overload for why the Timeline still hands it a raster canvas.
    // @a convention must match where the canvas's pixels eventually go: the default
    // suits a canvas that is read back through an N32 pixmap into a QImage, and
    // ColorConvention::Logical suits one composited and read back as kRGBA_8888 (the
    // Timeline's GPU canvas). Getting it wrong exchanges red and blue -- see the enum.
    void renderAtFrame(SkCanvas* canvas, float canvasWidth, float canvasHeight,
                       int64_t frameNumber,
                       ColorConvention convention = ColorConvention::QImageBytes) const;

    // Convenience for a caller holding a raster QImage: wraps the image's pixels
    // in a raster canvas and draws onto them in place.
    //
    // Deliberately NOT routed through a GPU surface of its own. Subtitles composite
    // onto an existing video frame, so doing that here means uploading the frame and
    // reading it back, which costs far more than the drawing it replaces. A caller
    // whose frame is ALREADY on the GPU uses the canvas overload above instead and
    // pays no transfer at all -- that is what Timeline::GetFrame does since W17.
    // This overload remains the whole of the CPU path and a no-GPU machine's route.
    void renderAtFrame(std::shared_ptr<QImage> frameImage, int64_t frameNumber) const;

    bool hasActiveSubtitlesAtFrame(const int64_t frameNumber) const;

private:
    std::vector<SubtitleSegment> segments;
    SegmentSettings defaultSettings;
    float fps;

    SegmentSettings parseSegmentSettings(const Json::Value& settingsJson) const;
    static void parseTextStyle(const Json::Value& styleJson, SubtitleTextStyle& style);
    static void parseAnimationSettings(const Json::Value& animJson, AnimationSettings& settings);

    void parseGlobalSettings(const Json::Value& settingsJson);
    void parseJSONRoot(const Json::Value& root);
};
}
}