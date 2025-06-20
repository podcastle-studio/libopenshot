/**
 * @file
 * @brief Source file for ColorGrading class
 *
 * @ref License
 */

// Copyright (c) 2024 OpenShot Studios, LLC
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "ColorGrading.h"
#include "Exceptions.h"
#include <QImage>
#include <QRgb>
#include <algorithm>
#include <cmath>

using namespace openshot;

// Anonymous namespace for internal helper functions
namespace {

// Helper function to clamp values
int clamp(int value, int min = 0, int max = 255) {
    return std::max(min, std::min(max, value));
}

double clampDouble(double value, double min = 0.0, double max = 255.0) {
    return std::max(min, std::min(max, value));
}

// Apply brightness adjustment to RGB values
void applyBrightness(double& r, double& g, double& b, double brightness_value) {
    if (brightness_value == 0) return;

    if (brightness_value > 0) {
        // Positive brightness - exponential increase
        double factor = std::pow(2.0, brightness_value / 100.0);
        r *= factor;
        g *= factor;
        b *= factor;
    } else {
        // Negative brightness - preserve contrast better
        double factor = 1.0 + (brightness_value / 100.0) * 0.7;
        r *= factor;
        g *= factor;
        b *= factor;
    }
}

// Apply blacks adjustment to RGB values
void applyBlacks(double& r, double& g, double& b, double blacks_value) {
    if (blacks_value == 0) return;

    const double blacksAdjust = blacks_value / 100.0;
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

// Apply whites adjustment to RGB values
void applyWhites(double& r, double& g, double& b, double whites_value) {
    if (whites_value == 0) return;

    const double whitesAdjust = whites_value / 100.0;
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

// Apply shadows adjustment to RGB values
void applyShadows(double& r, double& g, double& b, double shadows_value) {
    if (shadows_value == 0) return;

    const double amount = shadows_value / 100.0;
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

// Apply highlights adjustment to RGB values
void applyHighlights(double& r, double& g, double& b, double highlights_value) {
    if (highlights_value == 0) return;

    const double amount = highlights_value / 100.0;
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

// Apply temperature and tint adjustments to RGB values
void applyTemperatureTint(double& r, double& g, double& b, double temperature_value, double tint_value) {
    if (temperature_value == 0 && tint_value == 0) return;

    const double temp = temperature_value / 100.0;
    const double tint = tint_value / 100.0;

    // Temperature: blue-yellow axis
    r *= (1.0 + temp * 0.1);
    b *= (1.0 - temp * 0.1);

    // Tint: green-magenta axis
    g *= (1.0 - tint * 0.1);
    r *= (1.0 + tint * 0.05);
    b *= (1.0 + tint * 0.05);
}

// Apply vibrance adjustment to RGB values
void applyVibrance(double& r, double& g, double& b, double vibrance_value) {
    if (vibrance_value == 0) return;

    const double amount = vibrance_value / 100.0;
    double gray = (r + g + b) / 3.0;
    double maxRGB = std::max({r, g, b});
    double minRGB = std::min({r, g, b});
    double saturation = maxRGB == 0 ? 0 : (maxRGB - minRGB) / maxRGB;

    double boostFactor = amount * (1.0 - saturation * saturation);

    r = gray + (r - gray) * (1.0 + boostFactor);
    g = gray + (g - gray) * (1.0 + boostFactor);
    b = gray + (b - gray) * (1.0 + boostFactor);
}

} // anonymous namespace

/// Blank constructor, useful when using Json to load the effect properties
ColorGrading::ColorGrading() :
    brightness(0.0), contrast(0.0), highlights(0.0), shadows(0.0),
    whites(0.0), blacks(0.0), temperature(0.0), tint(0.0), vibrance(0.0) {
    // Init effect properties
    init_effect_details();
}

// Constructor with all parameters
ColorGrading::ColorGrading(Keyframe brightness, Keyframe contrast, Keyframe highlights,
                         Keyframe shadows, Keyframe whites, Keyframe blacks,
                         Keyframe temperature, Keyframe tint, Keyframe vibrance) :
    brightness(brightness), contrast(contrast), highlights(highlights), shadows(shadows),
    whites(whites), blacks(blacks), temperature(temperature), tint(tint), vibrance(vibrance)
{
    // Init effect properties
    init_effect_details();
}

// Init effect settings
void ColorGrading::init_effect_details()
{
    /// Initialize the values of the EffectInfo struct.
    InitEffectInfo();

    /// Set the effect info
    info.class_name = "ColorGrading";
    info.name = "Color Grading";
    info.description = "Professional color grading with separate controls for highlights, shadows, temperature, and more.";
    info.has_audio = false;
    info.has_video = true;
}

// Tone curve implementation for contrast
double ColorGrading::toneCurve(double value, double contrast) const
{
    const double normalized = value / 255.0;
    double output;

    if (contrast > 0) {
        // S-curve for positive contrast
        const double amount = contrast / 100.0;
        const double midpoint = 0.5;

        if (normalized < midpoint) {
            output = midpoint * std::pow(normalized / midpoint, 1 + amount);
        } else {
            output = 1 - (1 - midpoint) * std::pow((1 - normalized) / (1 - midpoint), 1 + amount);
        }
    } else if (contrast < 0) {
        // Inverse S-curve for negative contrast
        const double amount = -contrast / 100.0;
        output = normalized + (0.5 - normalized) * amount * std::sin(M_PI * normalized);
    } else {
        output = normalized;
    }

    return output * 255.0;
}

// Create contrast lookup table
std::array<uint8_t, 256> ColorGrading::createContrastLUT(double contrast) const
{
    std::array<uint8_t, 256> lut;
    for (int i = 0; i < 256; ++i) {
        lut[i] = static_cast<uint8_t>(std::round(toneCurve(i, contrast)));
    }
    return lut;
}

// This method is required for all derived classes of EffectBase, and returns a
// modified openshot::Frame object
std::shared_ptr<openshot::Frame> ColorGrading::GetFrame(std::shared_ptr<openshot::Frame> frame, int64_t frame_number)
{
    // Get the frame's image
    std::shared_ptr<QImage> frame_image = frame->GetImage();

    // Convert to ARGB32 format if needed
    if (frame_image->format() != QImage::Format_ARGB32) {
        *frame_image = frame_image->convertToFormat(QImage::Format_ARGB32);
    }

    // Get keyframe values for this frame
    double brightness_value = brightness.GetValue(frame_number);
    double contrast_value = contrast.GetValue(frame_number);
    double highlights_value = highlights.GetValue(frame_number);
    double shadows_value = shadows.GetValue(frame_number);
    double whites_value = whites.GetValue(frame_number);
    double blacks_value = blacks.GetValue(frame_number);
    double temperature_value = temperature.GetValue(frame_number);
    double tint_value = tint.GetValue(frame_number);
    double vibrance_value = vibrance.GetValue(frame_number);

    // Skip processing if all values are at default
    if (brightness_value == 0 && contrast_value == 0 && highlights_value == 0 &&
        shadows_value == 0 && whites_value == 0 && blacks_value == 0 &&
        temperature_value == 0 && tint_value == 0 && vibrance_value == 0) {
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

    // Process each pixel
    for (int y = 0; y < height; ++y) {
        QRgb* line = reinterpret_cast<QRgb*>(frame_image->scanLine(y));

        for (int x = 0; x < width; ++x) {
            QRgb pixel = line[x];
            double r = qRed(pixel);
            double g = qGreen(pixel);
            double b = qBlue(pixel);
            int a = qAlpha(pixel);

            // Apply adjustments in the correct order for best results

            // 1. Brightness adjustment
            applyBrightness(r, g, b, brightness_value);

            // 2. Apply contrast using LUT
            if (useContrastLUT) {
                r = contrastLUT[clamp(static_cast<int>(r))];
                g = contrastLUT[clamp(static_cast<int>(g))];
                b = contrastLUT[clamp(static_cast<int>(b))];
            }

            // 3. Blacks adjustment (affects the black point)
            applyBlacks(r, g, b, blacks_value);

            // 4. Whites adjustment (affects the white point)
            applyWhites(r, g, b, whites_value);

            // 5. Shadows adjustment (targets dark areas)
            applyShadows(r, g, b, shadows_value);

            // 6. Highlights adjustment (targets bright areas)
            applyHighlights(r, g, b, highlights_value);

            // 7. Temperature & Tint (color balance)
            applyTemperatureTint(r, g, b, temperature_value, tint_value);

            // 8. Vibrance (smart saturation)
            applyVibrance(r, g, b, vibrance_value);

            // Final clamping and assignment
            line[x] = qRgba(clamp(r), clamp(g), clamp(b), a);
        }
    }

    // return the modified frame
    return frame;
}

// Generate JSON string of this object
std::string ColorGrading::Json() const {
    // Return formatted string
    return JsonValue().toStyledString();
}

// Generate Json::Value for this object
Json::Value ColorGrading::JsonValue() const {
    // Create root json object
    Json::Value root = EffectBase::JsonValue(); // get parent properties
    root["type"] = info.class_name;
    root["brightness"] = brightness.JsonValue();
    root["contrast"] = contrast.JsonValue();
    root["highlights"] = highlights.JsonValue();
    root["shadows"] = shadows.JsonValue();
    root["whites"] = whites.JsonValue();
    root["blacks"] = blacks.JsonValue();
    root["temperature"] = temperature.JsonValue();
    root["tint"] = tint.JsonValue();
    root["vibrance"] = vibrance.JsonValue();

    // return JsonValue
    return root;
}

// Load JSON string into this object
void ColorGrading::SetJson(const std::string value) {
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
void ColorGrading::SetJsonValue(const Json::Value root) {
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
    if (!root["temperature"].isNull())
        temperature.SetJsonValue(root["temperature"]);
    if (!root["tint"].isNull())
        tint.SetJsonValue(root["tint"]);
    if (!root["vibrance"].isNull())
        vibrance.SetJsonValue(root["vibrance"]);
}

// Get all properties for a specific frame
std::string ColorGrading::PropertiesJSON(int64_t requested_frame) const {
    // Generate JSON properties list
    Json::Value root = BasePropertiesJSON(requested_frame);

    // Light adjustment keyframes
    root["brightness"] = add_property_json("Brightness", brightness.GetValue(requested_frame), "float", "", &brightness, -100.0, 100.0, false, requested_frame);
    root["contrast"] = add_property_json("Contrast", contrast.GetValue(requested_frame), "float", "", &contrast, -100.0, 100.0, false, requested_frame);
    root["highlights"] = add_property_json("Highlights", highlights.GetValue(requested_frame), "float", "", &highlights, -100.0, 100.0, false, requested_frame);
    root["shadows"] = add_property_json("Shadows", shadows.GetValue(requested_frame), "float", "", &shadows, -100.0, 100.0, false, requested_frame);
    root["whites"] = add_property_json("Whites", whites.GetValue(requested_frame), "float", "", &whites, -100.0, 100.0, false, requested_frame);
    root["blacks"] = add_property_json("Blacks", blacks.GetValue(requested_frame), "float", "", &blacks, -100.0, 100.0, false, requested_frame);

    // Color adjustment keyframes
    root["temperature"] = add_property_json("Temperature", temperature.GetValue(requested_frame), "float", "", &temperature, -100.0, 100.0, false, requested_frame);
    root["tint"] = add_property_json("Tint", tint.GetValue(requested_frame), "float", "", &tint, -100.0, 100.0, false, requested_frame);
    root["vibrance"] = add_property_json("Vibrance", vibrance.GetValue(requested_frame), "float", "", &vibrance, -100.0, 100.0, false, requested_frame);

    // Return formatted string
    return root.toStyledString();
}