/**
 * @file
 * @brief Source file for ColorAdjustment class
 *
 * @ref License
 */

// Copyright (c) 2024 OpenShot Studios, LLC
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "ColorAdjustment.h"
#include "Exceptions.h"
#include <QImage>
#include <QRgb>
#include <algorithm>
#include <cmath>

using namespace openshot;

// Anonymous namespace for internal helper functions
namespace {

// Apply temperature and tint adjustments to RGB values
// temperature_value: -1.0 to 1.0 , tint_value: -1.0 to 1.0
void applyTemperatureTint(double& r, double& g, double& b, const double temperature_value, const double tint_value) {
    if (temperature_value == 0 && tint_value == 0) return;

    const double temp = temperature_value;
    const double tint = tint_value;

    // Temperature: blue-yellow axis
    r *= (1.0 + temp * 0.1);
    b *= (1.0 - temp * 0.1);

    // Tint: green-magenta axis
    g *= (1.0 - tint * 0.1);
    r *= (1.0 + tint * 0.05);
    b *= (1.0 + tint * 0.05);
}

// Apply vibrance adjustment to RGB values: vibrance_value: -1.0 to 1.0
void applyVibrance(double& r, double& g, double& b, const double vibrance_value) {
    if (vibrance_value == 0) return;

    const double amount = vibrance_value;
    const double gray = (r + g + b) / 3.0;

    // Avoid initializer_list overhead
    double maxRGB = r > g ? (r > b ? r : b) : (g > b ? g : b);
    double minRGB = r < g ? (r < b ? r : b) : (g < b ? g : b);

    const double saturation = maxRGB == 0 ? 0 : (maxRGB - minRGB) / maxRGB;
    const double boostFactor = amount * (1.0 - saturation * saturation);

    r = gray + (r - gray) * (1.0 + boostFactor);
    g = gray + (g - gray) * (1.0 + boostFactor);
    b = gray + (b - gray) * (1.0 + boostFactor);
}

// Apply saturation adjustment to RGB values: saturation_value: -1.0 to 1.0
void applySaturation(double& r, double& g, double& b, const double saturation_value) {
    if (saturation_value == 0) return;

    const double amount = saturation_value;
    const double gray = (r * 0.299 + g * 0.587 + b * 0.114);

    r = gray + (r - gray) * (1.0 + amount);
    g = gray + (g - gray) * (1.0 + amount);
    b = gray + (b - gray) * (1.0 + amount);
}

} // anonymous namespace

/// Blank constructor, useful when using Json to load the effect properties
ColorAdjustment::ColorAdjustment() :
    temperature(0.0), tint(0.0), vibrance(0.0), saturation(0.0) {
    // Init effect properties
    init_effect_details();
}

// Constructor with all parameters
ColorAdjustment::ColorAdjustment(Keyframe temperature, Keyframe tint,
                                Keyframe vibrance, Keyframe saturation) :
    temperature(temperature), tint(tint), vibrance(vibrance), saturation(saturation)
{
    // Init effect properties
    init_effect_details();
}

// Init effect settings
void ColorAdjustment::init_effect_details()
{
    /// Initialize the values of the EffectInfo struct.
    InitEffectInfo();

    /// Set the effect info
    info.class_name = "ColorAdjustment";
    info.name = "Color Adjustment";
    info.description = "Color adjustment with controls for temperature, tint, vibrance, and saturation.";
    info.has_audio = false;
    info.has_video = true;
}

