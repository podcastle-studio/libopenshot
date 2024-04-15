#include "Wipe.h"

#include "Exceptions.h"

#include "ReaderBase.h"
#include "ChunkReader.h"
#include "FFmpegReader.h"
#include "QtImageReader.h"
#include "QPainter"
#include "QPainterPath"

#ifdef USE_IMAGEMAGICK
	#include "ImageReader.h"
#endif

using namespace openshot;

/// Blank constructor, useful when using Json to load the effect properties
Wipe::Wipe(Keyframe levelsLowPercentage, Keyframe levelsHighPercentage)
    : mLevelsLowPercentage(levelsLowPercentage), mLevelsHighPercentage(levelsHighPercentage) {
	// Init effect properties
	init_effect_details();
}

// Init effect settings
void Wipe::init_effect_details()
{
	/// Initialize the values of the EffectInfo struct.
	InitEffectInfo();

	/// Set the effect info
	info.class_name = "Wipe";
	info.name = "Alpha Wipe Transition";
	info.description = "Uses a grayscale mask image to gradually wipe / transition between 2 images.";
	info.has_audio = false;
	info.has_video = true;
}

// This method is required for all derived classes of EffectBase, and returns a
// modified openshot::Frame object
std::shared_ptr<openshot::Frame> Wipe::GetFrame(std::shared_ptr<openshot::Frame> frame, int64_t frame_number) {
    const auto lowPercentage = mLevelsLowPercentage.GetValue(frame_number);
    const auto highPercentage = mLevelsHighPercentage.GetValue(frame_number);

    // Map percentages to grayscale values (0-255)
    int lowThreshold = static_cast<int>(lowPercentage * 255.0 / 100.0);
    int highThreshold = static_cast<int>(highPercentage * 255.0 / 100.0);

    std::shared_ptr<QImage> frame_image = frame->GetImage();

    // Convert to grayscale first
    QImage mask_image = frame_image->convertToFormat(QImage::Format_Grayscale8);

    // Adjust grayscale image levels
    int width = frame_image->width();
    int height = frame_image->height();

    for (int y = 0; y < height; ++y) {
        uchar* scan = mask_image.scanLine(y);
        for (int x = 0; x < width; ++x) {
            int gray = scan[x];
            if (gray < lowThreshold) {
                scan[x] = 0; // Make pixel black
            } else if (gray > highThreshold) {
                scan[x] = 255; // Make pixel white
            } else {
                // For simplicity, values exactly between thresholds are not adjusted.
                // You can add linear interpolation here if needed.
            }
        }
    }

    // Apply adjusted frame to original frame as a mask to original frame

    // Create a new image with the same size as the original and support for transparency
    QImage maskedImage(frame_image->size(), QImage::Format_ARGB32);
    maskedImage.fill(Qt::transparent); // Initialize all pixels as transparent

    // Loop through all pixels
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            QRgb originalPixel = frame_image->pixel(x, y);
            uchar maskValue = mask_image.pixelColor(x, y).value();

            // Invert mask value for alpha channel, black in mask becomes transparent in result
            int alpha = 255 - maskValue;

            // Set the new pixel value in the masked image with modified alpha
            maskedImage.setPixelColor(x, y, QColor(qRed(originalPixel), qGreen(originalPixel), qBlue(originalPixel), alpha));
        }
    }
    // return the modified frame
    frame->AddImage(std::make_shared<QImage>(maskedImage));
	return frame;
}

// Generate JSON string of this object
std::string Wipe::Json() const {

	// Return formatted string
	return JsonValue().toStyledString();
}

// Generate Json::Value for this object
Json::Value Wipe::JsonValue() const {

	// Create root json object
	Json::Value root = EffectBase::JsonValue(); // get parent properties
	root["type"] = info.class_name;
    root["levelsLowPercentage"] = mLevelsLowPercentage.JsonValue();
    root["levelsHighPercentage"] = mLevelsHighPercentage.JsonValue();
	// return JsonValue
	return root;
}

// Load JSON string into this object
void Wipe::SetJson(const std::string value) {

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
void Wipe::SetJsonValue(const Json::Value root) {
	// Set parent data
	EffectBase::SetJsonValue(root);
}

// Get all properties for a specific frame
std::string Wipe::PropertiesJSON(int64_t requested_frame) const {

	// Generate JSON properties list
	Json::Value root = BasePropertiesJSON(requested_frame);

	// Add replace_image choices (dropdown style)
//	root["replace_image"] = add_property_json("Replace Image", replace_image, "int", "", NULL, 0, 1, false, requested_frame);
//	root["replace_image"]["choices"].append(add_property_choice_json("Yes", true, replace_image));
//	root["replace_image"]["choices"].append(add_property_choice_json("No", false, replace_image));
//
//	// Keyframes
//	root["brightness"] = add_property_json("Brightness", brightness.GetValue(requested_frame), "float", "", &brightness, -1.0, 1.0, false, requested_frame);
//	root["contrast"] = add_property_json("Contrast", contrast.GetValue(requested_frame), "float", "", &contrast, 0, 20, false, requested_frame);

	// Return formatted string
	return root.toStyledString();
}
