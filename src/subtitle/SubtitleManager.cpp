#include "SubtitleManager.h"
#include "SubtitleTypes.h"
#include "SubtitleRenderer.h"
#include "SkiaRenderer.h"
#include "Helpers.h"
#include "DefaultPreset.h"

#include <skia/include/core/SkBitmap.h>
#include <skia/include/core/SkCanvas.h>

#include <QImage>
#include <fstream>

namespace openshot {
namespace subtitle {

// Private implementation class
class SubtitleManager::Impl {
public:
    std::vector<SubtitleSegment> segments;
    SegmentSettings defaultSettings;
    bool enabled = true;
    float fps;

    explicit Impl(const float fps) : fps(fps) {
        // Initialize with default preset
        const SubtitlePreset preset = getDefaultPreset();
        defaultSettings = createSegmentSettings(preset);
    }
};

SubtitleManager::SubtitleManager(float fps)
    : pImpl(std::make_unique<Impl>(fps)) {}

SubtitleManager::~SubtitleManager() = default;

void SubtitleManager::setEnabled(const bool enable) const {
    pImpl->enabled = enable;
}

bool SubtitleManager::isEnabled() const {
    return pImpl->enabled;
}

void SubtitleManager::setDefaultStyle(const SubtitleTextStyle& style) const {
    pImpl->defaultSettings.defaultStyle = style;
}

SubtitleTextStyle& SubtitleManager::getDefaultStyle() const {
    return pImpl->defaultSettings.defaultStyle;
}

void SubtitleManager::setMaxWidth(const float width) const {
    pImpl->defaultSettings.transformation.maxWidth = width;
}

void SubtitleManager::addSegment(const SubtitleSegment& segment) const {
    pImpl->segments.push_back(segment);
}

void SubtitleManager::clearSegments() const {
    pImpl->segments.clear();
}

std::vector<SubtitleSegment>& SubtitleManager::getSegments() const {
    return pImpl->segments;
}

SegmentSettings SubtitleManager::parseSegmentSettings(const Json::Value& settingsJson) const {
    SegmentSettings settings = pImpl->defaultSettings; // Start with defaults

    // Parse containerStyle
    if (settingsJson.isMember("containerStyle")) {
        const Json::Value& containerStyle = settingsJson["containerStyle"];

        if (containerStyle.isMember("appearance")) {
            std::string appearance = containerStyle["appearance"].asString();
            if (appearance == "ONE_WORD") {
                settings.containerStyle.appearance = TextAppearance::ONE_WORD;
            } else {
                settings.containerStyle.appearance = TextAppearance::PER_TIME;
            }
        }

        if (containerStyle.isMember("textAlign")) {
            std::string align = containerStyle["textAlign"].asString();
            if (align == "LEFT") {
                settings.containerStyle.textAlign = TextAlignment::LEFT;
            } else if (align == "RIGHT") {
                settings.containerStyle.textAlign = TextAlignment::RIGHT;
            } else {
                settings.containerStyle.textAlign = TextAlignment::CENTER;
            }
        }

        if (containerStyle.isMember("opacity"))
            settings.containerStyle.opacity = containerStyle["opacity"].asFloat();
        if (containerStyle.isMember("paddingX"))
            settings.containerStyle.paddingX = containerStyle["paddingX"].asFloat();
        if (containerStyle.isMember("paddingY"))
            settings.containerStyle.paddingY = containerStyle["paddingY"].asFloat();
        if (containerStyle.isMember("radius"))
            settings.containerStyle.radius = containerStyle["radius"].asFloat();
        if (containerStyle.isMember("color"))
            settings.containerStyle.color = containerStyle["color"].asString();
    }

    // Parse defaultStyle
    if (settingsJson.isMember("defaultStyle")) {
        parseTextStyle(settingsJson["defaultStyle"], settings.defaultStyle);
    }

    // Parse animationSettings
    if (settingsJson.isMember("animationSettings")) {
        parseAnimationSettings(settingsJson["animationSettings"], settings.animationSettings);
    }

    // Parse transformation
    if (settingsJson.isMember("transformation")) {
        const Json::Value& transform = settingsJson["transformation"];

        if (transform.isMember("maxWidth"))
            settings.transformation.maxWidth = transform["maxWidth"].asFloat();

        if (transform.isMember("center")) {
            const Json::Value& center = transform["center"];
            if (center.isMember("x"))
                settings.transformation.center.x = center["x"].asFloat();
            if (center.isMember("y"))
                settings.transformation.center.y = center["y"].asFloat();
        }

        if (transform.isMember("scale")) {
            const Json::Value& scale = transform["scale"];
            if (scale.isMember("horizontalScale"))
                settings.transformation.scale.horizontalScale = scale["horizontalScale"].asFloat();
            if (scale.isMember("verticalScale"))
                settings.transformation.scale.verticalScale = scale["verticalScale"].asFloat();
        }

        if (transform.isMember("rotation"))
            settings.transformation.rotation = transform["rotation"].asFloat();
    }

    return settings;
}

// Helper method to parse text style
void SubtitleManager::parseTextStyle(const Json::Value& styleJson, SubtitleTextStyle& style) const {
    // Basic text properties
    if (styleJson.isMember("fontFamily"))
        style.fontFamily = styleJson["fontFamily"].asString();
    if (styleJson.isMember("fontSize"))
        style.fontSize = styleJson["fontSize"].asFloat();
    if (styleJson.isMember("bold"))
        style.bold = styleJson["bold"].asInt();
    if (styleJson.isMember("italic"))
        style.italic = styleJson["italic"].asBool();
    if (styleJson.isMember("color"))
        style.color = styleJson["color"].asString();
    if (styleJson.isMember("opacity"))
        style.opacity = styleJson["opacity"].asFloat();
    if (styleJson.isMember("letterSpacing"))
        style.letterSpacing = styleJson["letterSpacing"].asFloat();
    if (styleJson.isMember("lineHeight"))
        style.lineHeight = styleJson["lineHeight"].asFloat();

    // Text transform
    if (styleJson.isMember("textTransform")) {
        std::string transform = styleJson["textTransform"].asString();
        if (transform == "UPPERCASE")
            style.textTransform = TextTransform::UPPERCASE;
        else if (transform == "LOWERCASE")
            style.textTransform = TextTransform::LOWERCASE;
        else if (transform == "CAPITALIZE")
            style.textTransform = TextTransform::CAPITALIZE;
        else
            style.textTransform = TextTransform::NONE;
    }

    // Translation
    if (styleJson.isMember("translateX"))
        style.translateX = styleJson["translateX"].asFloat();
    if (styleJson.isMember("translateY"))
        style.translateY = styleJson["translateY"].asFloat();

    // Stroke properties
    if (styleJson.isMember("strokeColor"))
        style.strokeColor = styleJson["strokeColor"].asString();
    if (styleJson.isMember("strokeOpacity"))
        style.strokeOpacity = styleJson["strokeOpacity"].asFloat();
    if (styleJson.isMember("strokeWidth"))
        style.strokeWidth = styleJson["strokeWidth"].asFloat();

    // Shadow properties
    if (styleJson.isMember("shadowColor") && !styleJson["shadowColor"].isNull())
        style.shadowColor = styleJson["shadowColor"].asString();
    if (styleJson.isMember("shadowOpacity") && !styleJson["shadowOpacity"].isNull())
        style.shadowOpacity = styleJson["shadowOpacity"].asFloat();
    if (styleJson.isMember("shadowBlur") && !styleJson["shadowBlur"].isNull())
        style.shadowBlur = styleJson["shadowBlur"].asFloat();
    if (styleJson.isMember("shadowDistance") && !styleJson["shadowDistance"].isNull())
        style.shadowDistance = styleJson["shadowDistance"].asFloat();
    if (styleJson.isMember("shadowAngle") && !styleJson["shadowAngle"].isNull())
        style.shadowAngle = styleJson["shadowAngle"].asFloat();

    // Background properties
    if (styleJson.isMember("backgroundColor"))
        style.backgroundColor = styleJson["backgroundColor"].asString();
    if (styleJson.isMember("backgroundOpacity"))
        style.backgroundOpacity = styleJson["backgroundOpacity"].asFloat();
    if (styleJson.isMember("backgroundRadius"))
        style.backgroundRadius = styleJson["backgroundRadius"].asFloat();
    if (styleJson.isMember("backgroundPaddingX"))
        style.backgroundPaddingX = styleJson["backgroundPaddingX"].asFloat();
    if (styleJson.isMember("backgroundPaddingY"))
        style.backgroundPaddingY = styleJson["backgroundPaddingY"].asFloat();
    if (styleJson.isMember("bubble"))
        style.bubble = styleJson["bubble"].asBool();
}

// Helper method to parse animation settings
void SubtitleManager::parseAnimationSettings(const Json::Value& animJson, AnimationSettings& settings) const {
    // In animation
    if (animJson.isMember("inInterpolation")) {
        const Json::Value& inInterp = animJson["inInterpolation"];
        if (inInterp.isMember("type")) {
            std::string type = inInterp["type"].asString();
            if (type == "LINEAR")
                settings.inInterpolation = LINEAR;
            else if (type == "BEZIER")
                settings.inInterpolation = BEZIER;
            else if (type == "CONSTANT")
                settings.inInterpolation = CONSTANT;
        }
    }

    if (animJson.isMember("inDuration"))
        settings.inDuration = animJson["inDuration"].asFloat();

    // Parse inStyles
    if (animJson.isMember("inStyles")) {
        const Json::Value& inStyles = animJson["inStyles"];
        for (const auto& key : inStyles.getMemberNames()) {
            if (inStyles[key].isString()) {
                // It's a color
                settings.inStylesColor[key] = inStyles[key].asString();
            } else if (inStyles[key].isNumeric()) {
                // It's a numeric value
                settings.inStyles[key] = inStyles[key].asFloat();
            }
        }
    }

    // Out animation
    if (animJson.isMember("outInterpolation")) {
        const Json::Value& outInterp = animJson["outInterpolation"];
        if (outInterp.isMember("type")) {
            std::string type = outInterp["type"].asString();
            if (type == "LINEAR")
                settings.outInterpolation = LINEAR;
            else if (type == "BEZIER")
                settings.outInterpolation = BEZIER;
            else if (type == "CONSTANT")
                settings.outInterpolation = CONSTANT;
        }
    }

    if (animJson.isMember("outDuration"))
        settings.outDuration = animJson["outDuration"].asFloat();

    if (animJson.isMember("outStyles")) {
        const Json::Value& outStyles = animJson["outStyles"];
        for (const auto& key : outStyles.getMemberNames()) {
            if (outStyles[key].isString()) {
                settings.outStylesColor[key] = outStyles[key].asString();
            } else if (outStyles[key].isNumeric()) {
                settings.outStyles[key] = outStyles[key].asFloat();
            }
        }
    }
}

// Helper method to parse global settings
void SubtitleManager::parseGlobalSettings(const Json::Value& settingsJson) const {
    pImpl->defaultSettings = parseSegmentSettings(settingsJson);
}

void SubtitleManager::loadFromJSON(const std::string& jsonPath) const {
    try {
        std::ifstream file(jsonPath);
        if (!file.is_open()) {
            throw std::runtime_error("Cannot open subtitle file: " + jsonPath);
        }

        Json::Value root;
        file >> root;
        file.close();

        clearSegments();

        // Load segments - Fix field names to match JSON structure
        if (root.isMember("segments") && root["segments"].isArray()) {
            const Json::Value& segments = root["segments"];

            for (const auto& seg : segments) {
                subtitle::SubtitleSegment segment;

                // Fix field names to match JSON payload
                segment.id = seg.get("id", "").asString();
                segment.startTimeMs = seg.get("startTime", 0).asFloat();  // Changed from start_ms
                segment.endTimeMs = seg.get("endTime", 0).asFloat();      // Changed from end_ms
                segment.visible = seg.get("visible", true).asBool();
                segment.attached = seg.get("attached", true).asBool();

                // Load wordDetails (not "words")
                if (seg.isMember("wordDetails") && seg["wordDetails"].isArray()) {
                    const Json::Value& wordDetails = seg["wordDetails"];

                    for (const auto& w : wordDetails) {
                        subtitle::WordDetail word;
                        word.word = w.get("word", "").asString();
                        word.startMs = w.get("startTime", 0).asFloat();  // Changed from start_ms
                        word.endMs = w.get("endTime", 0).asFloat();      // Changed from end_ms
                        word.confidence = w.get("confidence", 1.0f).asFloat();

                        segment.wordDetails.push_back(word);
                    }
                }

                // Handle segment-specific settings (when attached = false)
                if (!segment.attached && seg.isMember("settings")) {
                    segment.settings = parseSegmentSettings(seg["settings"]);
                }

                addSegment(segment);
            }
        }

        // Load global settings
        if (root.isMember("settings")) {
            parseGlobalSettings(root["settings"]);
        }
    } catch (const std::exception& e) {
        throw std::runtime_error("Error loading subtitle JSON: " + std::string(e.what()));
    }
}

void SubtitleManager::renderAtFrame(std::shared_ptr<QImage> frameImage, int64_t frameNumber) const {
    if (!pImpl->enabled || !frameImage || frameImage->isNull()) return;

    // Convert frame to time
    float timeMs = frameToMs(frameNumber, pImpl->fps);

    // Create SkBitmap that wraps the QImage data
    SkBitmap bitmap;
    SkImageInfo skiaInfo = SkImageInfo::MakeN32Premul(frameImage->width(), frameImage->height());

    if (!bitmap.installPixels(skiaInfo, frameImage->bits(), frameImage->bytesPerLine())) {
        return;
    }

    // Create canvas from bitmap
    SkCanvas canvas(bitmap);

    // Create renderer
    SkiaRenderer skiaRenderer(&canvas);
    SubtitleRenderer subtitleRenderer(&skiaRenderer, pImpl->fps);

    // Update max width based on current frame size
    pImpl->defaultSettings.transformation.maxWidth = frameImage->width() - 100; // Some margin

    // Find and render active segments
    for (const auto& segment : pImpl->segments) {
        if (segment.visible && timeMs >= segment.startTimeMs && timeMs <= segment.endTimeMs) {
            float segmentTimeMs = timeMs - segment.startTimeMs;
            subtitleRenderer.renderSegment(segment, pImpl->defaultSettings, segmentTimeMs);
        }
    }
}

void SubtitleManager::renderAtTime(SkCanvas* canvas, float timeInSeconds) const {
    if (!pImpl->enabled || !canvas) return;

    const float timeMs = timeInSeconds * 1000;

    // Create renderer
    SkiaRenderer skiaRenderer(canvas);
    SubtitleRenderer subtitleRenderer(&skiaRenderer, pImpl->fps);

    // Find and render active segments
    for (const auto& segment : pImpl->segments) {
        if (segment.visible && timeMs >= segment.startTimeMs && timeMs <= segment.endTimeMs) {
            const float segmentTimeMs = timeMs - segment.startTimeMs;
            subtitleRenderer.renderSegment(segment, pImpl->defaultSettings, segmentTimeMs);
        }
    }
}

bool SubtitleManager::hasActiveSubtitlesAtFrame(int64_t frameNumber) const {
    if (!pImpl->enabled) return false;

    const float timeMs = frameToMs(frameNumber, pImpl->fps);

    for (const auto& segment : pImpl->segments) {
        if (segment.visible && timeMs >= segment.startTimeMs && timeMs <= segment.endTimeMs) {
            return true;
        }
    }

    return false;
}

void SubtitleManager::createExampleSubtitles() const {
    clearSegments();

    // Example 1: Simple subtitle
    SubtitleSegment segment1;
    segment1.id = "example-1";
    segment1.visible = true;
    segment1.attached = true;
    segment1.startTimeMs = 0;
    segment1.endTimeMs = 3000;
    segment1.wordDetails = {
        {"HELLO", 0, 800, 1.0f},
        {"WORLD", 800, 1600, 1.0f},
        {"SUBTITLES", 1600, 2400, 1.0f}
    };
    addSegment(segment1);

    // Example 2: Another subtitle
    SubtitleSegment segment2;
    segment2.id = "example-2";
    segment2.visible = true;
    segment2.attached = true;
    segment2.startTimeMs = 3000;
    segment2.endTimeMs = 6000;
    segment2.wordDetails = {
        {"ANIMATED", 3000, 3800, 1.0f},
        {"TEXT", 3800, 4600, 1.0f},
        {"RENDERING", 4600, 5400, 1.0f}
    };
    addSegment(segment2);
}

} // namespace subtitle
} // namespace openshot