// This method is required for all derived classes of EffectBase, and returns a
// modified openshot::Frame object
std::shared_ptr<openshot::Frame> ColorAdjustment::GetFrame(std::shared_ptr<openshot::Frame> frame, int64_t frame_number)
{
    // Get the frame's image
    std::shared_ptr<QImage> frame_image = frame->GetImage();

    // Convert to ARGB32 format if needed
    if (frame_image->format() != QImage::Format_ARGB32) {
        *frame_image = frame_image->convertToFormat(QImage::Format_ARGB32);
    }

    // Get keyframe values for this frame
    double temperature_value = temperature.GetValue(frame_number);
    double tint_value = tint.GetValue(frame_number);
    double vibrance_value = vibrance.GetValue(frame_number);
    double saturation_value = saturation.GetValue(frame_number);

    // Skip processing if all values are at default
    if (temperature_value == 0 && tint_value == 0 &&
        vibrance_value == 0 && saturation_value == 0) {
        return frame;
    }

    const int width = frame_image->width();
    const int height = frame_image->height();

    // Precompute frame-constant flags (skip repeated comparisons)
    const bool doTT  = (temperature_value != 0 || tint_value != 0);
    const bool doSat = (saturation_value  != 0);
    const bool doVib = (vibrance_value    != 0);

    // Detach once and use base pointer + stride
    uchar* base   = frame_image->bits();
    const int bpl = frame_image->bytesPerLine();

    // Parallelize rows only; inner loop remains identical math/order
    #pragma omp parallel for schedule(static)
    for (int y = 0; y < height; ++y) {
        QRgb* line = reinterpret_cast<QRgb*>(base + y * bpl);

        #if defined(__GNUC__) || defined(__clang__)
        #pragma GCC ivdep
        #endif
        for (int x = 0; x < width; ++x) {
            QRgb pixel = line[x];
            double r = qRed(pixel);
            double g = qGreen(pixel);
            double b = qBlue(pixel);
            int a = qAlpha(pixel);

            // 1. Temperature & Tint
            if (doTT)
                applyTemperatureTint(r, g, b, temperature_value, tint_value);

            // 2. Saturation
            if (doSat)
                applySaturation(r, g, b, saturation_value);

            // 3. Vibrance
            if (doVib)
                applyVibrance(r, g, b, vibrance_value);

            // Final clamping and assignment
            line[x] = qRgba(clamp(r), clamp(g), clamp(b), a);
        }
    }

    // return the modified frame
    return frame;
}

// Generate JSON string of this object
std::string ColorAdjustment::Json() const {
    // Return formatted string
    return JsonValue().toStyledString();
}

// Generate Json::Value for this object
Json::Value ColorAdjustment::JsonValue() const {
    // Create root json object
    Json::Value root = EffectBase::JsonValue(); // get parent properties
    root["type"] = info.class_name;
    root["temperature"] = temperature.JsonValue();
    root["tint"] = tint.JsonValue();
    root["vibrance"] = vibrance.JsonValue();
    root["saturation"] = saturation.JsonValue();

    // return JsonValue
    return root;
}

// Load JSON string into this object
void ColorAdjustment::SetJson(const std::string value) {
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
void ColorAdjustment::SetJsonValue(const Json::Value root) {
    // Set parent data
    EffectBase::SetJsonValue(root);

    // Set data from Json (if key is found)
    if (!root["temperature"].isNull())
        temperature.SetJsonValue(root["temperature"]);
    if (!root["tint"].isNull())
        tint.SetJsonValue(root["tint"]);
    if (!root["vibrance"].isNull())
        vibrance.SetJsonValue(root["vibrance"]);
    if (!root["saturation"].isNull())
        saturation.SetJsonValue(root["saturation"]);
}

// Get all properties for a specific frame
std::string ColorAdjustment::PropertiesJSON(int64_t requested_frame) const {
    // Generate JSON properties list
    Json::Value root = BasePropertiesJSON(requested_frame);

    // Color adjustment keyframes
    root["temperature"] = add_property_json("Temperature", temperature.GetValue(requested_frame), "float", "", &temperature, -100.0, 100.0, false, requested_frame);
    root["tint"] = add_property_json("Tint", tint.GetValue(requested_frame), "float", "", &tint, -100.0, 100.0, false, requested_frame);
    root["vibrance"] = add_property_json("Vibrance", vibrance.GetValue(requested_frame), "float", "", &vibrance, -100.0, 100.0, false, requested_frame);
    root["saturation"] = add_property_json("Saturation", saturation.GetValue(requested_frame), "float", "", &saturation, -100.0, 100.0, false, requested_frame);

    // Return formatted string
    return root.toStyledString();
}