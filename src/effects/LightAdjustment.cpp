/**
 * @file
 * @brief Source file for LightAdjustment class
 *
 * @ref License
 */

// Copyright (c) 2024 OpenShot Studios, LLC
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "LightAdjustment.h"
#include "Exceptions.h"
#include <QImage>
#include <QRgb>
#include <algorithm>
#include <cmath>

using namespace openshot;

// Anonymous namespace for internal helper functions
namespace {

// Apply brightness adjustment to RGB values: brightness_value: -1.0 to 1.0
void applyBrightness(double& r, double& g, double& b, const double brightness_value) {
    if (brightness_value == 0) return;

    if (brightness_value > 0) {
        // Positive brightness - exponential increase
        const double factor = std::pow(2.0, brightness_value);
        r *= factor;
        g *= factor;
        b *= factor;
    } else {
        // Negative brightness - preserve contrast better
        const double factor = 1.0 + brightness_value * 0.7;
        r *= factor;
        g *= factor;
        b *= factor;
    }
}

// Apply blacks adjustment to RGB values: blacks_value: -1.0 to 1.0
void applyBlacks(double& r, double& g, double& b, const double blacks_value) {
    if (blacks_value == 0) return;

    const double blacksAdjust = blacks_value;
    const double blackPoint = blacksAdjust * 0.1;

    if (blacksAdjust > 0) {
        r = blackPoint * 255 + r * (1 - blackPoint);
        g = blackPoint * 255 + g * (1 - blackPoint);
        b = blackPoint * 255 + b * (1 - blackPoint);
    } else {
        const double factor = 1.0 / (1.0 + blackPoint);
        r = std::max(0.0, (r - std::abs(blackPoint) * 255) * factor);
        g = std::max(0.0, (g - std::abs(blackPoint) * 255) * factor);
        b = std::max(0.0, (b - std::abs(blackPoint) * 255) * factor);
    }
}

// Apply whites adjustment to RGB values: whites_value: -1.0 to 1.0
void applyWhites(double& r, double& g, double& b, const double whites_value) {
    if (whites_value == 0) return;

    const double whitesAdjust = whites_value;
    const double whitePoint = 1.0 - std::abs(whitesAdjust) * 0.1;

    if (whitesAdjust > 0) {
        const double factor = 1.0 / whitePoint;
        r = std::min(255.0, r * factor);
        g = std::min(255.0, g * factor);
        b = std::min(255.0, b * factor);
    } else {
        r *= whitePoint;
        g *= whitePoint;
        b *= whitePoint;
    }
}

// Apply shadows adjustment to RGB values: shadows_value: -1.0 to 1.0
void applyShadows(double& r, double& g, double& b, const double shadows_value) {
    if (shadows_value == 0) return;

    const double amount = shadows_value;
    const double lum = (r * 0.299 + g * 0.587 + b * 0.114) / 255.0;

    double mask = 0;
    if (lum < 0.15) {
        mask = 1.0;
    } else if (lum < 0.35) {
        double t = (lum - 0.15) / 0.2;
        mask = 1.0 - t * t;
    }

    if (mask > 0) {
        if (amount > 0) {
            const double maxLift = 0.5;
            double lift = amount * mask * maxLift;
            double factor = 1.0 + lift * (1.0 - lum * 2.0);
            r *= factor;
            g *= factor;
            b *= factor;
        } else {
            double crush = std::abs(amount) * mask;
            double factor = 1.0 - crush * 0.9;
            r *= factor;
            g *= factor;
            b *= factor;
        }
    }
}

// Apply highlights adjustment to RGB values: highlights_value: -1.0 to 1.0
void applyHighlights(double& r, double& g, double& b, const double highlights_value) {
    if (highlights_value == 0) return;

    const double amount = highlights_value;
    const double lum = (r * 0.299 + g * 0.587 + b * 0.114) / 255.0;

    if (lum > 0.6) {
        double mask = (lum - 0.6) / 0.4;

        if (amount < 0) {
            double reduction = std::abs(amount) * mask * 0.5;
            r *= (1 - reduction);
            g *= (1 - reduction);
            b *= (1 - reduction);
        } else {
            double boost = amount * mask * 0.3;
            r += (255 - r) * boost;
            g += (255 - g) * boost;
            b += (255 - b) * boost;
        }
    }
}

} // anonymous namespace

