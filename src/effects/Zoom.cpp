#include "Zoom.h"
#include "Exceptions.h"

using namespace openshot;

/// Blank constructor, useful when using Json to load the effect properties
Zoom::Zoom() : zoomPercent(100) {
	// Init effect properties
	init_effect_details();
}

// Default constructor
Zoom::Zoom(const Keyframe& zoomPercent_, const Keyframe& anchorX_, const Keyframe& anchorY_)
    : zoomPercent(zoomPercent_), anchorX(anchorX_), anchorY(anchorY_)
{
	// Init effect properties
	init_effect_details();
}

// Init effect settings
void Zoom::init_effect_details()
{
	/// Initialize the values of the EffectInfo struct.
	InitEffectInfo();

	/// Set the effect info
	info.class_name = "Zoom";
	info.name = "Zoom";
	info.description = "Add zoom effect to your video.";
	info.has_audio = false;
	info.has_video = true;
}

// This method is required for all derived classes of EffectBase, and returns a
// modified openshot::Frame object
std::shared_ptr<openshot::Frame> Zoom::GetFrame(std::shared_ptr<openshot::Frame> frame, int64_t frame_number)
{
    auto zoomPercentVal = zoomPercent.GetValue(frame_number);
    auto anchorValX = anchorX.GetValue(frame_number);
    auto anchorValY = anchorY.GetValue(frame_number);

	// Get the frame's image
	cv::Mat frame_image = frame->GetImageCV();

    cv::Mat out_frame_image;
    int width = frame_image.cols;
    int height = frame_image.rows;

    int anchor_point_x = anchorValX * width;
    int anchor_point_y = anchorValY * height;

    if (zoomPercentVal > 100) { // Zoom In
        int newWidth = width * 100 / zoomPercentVal;
        int newHeight = height * 100 / zoomPercentVal;
        int x = std::max(anchor_point_x - newWidth / 2, 0);
        int y = std::max(anchor_point_y - newHeight / 2, 0);

        // Ensure ROI does not exceed image boundaries
        x = std::min(x, width - newWidth);
        y = std::min(y, height - newHeight);

        cv::Rect roi(x, y, newWidth, newHeight); // Define a rectangle for cropping
        cv::Mat cropped = frame_image(roi); // Crop the image
        cv::resize(cropped, out_frame_image, cv::Size(width, height)); // Resize back to original size
    } else if (zoomPercentVal < 100) { // Zoom Out
        float scaleFactor = zoomPercentVal / 100.0f;
        cv::Mat resized;
        cv::resize(frame_image, resized, cv::Size(), scaleFactor, scaleFactor, cv::INTER_LINEAR);

        int newWidth = resized.cols;
        int newHeight = resized.rows;

        int top, bottom, left, right;
        top = (height - newHeight) / 2 - (anchor_point_y - height / 2) * (1 - scaleFactor);
        bottom = (height - newHeight) - top;
        left = (width - newWidth) / 2 - (anchor_point_x - width / 2) * (1 - scaleFactor);
        right = (width - newWidth) - left;

        // Adjust for possible negative padding values
        top = std::max(top, 0);
        bottom = std::max(bottom, 0);
        left = std::max(left, 0);
        right = std::max(right, 0);

        // Mirror edges while padding
        cv::copyMakeBorder(resized, out_frame_image, top, bottom, left, right, cv::BORDER_REFLECT);
    } else { // No zooming, just copy the input image
        out_frame_image = frame_image;
    }

    frame->SetImageCV(out_frame_image);
	// return the modified frame
	return frame;
}

// Generate JSON string of this object
std::string Zoom::Json() const {

	// Return formatted string
	return JsonValue().toStyledString();
}

// Generate Json::Value for this object
Json::Value Zoom::JsonValue() const {

	// Create root json object
	Json::Value root = EffectBase::JsonValue(); // get parent properties
	root["type"] = info.class_name;
	root["zoomPercent"] = zoomPercent.JsonValue();

	// return JsonValue
	return root;
}

// Load JSON string into this object
void Zoom::SetJson(const std::string value) {

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
void Zoom::SetJsonValue(const Json::Value root) {

	// Set parent data
	EffectBase::SetJsonValue(root);

	// Set data from Json (if key is found)
	if (!root["zoomPercent"].isNull())
        zoomPercent.SetJsonValue(root["color"]);
}

// Get all properties for a specific frame
std::string Zoom::PropertiesJSON(int64_t requested_frame) const {

	// Generate JSON properties list
	Json::Value root = BasePropertiesJSON(requested_frame);

	// Keyframes
	root["zoomPercent"] = add_property_json("Zoom percent", 0.0, "zoomPercent", "", &zoomPercent, 0, std::numeric_limits<float>::infinity(), false, requested_frame);

	// Return formatted string
	return root.toStyledString();
}
