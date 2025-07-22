#include "SubtitleManager.h"
#include "SubtitleTypes.h"
#include "SubtitleRenderer.h"
#include "SkiaRenderer.h"
#include "Helpers.h"

#include <skia/include/core/SkBitmap.h>
#include <skia/include/core/SkCanvas.h>

#include <QImage>
#include <fstream>

namespace openshot {
namespace subtitle {

SubtitleManager::SubtitleManager(float fps) : fps(fps) {}

SubtitleManager::~SubtitleManager() = default;

void SubtitleManager::setEnabled(const bool enable) {
    enabled = enable;
}

bool SubtitleManager::isEnabled() const {
    return enabled;
}

void SubtitleManager::setDefaultStyle(const SubtitleTextStyle& style) {
    defaultSettings.defaultStyle = style;
}

SubtitleTextStyle& SubtitleManager::getDefaultStyle() {
    return defaultSettings.defaultStyle;
}

void SubtitleManager::setMaxWidth(const float width) {
    defaultSettings.transformation.maxWidth = width;
}

void SubtitleManager::addSegment(const SubtitleSegment& segment) {
    segments.push_back(segment);
}

void SubtitleManager::clearSegments() {
    segments.clear();
}

std::vector<SubtitleSegment>& SubtitleManager::getSegments() {
    return segments;
}

SegmentSettings SubtitleManager::parseSegmentSettings(const Json::Value& settingsJson) const {
    SegmentSettings settings = defaultSettings; // Start with defaults

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
            const std::string align = containerStyle["textAlign"].asString();
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
void SubtitleManager::parseTextStyle(const Json::Value& styleJson, SubtitleTextStyle& style) {
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
void SubtitleManager::parseAnimationSettings(const Json::Value& animJson, AnimationSettings& settings) {
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
void SubtitleManager::parseGlobalSettings(const Json::Value& settingsJson) {
    defaultSettings = parseSegmentSettings(settingsJson);
}

void SubtitleManager::loadFromJSON(const std::string& jsonPath) {
    try {
        std::ifstream file(jsonPath);
        if (!file.is_open()) {
            throw std::runtime_error("Cannot open subtitle file: " + jsonPath);
        }

        Json::Value root;
        file >> root;
        file.close();

        parseJSONRoot(root);
    } catch (const std::exception& e) {
        throw std::runtime_error("Error loading subtitle JSON: " + std::string(e.what()));
    }
}

void SubtitleManager::loadFromJSONString(const std::string& jsonString) {
    try {
        Json::Value root;
        Json::Reader reader;

        if (!reader.parse(jsonString, root)) {
            throw std::runtime_error("Failed to parse JSON string: " + reader.getFormattedErrorMessages());
        }

        parseJSONRoot(root);
    } catch (const std::exception& e) {
        throw std::runtime_error("Error loading subtitle JSON: " + std::string(e.what()));
    }
}

void SubtitleManager::parseJSONRoot(const Json::Value& root) {
    clearSegments();

    // Load segments - Fix field names to match JSON structure
    if (root.isMember("segments") && root["segments"].isArray()) {
        const Json::Value& segments = root["segments"];

        for (const auto& seg : segments) {
            SubtitleSegment segment;

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
                    WordDetail word;
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
}

void SubtitleManager::renderAtFrame(std::shared_ptr<QImage> frameImage, int64_t frameNumber) const {
    if (!enabled || !frameImage || frameImage->isNull()) return;

    // Convert frame to time
    float timeMs = frameToMs(frameNumber, fps);

    // Create SkBitmap that wraps the QImage data
    SkBitmap bitmap;
    SkImageInfo skiaInfo = SkImageInfo::MakeN32Premul(frameImage->width(), frameImage->height());

    if (!bitmap.installPixels(skiaInfo, frameImage->bits(), frameImage->bytesPerLine())) {
        return;
    }

    // Create canvas from bitmap
    SkCanvas canvas(bitmap);
    float canvasWidth = frameImage->width();
    float canvasHeight = frameImage->height();

    // Create renderer
    SkiaRenderer skiaRenderer(&canvas);
    SubtitleRenderer subtitleRenderer(&skiaRenderer, fps);

    // Update max width based on current frame size
    const_cast<SubtitleManager*>(this)->defaultSettings.transformation.maxWidth = frameImage->width() - 100; // Some margin

    // Find and render active segments
    for (const auto& segment : segments) {
        if (segment.visible && timeMs >= segment.startTimeMs && timeMs <= segment.endTimeMs) {
            float segmentTimeMs = timeMs - segment.startTimeMs;
            subtitleRenderer.renderSegment(segment, defaultSettings, segmentTimeMs, canvasWidth, canvasHeight);
        }
    }
}

bool SubtitleManager::hasActiveSubtitlesAtFrame(int64_t frameNumber) const {
    if (!enabled) return false;

    const float timeMs = frameToMs(frameNumber, fps);

    for (const auto& segment : segments) {
        if (segment.visible && timeMs >= segment.startTimeMs && timeMs <= segment.endTimeMs) {
            return true;
        }
    }

    return false;
}

} // namespace subtitle
} // namespace openshot