/// Blank constructor, useful when using Json to load the effect properties
LightAdjustment::LightAdjustment() :
    brightness(0.0), contrast(0.0), highlights(0.0), shadows(0.0),
    whites(0.0), blacks(0.0) {
    // Init effect properties
    init_effect_details();
}

// Constructor with all parameters
LightAdjustment::LightAdjustment(Keyframe brightness, Keyframe contrast, Keyframe highlights,
                                Keyframe shadows, Keyframe whites, Keyframe blacks) :
    brightness(brightness), contrast(contrast), highlights(highlights), shadows(shadows),
    whites(whites), blacks(blacks)
{
    // Init effect properties
    init_effect_details();
}

// Init effect settings
void LightAdjustment::init_effect_details()
{
    /// Initialize the values of the EffectInfo struct.
    InitEffectInfo();

    /// Set the effect info
    info.class_name = "LightAdjustment";
    info.name = "Light Adjustment";
    info.description = "Professional lighting adjustment with separate controls for brightness, contrast, highlights, shadows, whites, and blacks.";
    info.has_audio = false;
    info.has_video = true;
}

// Tone curve implementation for contrast
double LightAdjustment::toneCurve(double value, double contrast) const
{
    const double normalized = value / 255.0;
    double output;

    if (contrast > 0) {
        // S-curve for positive contrast
        const double amount = contrast;
        const double midpoint = 0.5;

        if (normalized < midpoint) {
            output = midpoint * std::pow(normalized / midpoint, 1 + amount);
        } else {
            output = 1 - (1 - midpoint) * std::pow((1 - normalized) / (1 - midpoint), 1 + amount);
        }
    } else if (contrast < 0) {
        // Inverse S-curve for negative contrast
        const double amount = -contrast;
        output = normalized + (0.5 - normalized) * amount * std::sin(M_PI * normalized);
    } else {
        output = normalized;
    }

    return output * 255.0;
}

// Create contrast lookup table
std::array<uint8_t, 256> LightAdjustment::createContrastLUT(double contrast) const
{
    std::array<uint8_t, 256> lut;
    for (int i = 0; i < 256; ++i) {
        lut[i] = static_cast<uint8_t>(std::round(toneCurve(i, contrast)));
    }
    return lut;
}

// This method is required for all derived classes of EffectBase, and returns a
// modified openshot::Frame object
std::shared_ptr<openshot::Frame> LightAdjustment::GetFrame(std::shared_ptr<openshot::Frame> frame, int64_t frame_number)
{
    // Get the frame's image
    std::shared_ptr<QImage> frame_image = frame->GetImage();

    // Assume incoming frame is already RGBA (e.g. QImage::Format_RGBA8888)
    // and do not convert formats.

    // Get keyframe values for this frame
    double brightness_value = brightness.GetValue(frame_number);
    double contrast_value = contrast.GetValue(frame_number);
    double highlights_value = highlights.GetValue(frame_number);
    double shadows_value = shadows.GetValue(frame_number);
    double whites_value = whites.GetValue(frame_number);
    double blacks_value = blacks.GetValue(frame_number);

    // Skip processing if all values are at default
    if (brightness_value == 0 && contrast_value == 0 && highlights_value == 0 &&
        shadows_value == 0 && whites_value == 0 && blacks_value == 0) {
        return frame;
    }

    const int width = frame_image->width();
    const int height = frame_image->height();

    // Create contrast LUT if needed
    std::array<uint8_t, 256> contrastLUT;
    bool useContrastLUT = contrast_value != 0;
    if (useContrastLUT) {
        contrastLUT = createContrastLUT(contrast_value);
    }

    // *** Minimal perf tweak: precompute brightness factor once ***
    const bool doBrightness = (brightness_value != 0.0);
    const bool brightPos    = (brightness_value > 0.0);
    const double brightFactorPos = brightPos && doBrightness ? std::pow(2.0, brightness_value) : 1.0;
    const double brightFactorNeg = (!brightPos && doBrightness) ? (1.0 + brightness_value * 0.7) : 1.0;

    // Detach QImage once before multi-thread writes
    frame_image->bits();

    const int bpl = frame_image->bytesPerLine();
    uchar* base = frame_image->bits();

    #pragma omp parallel for schedule(static)
    for (int y = 0; y < height; ++y) {
        uchar* line = base + y * bpl;

        #if defined(__GNUC__) || defined(__clang__)
        #pragma GCC ivdep
        #endif
        for (int x = 0; x < width; ++x) {
            uchar* px = line + x * 4;

            double r = px[0]; // R
            double g = px[1]; // G
            double b = px[2]; // B
            int a = px[3];    // A

            // 1) Brightness ? identical behavior, but no per-pixel pow()
            if (doBrightness) {
                const double f = brightPos ? brightFactorPos : brightFactorNeg;
                r *= f; g *= f; b *= f;
            }

            // 2. Apply contrast using LUT
            if (useContrastLUT) {
                r = contrastLUT[clamp(static_cast<int>(r))];
                g = contrastLUT[clamp(static_cast<int>(g))];
                b = contrastLUT[clamp(static_cast<int>(b))];
            }

            // 3. Blacks adjustment (affects the black point)
            if (blacks_value != 0)
                applyBlacks(r, g, b, blacks_value);

            // 4. Whites adjustment (affects the white point)
            if (whites_value != 0)
                applyWhites(r, g, b, whites_value);

            // 5. Shadows adjustment (targets dark areas)
            if (shadows_value != 0)
                applyShadows(r, g, b, shadows_value);

            // 6. Highlights adjustment (targets bright areas)
            if (highlights_value != 0)
                applyHighlights(r, g, b, highlights_value);

            // Final clamping and assignment
            px[0] = clamp(r);
            px[1] = clamp(g);
            px[2] = clamp(b);
            px[3] = a;
        }
    }

    // return the modified frame
    return frame;
}

