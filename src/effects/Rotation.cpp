#include <QPainter>

#include "Rotation.h"
#include "Exceptions.h"
#include "./image-processing-lib/src/Effects/effects.h"

using namespace openshot;

/// Blank constructor, useful when using Json to load the effect properties
Rotation::Rotation() : angle(0) {
	// Init effect properties
	init_effect_details();
}

// Default constructor
Rotation::Rotation(Keyframe newAngle, Keyframe anchorX, Keyframe anchorY)
	: angle(newAngle), rotationAnchorX(anchorX), rotationAnchorY(anchorY) {
	// Init effect properties
	init_effect_details();
}

// Init effect settings
void Rotation::init_effect_details() {
	/// Initialize the values of the EffectInfo struct.
	InitEffectInfo();

	/// Set the effect info
	info.class_name = "Rotation";
	info.name = "Rotation";
	info.description = "Rotate image by specified angle";
	info.has_audio = false;
	info.has_video = true;
}

// This method is required for all derived classes of EffectBase, and returns a
// modified openshot::Frame object
std::shared_ptr<openshot::Frame> Rotation::GetFrame(std::shared_ptr<openshot::Frame> frame, int64_t frame_number) {
    const auto angleValue = angle.GetValue(frame_number);
    const auto anchorX = rotationAnchorX.GetValue(frame_number);
    const auto anchorY = rotationAnchorY.GetValue(frame_number);

    if (angleValue == 0) {
        return frame;
    }

    const auto imagePtr = frame->GetImage();

	auto image = *imagePtr;
	// 1) Make sure we have alpha
	if (image.format() != QImage::Format_ARGB32 && image.format() != QImage::Format_ARGB32_Premultiplied) {
		image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
	}

	// 2) Prepare a transparent canvas
	QImage canvas(image.size(), QImage::Format_ARGB32_Premultiplied);
	canvas.fill(Qt::transparent);

	// 3) Set up painter with smooth transformation
	QPainter p(&canvas);
	p.setRenderHint(QPainter::SmoothPixmapTransform, true);

	// 4) Build a transform that rotates around the image center
	const QPointF center(image.width()  * anchorX, image.height() * anchorY);
	QTransform xf;
	xf.translate(center.x(), center.y());
	xf.rotate(angleValue);
	xf.translate(-center.x(), -center.y());
	p.setTransform(xf);

	// 5) Draw the original image into our canvas
	p.drawImage(0, 0, image);
	p.end();

	// 6) Replace original
	*imagePtr = std::move(canvas);

	// return the modified frame
	return frame;
}

// Generate JSON string of this object
std::string Rotation::Json() const {

	// Return formatted string
	return JsonValue().toStyledString();
}

// Generate Json::Value for this object
Json::Value Rotation::JsonValue() const {
	// Create root json object
	Json::Value root = EffectBase::JsonValue(); // get parent properties
	root["type"] = info.class_name;
	root["angle"] = angle.JsonValue();

	// return JsonValue
	return root;
}

// Load JSON string into this object
void Rotation::SetJson(const std::string value) {

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
void Rotation::SetJsonValue(const Json::Value root) {

	// Set parent data
	EffectBase::SetJsonValue(root);

	// Set data from Json (if key is found)
	if (!root["angle"].isNull())
        angle.SetJsonValue(root["angle"]);
}

// Get all properties for a specific frame
std::string Rotation::PropertiesJSON(int64_t requested_frame) const {

	// Generate JSON properties list
	Json::Value root = BasePropertiesJSON(requested_frame);

	// Keyframes
	root["angle"] = add_property_json("angle", angle.GetValue(requested_frame), "float", "", &angle, 0, 100, false, requested_frame);

	// Return formatted string
	return root.toStyledString();
}
