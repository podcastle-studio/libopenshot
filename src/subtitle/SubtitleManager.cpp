/**
 * @file
 * @brief Implementation of SubtitleManager class
 * @author OpenShot Studios, LLC
 *
 * @ref License
 */

// Copyright (c) 2008-2024 OpenShot Studios, LLC
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "SubtitleManager.h"
#include "SubtitleTypes.h"
#include "SubtitleRenderer.h"
#include "SkiaRenderer.h"
#include "DefaultPreset.h"
#include "Helpers.h"
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

void SubtitleManager::loadFromJSON(const std::string& jsonPath) const {
    try {
        // Read the JSON file
        std::ifstream file(jsonPath);
        if (!file.is_open()) {
            throw std::runtime_error("Cannot open subtitle file: " + jsonPath);
        }

        // Parse JSON
        Json::Value root;
        file >> root;
        file.close();

        // Clear existing segments
        clearSegments();

        // Load segments
        if (root.isMember("segments") && root["segments"].isArray()) {
            const Json::Value& segments = root["segments"];

            for (const auto& seg : segments) {
                subtitle::SubtitleSegment segment;

                // Basic properties
                segment.id = seg.get("id", "").asString();
                segment.startTimeMs = seg.get("start_ms", 0).asFloat();
                segment.endTimeMs = seg.get("end_ms", 0).asFloat();
                segment.visible = seg.get("visible", true).asBool();
                segment.attached = seg.get("attached", true).asBool();

                // Load words
                if (seg.isMember("words") && seg["words"].isArray()) {
                    const Json::Value& words = seg["words"];

                    for (const auto& w : words) {
                        subtitle::WordDetail word;
                        word.word = w.get("text", "").asString();
                        word.startMs = w.get("start_ms", 0).asFloat();
                        word.endMs = w.get("end_ms", 0).asFloat();
                        word.confidence = w.get("confidence", 1.0f).asFloat();

                        segment.wordDetails.push_back(word);
                    }
                }

                // Add segment
                addSegment(segment);
            }
        }

        // Load default style if present
        if (root.isMember("default_style")) {
            const Json::Value& style = root["default_style"];
            auto& defaultStyle = getDefaultStyle();

            // Font properties
            if (style.isMember("font_family"))
                defaultStyle.fontFamily = style["font_family"].asString();
            if (style.isMember("font_size"))
                defaultStyle.fontSize = style["font_size"].asFloat();
            if (style.isMember("bold"))
                defaultStyle.bold = style["bold"].asInt();
            if (style.isMember("italic"))
                defaultStyle.italic = style["italic"].asBool();

            // Colors
            if (style.isMember("color"))
                defaultStyle.color = style["color"].asString();
            if (style.isMember("opacity"))
                defaultStyle.opacity = style["opacity"].asFloat();

            // Stroke
            if (style.isMember("stroke_width"))
                defaultStyle.strokeWidth = style["stroke_width"].asFloat();
            if (style.isMember("stroke_color"))
                defaultStyle.strokeColor = style["stroke_color"].asString();
            if (style.isMember("stroke_opacity"))
                defaultStyle.strokeOpacity = style["stroke_opacity"].asFloat();

            // Shadow
            if (style.isMember("shadow_color"))
                defaultStyle.shadowColor = style["shadow_color"].asString();
            if (style.isMember("shadow_opacity"))
                defaultStyle.shadowOpacity = style["shadow_opacity"].asFloat();
            if (style.isMember("shadow_blur"))
                defaultStyle.shadowBlur = style["shadow_blur"].asFloat();
            if (style.isMember("shadow_distance"))
                defaultStyle.shadowDistance = style["shadow_distance"].asFloat();
            if (style.isMember("shadow_angle"))
                defaultStyle.shadowAngle = style["shadow_angle"].asFloat();

            // Background
            if (style.isMember("background_color"))
                defaultStyle.backgroundColor = style["background_color"].asString();
            if (style.isMember("background_opacity"))
                defaultStyle.backgroundOpacity = style["background_opacity"].asFloat();
            if (style.isMember("background_padding_x"))
                defaultStyle.backgroundPaddingX = style["background_padding_x"].asFloat();
            if (style.isMember("background_padding_y"))
                defaultStyle.backgroundPaddingY = style["background_padding_y"].asFloat();
            if (style.isMember("background_radius"))
                defaultStyle.backgroundRadius = style["background_radius"].asFloat();
            if (style.isMember("bubble"))
                defaultStyle.bubble = style["bubble"].asBool();

            // Text transform
            if (style.isMember("text_transform")) {
                std::string transform = style["text_transform"].asString();
                if (transform == "UPPERCASE")
                    defaultStyle.textTransform = subtitle::TextTransform::UPPERCASE;
                else if (transform == "LOWERCASE")
                    defaultStyle.textTransform = subtitle::TextTransform::LOWERCASE;
                else if (transform == "CAPITALIZE")
                    defaultStyle.textTransform = subtitle::TextTransform::CAPITALIZE;
                else
                    defaultStyle.textTransform = subtitle::TextTransform::NONE;
            }

            // Other properties
            if (style.isMember("letter_spacing"))
                defaultStyle.letterSpacing = style["letter_spacing"].asFloat();
            if (style.isMember("line_height"))
                defaultStyle.lineHeight = style["line_height"].asFloat();
        }

        // Load animation settings if present
        if (root.isMember("animation_settings")) {
            const Json::Value& anim = root["animation_settings"];
            auto& animSettings = pImpl->defaultSettings.animationSettings;

            // In animation
            if (anim.isMember("in_interpolation")) {
                std::string interp = anim["in_interpolation"].asString();
                if (interp == "LINEAR")
                    animSettings.inInterpolation = LINEAR;
                else if (interp == "BEZIER")
                    animSettings.inInterpolation = BEZIER;
                else if (interp == "CONSTANT")
                    animSettings.inInterpolation = CONSTANT;
            }

            if (anim.isMember("in_speed"))
                animSettings.inSpeed = anim["in_speed"].asFloat();

            // In styles
            if (anim.isMember("in_styles")) {
                const Json::Value& inStyles = anim["in_styles"];
                for (const auto& key : inStyles.getMemberNames()) {
                    animSettings.inStyles[key] = inStyles[key].asFloat();
                }
            }

            // In styles color
            if (anim.isMember("in_styles_color")) {
                const Json::Value& inStylesColor = anim["in_styles_color"];
                for (const auto& key : inStylesColor.getMemberNames()) {
                    animSettings.inStylesColor[key] = inStylesColor[key].asString();
                }
            }

            // Out animation (similar structure)
            if (anim.isMember("out_interpolation")) {
                std::string interp = anim["out_interpolation"].asString();
                if (interp == "LINEAR")
                    animSettings.outInterpolation = LINEAR;
                else if (interp == "BEZIER")
                    animSettings.outInterpolation = BEZIER;
                else if (interp == "CONSTANT")
                    animSettings.outInterpolation = CONSTANT;
            }

            if (anim.isMember("out_speed"))
                animSettings.outSpeed = anim["out_speed"].asFloat();
        }

        std::cout << "Loaded " << pImpl->segments.size() << " subtitle segments from " << jsonPath << std::endl;

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