// Generate JSON string of this object
std::string LightAdjustment::Json() const {
    // Return formatted string
    return JsonValue().toStyledString();
}

// Generate Json::Value for this object
Json::Value LightAdjustment::JsonValue() const {
    // Create root json object
    Json::Value root = EffectBase::JsonValue(); // get parent properties
    root["type"] = info.class_name;
    root["brightness"] = brightness.JsonValue();
    root["contrast"] = contrast.JsonValue();
    root["highlights"] = highlights.JsonValue();
    root["shadows"] = shadows.JsonValue();
    root["whites"] = whites.JsonValue();
    root["blacks"] = blacks.JsonValue();

    // return JsonValue
    return root;
}

// Load JSON string into this object
void LightAdjustment::SetJson(const std::string value) {
    // Parse JSON string into JSON objects
    try
    {
        const Json::Value root = openshot::stringToJson(value);
        // Set all values that match
        SetJsonValue(root);
    }
    catch (const std::exception& e)
    {
        // Error parsing JSON (or missing keys)
        throw InvalidJSON("JSON is invalid (missing keys or invalid data types)");
    }
}

// Load Json::Value into this object
void LightAdjustment::SetJsonValue(const Json::Value root) {
    // Set parent data
    EffectBase::SetJsonValue(root);

    // Set data from Json (if key is found)
    if (!root["brightness"].isNull())
        brightness.SetJsonValue(root["brightness"]);
    if (!root["contrast"].isNull())
        contrast.SetJsonValue(root["contrast"]);
    if (!root["highlights"].isNull())
        highlights.SetJsonValue(root["highlights"]);
    if (!root["shadows"].isNull())
        shadows.SetJsonValue(root["shadows"]);
    if (!root["whites"].isNull())
        whites.SetJsonValue(root["whites"]);
    if (!root["blacks"].isNull())
        blacks.SetJsonValue(root["blacks"]);
}

// Get all properties for a specific frame
std::string LightAdjustment::PropertiesJSON(int64_t requested_frame) const {
    // Generate JSON properties list
    Json::Value root = BasePropertiesJSON(requested_frame);

    // Light adjustment keyframes
    root["brightness"] = add_property_json("Brightness", brightness.GetValue(requested_frame), "float", "", &brightness, -100.0, 100.0, false, requested_frame);
    root["contrast"] = add_property_json("Contrast", contrast.GetValue(requested_frame), "float", "", &contrast, -100.0, 100.0, false, requested_frame);
    root["highlights"] = add_property_json("Highlights", highlights.GetValue(requested_frame), "float", "", &highlights, -100.0, 100.0, false, requested_frame);
    root["shadows"] = add_property_json("Shadows", shadows.GetValue(requested_frame), "float", "", &shadows, -100.0, 100.0, false, requested_frame);
    root["whites"] = add_property_json("Whites", whites.GetValue(requested_frame), "float", "", &whites, -100.0, 100.0, false, requested_frame);
    root["blacks"] = add_property_json("Blacks", blacks.GetValue(requested_frame), "float", "", &blacks, -100.0, 100.0, false, requested_frame);

    // Return formatted string
    return root.toStyledString();
}
