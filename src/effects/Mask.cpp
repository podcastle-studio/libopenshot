/**
 * @file
 * @brief Source file for Mask class
 * @author Jonathan Thomas <jonathan@openshot.org>
 *
 * @ref License
 */

// Copyright (c) 2008-2019 OpenShot Studios, LLC
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "Mask.h"

#include "../gpu/GpuDevice.h"
#include "../gpu/GpuFrame.h"

#include "skia/include/core/SkImageInfo.h"
#include "skia/include/core/SkPixmap.h"
#include "skia/include/core/SkSamplingOptions.h"
#include "skia/include/core/SkShader.h"
#include "skia/include/core/SkTileMode.h"
#include "EffectShaders.h"

#include "skia/include/effects/SkRuntimeEffect.h"

#include <cmath>

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
Mask::Mask() : maskType(MaskType::INVALID), reader(NULL), replace_image(false), invert(false), fade_audio_hint(false), needs_refresh(true) {
	// Init effect properties
	init_effect_details();
}

// Default constructor
Mask::Mask(ReaderBase *mask_reader, const Keyframe& mask_brightness, const Keyframe& mask_contrast,
			const Keyframe& start_frame, const Keyframe& end_frame)
		: reader(mask_reader), needs_refresh(true), maskType(MaskType::CUSTOM), replace_image(false), invert(false), fade_audio_hint(false)
		, brightness(mask_brightness), contrast(mask_contrast), startFrame(start_frame), endFrame(end_frame)
{
	// Init effect properties
	init_effect_details();
}

Mask::Mask(MaskType _maskType, const Keyframe& mask_brightness, const Keyframe& mask_contrast,
			const Keyframe& start_frame, const Keyframe& end_frame) :
		maskType(_maskType), reader(NULL), brightness(mask_brightness), contrast(mask_contrast), replace_image(false), invert(false), fade_audio_hint(false), needs_refresh(true),
        roundedRadiusX(0), roundedRadiusY(0), startFrame(start_frame), endFrame(end_frame)
{
	// Init effect properties
	init_effect_details();
    if (maskType == MaskType::ROUNDED_CORNERS)
    {
        SetRoundedCornersMaskRadius(15, 15);
    }
}

// Init effect settings
void Mask::init_effect_details()
{
	/// Initialize the values of the EffectInfo struct.
	InitEffectInfo();

	/// Set the effect info
	info.class_name = "Mask";
	info.name = "Alpha Mask / Wipe Transition";
	info.description = "Uses a grayscale mask image to gradually wipe / transition between 2 images.";
	info.has_audio = false;
	info.has_video = true;
}

// This method is required for all derived classes of EffectBase, and returns a
// modified openshot::Frame object
std::shared_ptr<openshot::Frame> Mask::GetFrame(std::shared_ptr<openshot::Frame> frame, int64_t frame_number) {
	if (frame_number < startFrame.GetValue(frame_number) || frame_number > endFrame.GetValue(frame_number)) {
		return frame;
	}

	// The frame's size, taken from the frame rather than from its QImage, so that preparing the
	// mask below does not force a GPU-backed frame to read itself back before ApplyOnGpu can run.
	const int frame_width = frame->GetWidth();
	const int frame_height = frame->GetHeight();

	if (maskType == CUSTOM) {
		// Check if mask reader is open
		#pragma omp critical (open_mask_reader)
		{
			if (reader && !reader->IsOpen()) reader->Open();
		}

		// No reader (bail on applying the mask)
		if (!reader) return frame;

		// Get mask image (if missing or different size than frame image)
		#pragma omp critical (open_mask_reader)
		{
			if (!original_mask || !reader->info.has_single_image || needs_refresh ||
				(original_mask && original_mask->size() != QSize(frame_width, frame_height))) {

				// Only get mask if needed
				const auto mask_without_sizing = std::make_shared<QImage>(*reader->GetFrame(frame_number)->GetImage());

				// Resize mask image to match frame size
				original_mask = std::make_shared<QImage>(mask_without_sizing->scaled(
								frame_width, frame_height,
								Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
			}
		}
	} else if (maskType == ROUNDED_CORNERS) {
		QImage mask(frame_width, frame_height, QImage::Format_RGBA8888_Premultiplied);
		mask.fill(Qt::white);
		QPainter p(&mask);
		p.setRenderHint(QPainter::Antialiasing);
		QPainterPath path;
		path.addRoundedRect(QRectF(0, 0, frame_width, frame_height), roundedRadiusX, roundedRadiusY);
		QPen pen(Qt::black, 0);
		p.setPen(pen);
		p.fillPath(path, Qt::black);
		p.drawPath(path);
		p.end();
		original_mask = std::make_shared<QImage>(mask);
	}

    // Refresh no longer needed
	needs_refresh = false;

	// The shader when there is a GPU to run it on, the C++ otherwise. It comes after the mask has
	// been prepared -- that work is CPU-side either way, and the fragment takes the result as a
	// texture -- but before GetImage(), which on a GPU-backed frame is the one readback.
	if (ApplyOnGpu(frame, frame_number))
		return frame;

	// Get pixel arrays
	std::shared_ptr<QImage> frame_image = frame->GetImage();
	unsigned char *pixels = frame_image->bits();
	const unsigned char *mask_pixels = original_mask->bits();

	const double contrast_value = (contrast.GetValue(frame_number));
	const double brightness_value = (brightness.GetValue(frame_number));

	// Loop through mask pixels, and apply average gray value to frame alpha channel
	for (int pixel = 0, byte_index=0; pixel < original_mask->width() * original_mask->height(); pixel++, byte_index+=4) {
		// Get the RGB values from the pixel
		const int R = mask_pixels[byte_index];
		const int G = mask_pixels[byte_index + 1];
		const int B = mask_pixels[byte_index + 2];
		const int A = mask_pixels[byte_index + 3];

		// Get the average luminosity
		int gray_value = qGray(R, G, B);

		// Adjust the brightness
		gray_value += (255 * brightness_value);

		// Adjust the contrast
		const float factor = (20 / std::fmax(0.00001, 20.0 - contrast_value));
		gray_value = (factor * (gray_value - 128) + 128);

		// Invert the gray value if needed
		if (invert)
			gray_value = 255 - gray_value;

		// Calculate the % change in alpha
		const float alpha_percent = float(constrain(A - gray_value)) / 255.0;

		// Set the alpha channel to the gray value
		if (replace_image) {
			// Replace frame pixels with gray value (including alpha channel)
			pixels[byte_index + 0] = constrain(255 * alpha_percent);
			pixels[byte_index + 1] = constrain(255 * alpha_percent);
			pixels[byte_index + 2] = constrain(255 * alpha_percent);
			pixels[byte_index + 3] = constrain(255 * alpha_percent);
		} else {
			// Multiply new alpha value with all the colors (since we are using a premultiplied
			// alpha format)
			pixels[byte_index + 0] *= alpha_percent;
			pixels[byte_index + 1] *= alpha_percent;
			pixels[byte_index + 2] *= alpha_percent;
			pixels[byte_index + 3] *= alpha_percent;
		}
	}

	// return the modified frame
	return frame;
}

// The prepared mask as a GPU texture. See Mask.h for why this is declared there and defined here.
struct Mask::MaskTextureCache
{
	sk_sp<SkImage> texture;
	qint64 cache_key = 0;              // QImage::cacheKey(), which changes whenever the mask does
	unsigned long long generation = 0; // GpuDevice::Generation()
	std::shared_ptr<GpuFrame> owner;   // keeps the pooled surface alive alongside its snapshot
};

// The shared SkSL source lives in image-processing-lib/shaders/ so the editor loads the
// same bytes through CanvasKit; it is embedded here at build time. Read it there --
// including why it is written the way it is.
const char* Mask::GpuShaderSource() const
{
	return openshot::shaders::kMask;
}

bool Mask::SetGpuUniforms(SkRuntimeEffectBuilder& builder, int64_t frame_number,
						  int width, int height) const
{
	// GetFrame prepares original_mask before calling ApplyOnGpu; without one there is nothing to
	// apply and the CPU path bails out the same way.
	if (!original_mask || original_mask->isNull())
		return false;
	if (original_mask->width() != width || original_mask->height() != height)
		return false;

	const unsigned long long generation = GpuDevice::Generation();
	if (!mask_texture || mask_texture->cache_key != original_mask->cacheKey() ||
		mask_texture->generation != generation || !mask_texture->texture) {
		auto cache = std::make_shared<MaskTextureCache>();
		cache->cache_key = original_mask->cacheKey();
		cache->generation = generation;

		// The C++ reads the mask's raw bytes, so the upload must not convert them. Both routes
		// that build original_mask produce Format_RGBA8888_Premultiplied, which is byte-for-byte
		// kRGBA_8888 premultiplied.
		if (original_mask->format() != QImage::Format_RGBA8888_Premultiplied)
			return false;
		const SkPixmap pixels(
			SkImageInfo::Make(original_mask->width(), original_mask->height(),
							  kRGBA_8888_SkColorType, kPremul_SkAlphaType),
			original_mask->constBits(), original_mask->bytesPerLine());

		cache->owner = GpuFrame::Create(original_mask->width(), original_mask->height(),
										kRGBA_8888_SkColorType);
		if (!cache->owner || !cache->owner->upload(pixels))
			return false;
		cache->texture = cache->owner->snapshot();
		if (!cache->texture)
			return false;
		mask_texture = std::move(cache);
	}

	// Nearest and no local matrix: the mask is the frame's size, so eval(p) is the same texel the
	// C++ indexes by the same loop counter.
	builder.child("maskImage") = mask_texture->texture->makeShader(
		SkTileMode::kClamp, SkTileMode::kClamp, SkSamplingOptions());

	const double contrast_value = contrast.GetValue(frame_number);
	builder.uniform("brightnessShift") =
		static_cast<float>(255 * brightness.GetValue(frame_number));
	builder.uniform("contrastFactor") =
		static_cast<float>(20 / std::fmax(0.00001, 20.0 - contrast_value));
	builder.uniform("invertMask") = invert ? 1.0f : 0.0f;
	builder.uniform("replaceImage") = replace_image ? 1.0f : 0.0f;
	return true;
}

// Generate JSON string of this object
std::string Mask::Json() const {

	// Return formatted string
	return JsonValue().toStyledString();
}

// Generate Json::Value for this object
Json::Value Mask::JsonValue() const {

	// Create root json object
	Json::Value root = EffectBase::JsonValue(); // get parent properties
	root["type"] = info.class_name;
	root["brightness"] = brightness.JsonValue();
	root["contrast"] = contrast.JsonValue();
	if (reader)
		root["reader"] = reader->JsonValue();
	else
		root["reader"] = Json::objectValue;
	root["replace_image"] = replace_image;
	root["fade_audio_hint"] = fade_audio_hint;
	root["invert"] = invert;
	root["start_frame"] = startFrame.JsonValue();
	root["end_frame"]   = endFrame.JsonValue();
	// return JsonValue
	return root;
}

// Load JSON string into this object
void Mask::SetJson(const std::string value) {

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
void Mask::SetJsonValue(const Json::Value root) {

	// Set parent data
	EffectBase::SetJsonValue(root);

	// Set data from Json (if key is found)
	if (!root["replace_image"].isNull())
		replace_image = root["replace_image"].asBool();

	if (!root["fade_audio_hint"].isNull())
		fade_audio_hint = root["fade_audio_hint"].asBool();
	if (!root["invert"].isNull())
		invert = root["invert"].asBool();
	if (!root["brightness"].isNull())
		brightness.SetJsonValue(root["brightness"]);
	if (!root["contrast"].isNull())
		contrast.SetJsonValue(root["contrast"]);
	if (!root["start_frame"].isNull()) startFrame = root["start_frame"].asInt();
	if (!root["end_frame"].isNull())   endFrame   = root["end_frame"].asInt();
	if (!root["reader"].isNull()) // does Json contain a reader?
	{
		#pragma omp critical (open_mask_reader)
		{
			// This reader has changed, so refresh cached assets
			needs_refresh = true;

			if (!root["reader"]["type"].isNull()) // does the reader Json contain a 'type'?
			{
				// Close previous reader (if any)
				if (reader) {
					// Close and delete existing reader (if any)
					reader->Close();
					delete reader;
					reader = NULL;
				}

				// Create new reader (and load properties)
				std::string type = root["reader"]["type"].asString();

				if (type == "FFmpegReader") {

					// Create new reader
					reader = new FFmpegReader(root["reader"]["path"].asString());
					reader->SetJsonValue(root["reader"]);

	#ifdef USE_IMAGEMAGICK
				} else if (type == "ImageReader") {

					// Create new reader
					reader = new ImageReader(root["reader"]["path"].asString());
					reader->SetJsonValue(root["reader"]);
	#endif

				} else if (type == "QtImageReader") {

					// Create new reader
					reader = new QtImageReader(root["reader"]["path"].asString());
					reader->SetJsonValue(root["reader"]);

				} else if (type == "ChunkReader") {

					// Create new reader
					reader = new ChunkReader(root["reader"]["path"].asString(), (ChunkVersion) root["reader"]["chunk_version"].asInt());
					reader->SetJsonValue(root["reader"]);

				}
			}

		}
	}

}

void Mask::SetRoundedCornersMaskRadius(int x, int y)
{
    roundedRadiusX = x;
    roundedRadiusY = y;
}


// Get all properties for a specific frame
std::string Mask::PropertiesJSON(int64_t requested_frame) const {

	// Generate JSON properties list
	Json::Value root = BasePropertiesJSON(requested_frame);

	// Add replace_image choices (dropdown style)
	root["replace_image"] = add_property_json("Replace Image", replace_image, "int", "", NULL, 0, 1, false, requested_frame);
	root["replace_image"]["choices"].append(add_property_choice_json("Yes", true, replace_image));
	root["replace_image"]["choices"].append(add_property_choice_json("No", false, replace_image));

	// Timeline-only hint: equal-power audio crossfade when this Mask is a global transition
	root["fade_audio_hint"] = add_property_json("Fade Audio", fade_audio_hint, "int", "", NULL, 0, 1, false, requested_frame);
	root["fade_audio_hint"]["choices"].append(add_property_choice_json("Yes", true, fade_audio_hint));
	root["fade_audio_hint"]["choices"].append(add_property_choice_json("No", false, fade_audio_hint));

	// Add invert choices (dropdown style)
	root["invert"] = add_property_json("Invert Mask", invert, "int", "", NULL, 0, 1, false, requested_frame);
	root["invert"]["choices"].append(add_property_choice_json("Yes", true, invert));
	root["invert"]["choices"].append(add_property_choice_json("No", false, invert));

	// Keyframes
	root["brightness"] = add_property_json("Brightness", brightness.GetValue(requested_frame), "float", "", &brightness, -1.0, 1.0, false, requested_frame);
	root["contrast"] = add_property_json("Contrast", contrast.GetValue(requested_frame), "float", "", &contrast, 0, 20, false, requested_frame);

	if (reader)
		root["reader"] = add_property_json("Source", 0.0, "reader", reader->Json(), NULL, 0, 1, false, requested_frame);
	else
		root["reader"] = add_property_json("Source", 0.0, "reader", "{}", NULL, 0, 1, false, requested_frame);

	// Return formatted string
	return root.toStyledString();
}
